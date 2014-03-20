/*  =========================================================================
    zpipes_server - ZPIPES server

    Copyright contributors as noted in the AUTHORS file.                    
    This file is part of zbroker, the ZeroMQ broker project.                
                                                                            
    This is free software; you can redistribute it and/or modify it under   
    the terms of the GNU Lesser General Public License as published by the  
    Free Software Foundation; either version 3 of the License, or (at your  
    option) any later version.                                              
                                                                            
    This software is distributed in the hope that it will be useful, but    
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABIL-
    ITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General     
    Public License for more details.                                        
                                                                            
    You should have received a copy of the GNU Lesser General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.    
    =========================================================================
*/

/*
@header
    Description of class for man page.
@discuss
    Detailed discussion of the class, if any.
@end
*/

#include <czmq.h>
#include "../include/zpipes_msg.h"
#include "../include/zpipes_server.h"

//  ---------------------------------------------------------------------
//  This structure defines the context for each running server. Store
//  whatever properties and structures you need for the server.
//  

typedef struct {
    char *name;                 //  Server public name
    zhash_t *pipes;             //  Collection of pipes
} server_t;

static int
server_alloc (server_t *self)
{
    //  Get name and bind point via API
//     self->name = "default";
    //  Bind router socket to server IPC command interface
    //  We're using abstract namespaces, assuming Linux for now
//     int rc = zsocket_bind (self->router, "ipc://@/zpipes/%s", self->name);
//     assert (rc == 0);
    self->pipes = zhash_new ();
    return 0;
}

static void
server_free (server_t *self)
{
    zhash_destroy (&self->pipes);
}


//  --------------------------------------------------------------------------
//  Structure defining a single named pipe

typedef struct {
    server_t *server;           //  Parent server
    char *name;                 //  Name of pipe
    zlist_t *queue;             //  Pipe holds a queue of zchunk_t objects
    size_t links;               //  Number of callers that have opened pipe
    zframe_t *pending;          //  Routing ID of pending reader, if any
} pipe_t;

static void s_delete_pipe (void *argument);

static pipe_t *
pipe_new (server_t *server, char *name)
{
    pipe_t *self = (pipe_t *) zmalloc (sizeof (pipe_t));
    self->name = strdup (name);
    self->queue = zlist_new ();
    self->server = server;
    zhash_insert (server->pipes, name, self);
    zhash_freefn (server->pipes, name, s_delete_pipe);
    return self;
}

static void
pipe_destroy (pipe_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        pipe_t *self = *self_p;
        zchunk_t *chunk = (zchunk_t *) zlist_first (self->queue);
        while (chunk) {
            zchunk_destroy (&chunk);
            chunk = (zchunk_t *) zlist_next (self->queue);
        }
        zframe_destroy (&self->pending);
        zlist_destroy (&self->queue);
        free (self->name);
        free (self);
        *self_p = NULL;
    }
}

//  Callback when we remove group from container
static void
s_delete_pipe (void *argument)
{
    pipe_t *pipe = (pipe_t *) argument;
    pipe_destroy (&pipe);
}


//  ---------------------------------------------------------------------
//  This structure defines the state for each client connection. It will
//  be passed to each action in the 'self' argument.

typedef struct {
    //  These properties must always be present in the client_t
    server_t *server;           //  Reference to parent server
    zpipes_msg_t *request;      //  Last received request
    zpipes_msg_t *reply;        //  Reply to send out, if any
    //  These properties are specific for this application
    pipe_t *pipe;               //  Current pipe, if any
    bool writing;               //  Are we writing to pipe?
} client_t;

static int
client_alloc (client_t *self)
{
    return 0;
}

static void
client_free (client_t *self)
{
}


//  Include the generated server engine
#include "zpipes_server_engine.h"

//  --------------------------------------------------------------------------
//  open_pipe_for_input
//

static void
open_pipe_for_input (client_t *self)
{
    char *pipename = zpipes_msg_pipename (self->request);
    self->pipe = (pipe_t *) zhash_lookup (self->server->pipes, pipename);
    if (!self->pipe)
        self->pipe = pipe_new (self->server, pipename);
    self->pipe->links++;        //  Count this link to the pipe
    self->writing = false;      
}


//  --------------------------------------------------------------------------
//  open_pipe_for_output
//

static void
open_pipe_for_output (client_t *self)
{
    char *pipename = zpipes_msg_pipename (self->request);
    self->pipe = (pipe_t *) zhash_lookup (self->server->pipes, pipename);
    if (!self->pipe)
        self->pipe = pipe_new (self->server, pipename);
    self->pipe->links++;        //  Count this link to the pipe
    self->writing = true;
}


