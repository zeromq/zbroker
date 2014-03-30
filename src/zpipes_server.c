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
    client_t *writer;           //  Pipe writer, if any
    client_t *reader;           //  Pipe reader, if any
} pipe_t;

static void s_delete_pipe (void *argument);

static pipe_t *
pipe_new (server_t *server, const char *name)
{
    pipe_t *self = (pipe_t *) zmalloc (sizeof (pipe_t));
    self->name = strdup (name);
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
    size_t pending;             //  Current total size of queue
    zlist_t *queue;             //  Queue of chunks to be delivered
};

static int
client_initialize (client_t *self)
{
    self->queue = zlist_new ();
    return 0;
}

static void
client_terminate (client_t *self)
{
    zchunk_t *chunk = (zchunk_t *) zlist_first (self->queue);
    while (chunk) {
        zchunk_destroy (&chunk);
        chunk = (zchunk_t *) zlist_next (self->queue);
    }
    zlist_destroy (&self->queue);
}

//  Include the generated server engine
#include "zpipes_server_engine.h"


//  --------------------------------------------------------------------------
//  lookup_or_create_pipe
//

static void
lookup_or_create_pipe (client_t *self)
{
    const char *pipename = zpipes_msg_pipename (self->request);
    self->pipe = (pipe_t *) zhash_lookup (self->server->pipes, pipename);
    if (!self->pipe)
        self->pipe = pipe_new (self->server, pipename);
}


//  --------------------------------------------------------------------------
//  open_pipe_writer
//

static void
open_pipe_writer (client_t *self)
{
    assert (self->pipe);
    if (self->pipe->writer == NULL)
        self->pipe->writer = self;
    else
        //  Two writers on same pipe isn't allowed
        raise_exception (self, exception_event);
}


//  --------------------------------------------------------------------------
//  open_pipe_reader
//

static void
open_pipe_reader (client_t *self)
{
    assert (self->pipe);
    if (self->pipe->reader == NULL) {
        self->pipe->reader = self;
        //  If writer was waiting, wake it up with a have_reader event
        if (self->pipe->writer)
            send_event (self->pipe->writer, have_reader_event);
    }
    else
        //  Two readers on same pipe isn't allowed
        raise_exception (self, exception_event);
}


//  --------------------------------------------------------------------------
//  close_pipe_writer
//

static void
close_pipe_writer (client_t *self)
{
    assert (self->pipe);

    //  Destroy the pipe, and make sure the reader knows about this
    if (self->pipe->reader)
        //  Wipe out reader's reference to pipe
        self->pipe->reader->pipe = NULL;

    //  Destroy the pipe now
    zhash_delete (self->server->pipes, self->pipe->name);
}


//  --------------------------------------------------------------------------
//  close_pipe_reader
//

static void
close_pipe_reader (client_t *self)
{
    if (self->pipe)
        self->pipe->reader = NULL;
}


//  --------------------------------------------------------------------------
//  expect_pipe_reader
//

static void
expect_pipe_reader (client_t *self)
{
    assert (self->pipe);
    if (self->pipe->reader)
        set_next_event (self, have_reader_event);
    else
    if (zpipes_msg_timeout (self->request))
        set_wakeup_event (self, zpipes_msg_timeout (self->request), wakeup_event);
    //
    //  or else wait until a reader arrives
}


//  --------------------------------------------------------------------------
//  pass_data_to_reader
//

static void
pass_data_to_reader (client_t *self)
{
    assert (self->pipe);

    //  Reach through to reader's queue and store data there
    zchunk_t *chunk = zpipes_msg_get_chunk (self->request);
    zlist_append (self->pipe->reader->queue, chunk);
    self->pipe->reader->pending += zchunk_size (chunk);
    send_event (self->pipe->reader, have_data_event);
}


//  --------------------------------------------------------------------------
//  expect_pipe_data
//

static void
expect_pipe_data (client_t *self)
{
    if (zpipes_msg_size (self->request) == 0)
        raise_exception (self, read_nothing_event);
    else
    if (zlist_size (self->queue))
        set_next_event (self, have_data_event);
    else
    if (!self->pipe)
        //  Repeated read on closed pipe will return end-of-pipe
        set_next_event (self, pipe_terminated_event);
    else
    if (zpipes_msg_timeout (self->request))
        set_wakeup_event (self, zpipes_msg_timeout (self->request), wakeup_event);
    //
    //  or else wait until a writer has data for us
}


//  --------------------------------------------------------------------------
//  collect_data_to_send
//

static void
collect_data_to_send (client_t *self)
{
    //  Do we have enough data to satisfy the read request?
    size_t required = zpipes_msg_size (self->request);
    if (self->pending >= required) {
        //  Create a bucket chunk with the required max size
        zchunk_t *bucket = zchunk_new (NULL, required);

        //  Now fill the bucket with chunks from our queue
        while (zchunk_size (bucket) < required) {
            //  Get next chunk and consume as much of it as possible
            zchunk_t *chunk = (zchunk_t *) zlist_pop (self->queue);
            assert (chunk);
            zchunk_consume (bucket, chunk);
            //  If chunk is exhausted, destroy it
            if (zchunk_exhausted (chunk))
                zchunk_destroy (&chunk);
            else {
                //  Push chunk back for next time
                zlist_push (self->queue, chunk);
                assert (zchunk_size (bucket) == required);
            }
        }
        zpipes_msg_set_chunk (self->reply, &bucket);
        self->pending -= required;
    }
    else
        raise_exception (self, not_enough_data_event);
}


