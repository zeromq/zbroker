/*  =========================================================================
    zpipes_server - ZPIPES server

    Copyright (c) the Contributors as noted in the AUTHORS file.
    This file is part of zbroker, the ZeroMQ broker project.                
                                                                            
    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.
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
//  Forward declarations for the two main classes we use here

typedef struct _server_t server_t;
typedef struct _client_t client_t;

//  This structure defines the context for each running server. Store
//  whatever properties and structures you need for the server.

struct _server_t {
    char *name;                 //  Server public name
    zhash_t *pipes;             //  Collection of pipes
};

static int
server_initialize (server_t *self)
{
    self->pipes = zhash_new ();
    return 0;
}

static void
server_terminate (server_t *self)
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
    client_t *reader;           //  Pending reader, if any
    bool terminated;            //  Pipe closed by writer?
} pipe_t;

static void s_delete_pipe (void *argument);

static pipe_t *
pipe_new (server_t *server, const char *name)
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

struct _client_t {
    //  These properties must always be present in the client_t
    server_t *server;           //  Reference to parent server
    zpipes_msg_t *request;      //  Last received request
    zpipes_msg_t *reply;        //  Reply to send out, if any
    //  These properties are specific for this application
    pipe_t *pipe;               //  Current pipe, if any
    bool writing;               //  Are we writing to pipe?
};

static int
client_initialize (client_t *self)
{
    return 0;
}

static void
client_terminate (client_t *self)
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
    const char *pipename = zpipes_msg_pipename (self->request);
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
    const char *pipename = zpipes_msg_pipename (self->request);
    self->pipe = (pipe_t *) zhash_lookup (self->server->pipes, pipename);
    if (!self->pipe)
        self->pipe = pipe_new (self->server, pipename);
    self->pipe->links++;        //  Count this link to the pipe
    self->writing = true;
}


//  --------------------------------------------------------------------------
//  expect_chunk_on_pipe
//

static void
expect_chunk_on_pipe (client_t *self)
{
    if (zpipes_msg_count (self->request) == 0)
        set_next_event (self, pipe_terminated_event);
    else
    if (zlist_size (self->pipe->queue))
        set_next_event (self, have_chunk_event);
    else
    if (self->pipe->terminated)
        set_next_event (self, pipe_terminated_event);
    else {
        assert (self->pipe->reader == NULL);
        self->pipe->reader = self;
        if (zpipes_msg_timeout (self->request))
            set_wakeup_event (self,
                              zpipes_msg_timeout (self->request),
                              timeout_expired_event);
    }
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

    //  Pipe must have content at this stage
    zchunk_t *chunk = (zchunk_t *) zlist_pop (self->pipe->queue);
    assert (chunk);
    zpipes_msg_set_chunk (self->reply, &chunk);
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

    //  Always store chunk on list, even to pass to pending reader
    zlist_append (self->pipe->queue, zpipes_msg_get_chunk (self->request));
    if (self->pipe->reader) {
        send_event (self->pipe->reader, have_chunk_event);
        assert (zlist_size (self->pipe->queue) == 0);
    }
}


//  --------------------------------------------------------------------------
//  clear_pending_reads
//

static void
clear_pending_reads (client_t *self)
{
    self->pipe->reader = NULL;
}


//  --------------------------------------------------------------------------
//  close_pipe
//

static void
close_pipe (client_t *self)
{
    assert (self->pipe);
    self->pipe->links--;
    
    //  Delete pipe if unused & empty
    if (self->pipe->links == 0 && zlist_size (self->pipe->queue) == 0)
        zhash_delete (self->server->pipes, self->pipe->name);
    else
    if (self->writing) {
        self->pipe->terminated = true;
        if (self->pipe->reader)
            send_event (self->pipe->reader, pipe_terminated_event);
    }
}


//  --------------------------------------------------------------------------
//  Selftest

static void
s_expect_reply (void *dealer, int message_id)
{
    zpipes_msg_t *reply = zpipes_msg_recv (dealer);
    assert (reply);
    assert (zpipes_msg_id (reply) == message_id);
    zpipes_msg_destroy (&reply);
}

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
    
    //  Simple reader/writer test
    zpipes_msg_send_output (writer, "hello");
    s_expect_reply (writer, ZPIPES_MSG_OUTPUT_OK);
    
    zpipes_msg_send_input (reader, "hello");
    s_expect_reply (reader, ZPIPES_MSG_INPUT_OK);
    
    //  Zero read request returns "end of pipe"
    zpipes_msg_send_read (reader, 0, 0);
    s_expect_reply (reader, ZPIPES_MSG_END_OF_PIPE);

    //  Pipeline three read requests
    zpipes_msg_send_read (reader, 1000, 100);
    zpipes_msg_send_read (reader, 1000, 100);
    zpipes_msg_send_read (reader, 1000, 100);

    //  First will return with a timeout
    s_expect_reply (reader, ZPIPES_MSG_TIMEOUT);

    //  Store a chunk
    zchunk_t *chunk = zchunk_new ("World", 5);
    assert (chunk);
    zpipes_msg_send_write (writer, chunk, 100);
    zchunk_destroy (&chunk);
    s_expect_reply (writer, ZPIPES_MSG_WRITE_OK);
    
    //  Second will return with a chunk
    s_expect_reply (reader, ZPIPES_MSG_READ_OK);

    //  Close writer
    zpipes_msg_send_close (writer);
    s_expect_reply (writer, ZPIPES_MSG_CLOSE_OK);
    
    //  Third will return empty
    s_expect_reply (reader, ZPIPES_MSG_END_OF_PIPE);

    //  Close reader
    zpipes_msg_send_close (reader);
    s_expect_reply (reader, ZPIPES_MSG_CLOSE_OK);

    zpipes_server_destroy (&self);
    zctx_destroy (&ctx);
    //  @end

    //  No clean way to wait for a background thread to exit
    //  Under valgrind this will randomly show as leakage
    //  Reduce this by giving server thread time to exit
    zclock_sleep (200);
    printf ("OK\n");
}