//  --------------------------------------------------------------------------
//  store_chunk_to_pipe
//

static void
store_chunk_to_pipe (client_t *self)
{
    //  State machine guarantees we're in a valid state for writing
    assert (self->pipe);
    assert (self->writing);
    zlist_append (self->pipe->queue, zpipes_msg_get_chunk (self->request));
}


//  --------------------------------------------------------------------------
//  fetch_chunk_from_pipe
//

static void
fetch_chunk_from_pipe (client_t *self)
{
    //  State machine guarantees we're in a valid state for reading
    assert (self->pipe);
    assert (!self->writing);

    //  If pipe has content, take next chunk and return it
    zchunk_t *chunk = (zchunk_t *) zlist_pop (self->pipe->queue);
    if (chunk)
        zpipes_msg_set_chunk (self->reply, &chunk);
    else
        raise_exception (self, timeout_event);
    //  How to track pending writes?
    //  Notification events...?
//     else
//         //  Pipe is empty, mark pending delivery to this caller
//         pipe->pending = caller_rid;
        //  want to send notification event to pending client
}


//  --------------------------------------------------------------------------
//  close_pipe
//

static void
close_pipe (client_t *self)
{
    assert (self->pipe);
    self->pipe->links--;
    //  Delete unused pipes if empty
    if (self->pipe->links == 0 && zlist_size (self->pipe->queue) == 0)
        zhash_delete (self->server->pipes, self->pipe->name);
}


//  --------------------------------------------------------------------------
//  Selftest

void
zpipes_server_test (bool verbose)
{
    printf (" * zpipes_server: \n");

    //  @selftest
    zctx_t *ctx = zctx_new ();

    zpipes_server_t *self = zpipes_server_new ();
    assert (self);
    zpipes_server_bind (self, "tcp://127.0.0.1:5670");

    void *writer = zsocket_new (ctx, ZMQ_DEALER);
    assert (writer);
    zsocket_connect (writer, "tcp://127.0.0.1:5670");

    void *reader = zsocket_new (ctx, ZMQ_DEALER);
    assert (reader);
    zsocket_connect (reader, "tcp://127.0.0.1:5670");
    
    zpipes_msg_t *reply;

    //  Minimal hello world test
    zpipes_msg_send_output (writer, "hello");
    reply = zpipes_msg_recv (writer);
    assert (reply);
    assert (zpipes_msg_id (reply) == ZPIPES_MSG_READY);
    zpipes_msg_destroy (&reply);
    
    zchunk_t *chunk = zchunk_new ("World", 5);
    assert (chunk);
    zpipes_msg_send_store (writer, chunk);
    zchunk_destroy (&chunk);
    reply = zpipes_msg_recv (writer);
    assert (reply);
    assert (zpipes_msg_id (reply) == ZPIPES_MSG_STORED);
    zpipes_msg_destroy (&reply);
    
    zpipes_msg_send_close (writer);
    reply = zpipes_msg_recv (writer);
    assert (reply);
    assert (zpipes_msg_id (reply) == ZPIPES_MSG_CLOSED);
    zpipes_msg_destroy (&reply);

    zpipes_msg_send_input (reader, "hello");
    reply = zpipes_msg_recv (reader);
    assert (reply);
    assert (zpipes_msg_id (reply) == ZPIPES_MSG_READY);
    zpipes_msg_destroy (&reply);

    zpipes_msg_send_fetch (reader, 0);
    reply = zpipes_msg_recv (reader);
    assert (reply);
    assert (zpipes_msg_id (reply) == ZPIPES_MSG_FETCHED);
    zpipes_msg_destroy (&reply);

    zpipes_msg_send_fetch (reader, 100);
    reply = zpipes_msg_recv (reader);
    assert (reply);
    assert (zpipes_msg_id (reply) == ZPIPES_MSG_TIMEOUT);
    zpipes_msg_destroy (&reply);

    zpipes_msg_send_close (reader);
    reply = zpipes_msg_recv (reader);
    assert (reply);
    assert (zpipes_msg_id (reply) == ZPIPES_MSG_CLOSED);
    zpipes_msg_destroy (&reply);

    zpipes_server_destroy (&self);
    
    zctx_destroy (&ctx);
    //  @end

    //  No clean way to wait for a background thread to exit
    //  Under valgrind this will randomly show as leakage
    //  Reduce this by giving server thread time to exit
    zclock_sleep (200);
    printf ("OK\n");
}