//  --------------------------------------------------------------------------
//  Selftest

static int
s_expect_reply (void *dealer, int message_id)
{
    zpipes_msg_t *reply = zpipes_msg_recv (dealer);
    assert (reply);
    int rc = zpipes_msg_id (reply) == message_id? 0: -1;
    zpipes_msg_destroy (&reply);
    return rc;
}

void
zpipes_server_test (bool verbose)
{
    printf (" * zpipes_server: \n");

    //  @selftest
    zctx_t *ctx = zctx_new ();

    //  Test cases to do:
    //  - close writer, with active reader, try to reopen, should work
    //  - multiple readers, on pipes with same names (after writer closes)
    //  - repeated read on closed pipe will return end-of-pipe
    //  - two writers on same pipe isn't allowed
    //  - if writer was waiting, wake it up with a have_reader event
    //  - two readers on same pipe isn't allowed

    zpipes_server_t *self = zpipes_server_new ();
    assert (self);
    zpipes_server_bind (self, "ipc://@/zpipes/local");

    void *writer = zsocket_new (ctx, ZMQ_DEALER);
    assert (writer);
    zsocket_connect (writer, "ipc://@/zpipes/local");

    void *reader = zsocket_new (ctx, ZMQ_DEALER);
    assert (reader);
    zsocket_connect (reader, "ipc://@/zpipes/local");
    
    //  --------------------------------------------------------------------
    //  Simple hello world test

    //  Now open reader on pipe
    zpipes_msg_send_output (writer, "test pipe");
    if (s_expect_reply (writer, ZPIPES_MSG_OUTPUT_OK))
        assert (false);
    
    //  Write will timeout if there's no reader
    zchunk_t *chunk = zchunk_new ("Hello, World", 12);
    int32_t timeout = 100;
    zpipes_msg_send_write (writer, chunk, timeout);
    if (s_expect_reply (writer, ZPIPES_MSG_TIMEOUT))
        assert (false);

    //  Now open reader on pipe
    zpipes_msg_send_input (reader, "test pipe");
    if (s_expect_reply (reader, ZPIPES_MSG_INPUT_OK))
        assert (false);

    //  Read will timeout if there's no data
    zpipes_msg_send_read (reader, 12, timeout);
    if (s_expect_reply (reader, ZPIPES_MSG_TIMEOUT))
        assert (false);

    //  Write should now be successful
    zpipes_msg_send_write (writer, chunk, 0);
    zchunk_destroy (&chunk);
    if (s_expect_reply (writer, ZPIPES_MSG_WRITE_OK))
        assert (false);

    //  Read should now be successful
    zpipes_msg_send_read (reader, 12, 0);
    if (s_expect_reply (reader, ZPIPES_MSG_READ_OK))
        assert (false);

    //  Zero read request returns "end of pipe"
    zpipes_msg_send_read (reader, 0, 0);
    if (s_expect_reply (reader, ZPIPES_MSG_END_OF_PIPE))
        assert (false);

    zpipes_msg_send_close (writer);
    if (s_expect_reply (writer, ZPIPES_MSG_CLOSE_OK))
        assert (false);

    //  Non-zero read request returns "end of pipe"
    zpipes_msg_send_read (reader, 12, timeout);
    if (s_expect_reply (reader, ZPIPES_MSG_END_OF_PIPE))
        assert (false);

    zpipes_msg_send_close (reader);
    if (s_expect_reply (reader, ZPIPES_MSG_CLOSE_OK))
        assert (false);
    
    //  --------------------------------------------------------------------
    //  Test re-chunking

//
//     //  Pipeline three read requests
//     zpipes_msg_send_read (reader, 5, 100);
//     zpipes_msg_send_read (reader, 5, 100);
//     zpipes_msg_send_read (reader, 5, 100);
//
//     //  First will return with a timeout
//     if (s_expect_reply (reader);
//     assert (reply == ZPIPES_MSG_TIMEOUT);
//
//     //  Store a chunk
//     zchunk_t *chunk = zchunk_new ("World", 5);
//     assert (chunk);
//     zpipes_msg_send_write (writer, chunk, 0);
//     zchunk_destroy (&chunk);
//     if (s_expect_reply (writer);
//     assert (reply == ZPIPES_MSG_WRITE_OK);
//
//     //  Second will return with a chunk
//     if (s_expect_reply (reader);
//     assert (reply == ZPIPES_MSG_READ_OK);
//
//     //  Close writer
//     zpipes_msg_send_close (writer);
//     if (s_expect_reply (writer);
//     assert (reply == ZPIPES_MSG_CLOSE_OK);
//
//     //  Third will return empty
//     if (s_expect_reply (reader);
//     assert (reply == ZPIPES_MSG_END_OF_PIPE);


    zpipes_server_destroy (&self);
    zctx_destroy (&ctx);
    //  @end

    //  No clean way to wait for a background thread to exit
    //  Under valgrind this will randomly show as leakage
    //  Reduce this by giving server thread time to exit
    zclock_sleep (200);
    printf ("OK\n");
}
