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
#include <zyre.h>
#include "../include/zpipes_msg.h"
#include "../include/zpipes_server.h"

//  This method handles all traffic from other server nodes

static int
zyre_handler (zloop_t *loop, zmq_pollitem_t *item, void *argument);

//  ---------------------------------------------------------------------
//  Forward declarations for the two main classes we use here

typedef struct _server_t server_t;
typedef struct _client_t client_t;

//  This structure defines the context for each running server. Store
//  whatever properties and structures you need for the server.

struct _server_t {
    //  These properties must always be present in the server_t
    //  and are set by the generated engine; do not modify them!
    zctx_t *ctx;                //  Parent ZeroMQ context
    zlog_t *log;                //  For any logging needed
    zconfig_t *config;          //  Current loaded configuration
    
    //  These properties are specific for this application
    char *name;                 //  Server public name
    zhash_t *pipes;             //  Collection of pipes
    zyre_t *zyre;               //  Zyre node
};

//  --------------------------------------------------------------------------
//  Structure defining a single named pipe; a pipe routes data chunks from
//  one reader to one writer.

typedef struct {
    char *name;                 //  Name of pipe
    server_t *server;           //  Parent server
    client_t *writer;           //  Pipe writer, if local
    client_t *reader;           //  Pipe reader, if local
    char *remote;               //  Remote reader/writer if any
} pipe_t;

//  Used for pipe reader or writer to indicate client on remote node
#define REMOTE_NODE (client_t *) -1

//  ---------------------------------------------------------------------
//  This structure defines the state for each client connection. It will
//  be passed to each action in the 'self' argument.

struct _client_t {
    //  These properties must always be present in the client_t
    //  and are set by the generated engine; do not modify them!
    server_t *server;           //  Reference to parent server
    zpipes_msg_t *request;      //  Last received request
    zpipes_msg_t *reply;        //  Reply to send out, if any

    //  These properties are specific for this application
    pipe_t *pipe;               //  Current pipe, if any
    size_t pending;             //  Current total size of queue
    zlist_t *queue;             //  Queue of chunks to be delivered
};

//  Include the generated server engine

#include "zpipes_server_engine.h"


//  Allocate properties and structures for a new server instance.
//  Return 0 if OK, or -1 if there was an error.

static int
server_initialize (server_t *self)
{
    self->pipes = zhash_new ();
    self->zyre = zyre_new (self->ctx);
    //  Set rapid cluster discovery; this is tuned for wired LAN not WiFi
    zyre_set_interval (self->zyre, 250);
    zyre_start (self->zyre);
    zyre_join (self->zyre, "ZPIPES");
    zlog_info (self->log, "joining cluster as %s", zyre_uuid (self->zyre));
    engine_handle_socket (self, zyre_socket (self->zyre), zyre_handler);
    return 0;
}

//  Free properties and structures for a server instance

static void
server_terminate (server_t *self)
{
    zyre_destroy (&self->zyre);
    zhash_destroy (&self->pipes);
}


//  Allocate properties and structures for a new client connection and
//  optionally engine_set_next_event (). Return 0 if OK, or -1 on error.

static int
client_initialize (client_t *self)
{
    self->queue = zlist_new ();
    return 0;
}

//  Free properties and structures for a client connection

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


static void
client_store_chunk (client_t *self, zchunk_t **chunk_p)
{
    zchunk_t *chunk = *chunk_p;
    assert (chunk);
    zlist_append (self->queue, chunk);
    self->pending += zchunk_size (chunk);
    *chunk_p = NULL;
}


static void s_delete_pipe (void *argument);

//  Constructor

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

//  Destructor

static void
pipe_destroy (pipe_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        pipe_t *self = *self_p;
        
        //  Remove pipe from hash of known pipes
        zhash_freefn (self->server->pipes, self->name, NULL);
        zhash_delete (self->server->pipes, self->name);
        free (self->remote);
        free (self->name);
        free (self);
        *self_p = NULL;
    }
}

//  Return true if pipe has an attached reader

static bool
pipe_accepts_data (pipe_t *self)
{
    assert (self);
    return self->reader != NULL;
}

//  Set local pipe reader; returns 0 if OK, -1 if not possible (due to
//  previous existing reader).

static int
pipe_attach_local_reader (pipe_t *self, client_t *reader)
{
    assert (self);
    if (self->reader == NULL) {
        self->reader = reader;
        if (self->writer == NULL) {
            //  Announce that we have a new pipe reader so that writers
            //  in the cluster may discover us
            zmsg_t *msg = zmsg_new ();
            zmsg_addstr (msg, "HAVE READER");
            zmsg_addstr (msg, self->name);
            zyre_shout (self->server->zyre, "ZPIPES", &msg);
        }
        else
        if (self->writer == REMOTE_NODE) {
            //  Tell remote node we would like to be reader
            zmsg_t *msg = zmsg_new ();
            zmsg_addstr (msg, "HAVE READER");
            zmsg_addstr (msg, self->name);
            zyre_whisper (self->server->zyre, self->remote, &msg);
        }
        else
            engine_send_event (self->writer, have_reader_event);

        return 0;
    }
    return -1;                  //  Pipe already has reader
}

//  Set remote pipe reader, if possible, else returns -1. If we have a
//  local pipe writer, signal that to the remote node. If not, we will
//  signal when a local pipe writer arrives.

static int
pipe_attach_remote_reader (pipe_t *self, const char *remote)
{
    assert (self);
    if (self->reader == NULL) {
        //  This is how we indicate a remote reader
        self->reader = REMOTE_NODE;
        self->remote = strdup (remote);

        if (self->writer) {
            //  Tell remote node we're acting as writer
            zmsg_t *msg = zmsg_new ();
            zmsg_addstr (msg, "HAVE WRITER");
            zmsg_addstr (msg, self->name);
            zyre_whisper (self->server->zyre, self->remote, &msg);
        }
        return 0;
    }
    return -1;                  //  Pipe already has reader
}

//  Set local pipe writer; returns 0 if OK, -1 if not possible (due to 
//  previous existing writer).

static int
pipe_attach_local_writer (pipe_t *self, client_t *writer)
{
    assert (self);
    if (self->writer == NULL) {
        self->writer = writer;
        if (self->reader == NULL) {
            //  Announce that we have a new pipe writer so that readers
            //  in the cluster may discover us
            zmsg_t *msg = zmsg_new ();
            zmsg_addstr (msg, "HAVE WRITER");
            zmsg_addstr (msg, self->name);
            zyre_shout (self->server->zyre, "ZPIPES", &msg);
        }
        else
        if (self->reader == REMOTE_NODE) {
            //  Tell remote node we would like to be writer
            zmsg_t *msg = zmsg_new ();
            zmsg_addstr (msg, "HAVE WRITER");
            zmsg_addstr (msg, self->name);
            zyre_whisper (self->server->zyre, self->remote, &msg);
        }
        else
            engine_send_event (self->reader, have_writer_event);
        return 0;
    }
    return -1;
}

//  Set remote pipe writer, if possible, else returns -1. If we have a
//  local pipe reader, signal that to the remote node. If not, we will
//  signal when a local pipe reader arrives.

static int
pipe_attach_remote_writer (pipe_t *self, const char *remote)
{
    assert (self);
    if (self->writer == NULL) {
        //  This is how we indicate a remote writer
        self->writer = REMOTE_NODE;
        self->remote = strdup (remote);

        if (self->reader == NULL) {
            //  Tell remote node we're acting as reader
            zmsg_t *msg = zmsg_new ();
            zmsg_addstr (msg, "HAVE READER");
            zmsg_addstr (msg, self->name);
            zyre_whisper (self->server->zyre, self->remote, &msg);
        }
        return 0;
    }
    return -1;
}


//  Drop pipe reader and handshake pipe destruction via writer if any

static void
pipe_drop_local_reader (pipe_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        pipe_t *self = *self_p;
        //  TODO: what if self->reader is REMOTE_NODE?
        self->reader = NULL;
        if (self->writer) {
            if (self->writer == REMOTE_NODE) {
                //  Tell remote node we're dropping off
                zmsg_t *msg = zmsg_new ();
                zmsg_addstr (msg, "DROP READER");
                zmsg_addstr (msg, self->name);
                zyre_whisper (self->server->zyre, self->remote, &msg);
            }
            else {
                engine_send_event (self->writer, reader_dropped_event);
                //  Don't destroy pipe yet - writer is still using it
                *self_p = NULL;
            }
        }
        else
        pipe_destroy (self_p);
    }
}

//  Drop pipe writer and handshake pipe destruction via reader if any

static void
pipe_drop_local_writer (pipe_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        pipe_t *self = *self_p;
        //  TODO: what if self->writer is REMOTE_NODE?
        self->writer = NULL;
        if (self->reader) {
            if (self->reader == REMOTE_NODE) {
                //  Tell remote node we're dropping off
                zmsg_t *msg = zmsg_new ();
                zmsg_addstr (msg, "DROP WRITER");
                zmsg_addstr (msg, self->name);
                zyre_whisper (self->server->zyre, self->remote, &msg);
            }
            else {
                engine_send_event (self->reader, writer_dropped_event);
                //  Don't destroy pipe yet - reader is still using it
                *self_p = NULL;
            }
        }
        pipe_destroy (self_p);
    }
}

//  Drop remote pipe reader

static void
pipe_drop_remote_reader (pipe_t **self_p, const char *remote)
{
    assert (self_p);
    if (*self_p) {
        pipe_t *self = *self_p;
        if (self->reader == REMOTE_NODE && streq (self->remote, remote)) {
            self->reader = NULL;
            if (self->writer) {
                assert (self->writer != REMOTE_NODE);
                engine_send_event (self->writer, reader_dropped_event);
                //  Don't destroy pipe yet - writer is still using it
                *self_p = NULL;
            }
        }
        pipe_destroy (self_p);
    }
}

//  Drop remote pipe writer

static void
pipe_drop_remote_writer (pipe_t **self_p, const char *remote)
{
    assert (self_p);
    if (*self_p) {
        pipe_t *self = *self_p;
        if (self->writer == REMOTE_NODE && streq (self->remote, remote)) {
            self->writer = NULL;
            if (self->reader) {
                assert (self->reader != REMOTE_NODE);
                engine_send_event (self->reader, writer_dropped_event);
                //  Don't destroy pipe yet - reader is still using it
                *self_p = NULL;
            }
        }
        pipe_destroy (self_p);
    }
}


//  Send data through pipe from writer to reader

static void
pipe_send_data (pipe_t *self, zchunk_t **chunk_p)
{
    assert (self);
    assert (self->reader);

    zchunk_t *chunk = *chunk_p;
    assert (chunk);
    
    if (self->reader == REMOTE_NODE) {
        //  Send chunk to remote node reader
        zmsg_t *msg = zmsg_new ();
        zmsg_addstr (msg, "DATA");
        zmsg_addstr (msg, self->name);
        zmsg_addmem (msg, zchunk_data (chunk), zchunk_size (chunk));
        zyre_whisper (self->server->zyre, self->remote, &msg);
        zchunk_destroy (chunk_p);
    }
    else {
        client_store_chunk (self->reader, chunk_p);
        engine_send_event (self->reader, have_data_event);
    }
}

//  Callback when we remove group from container

static void
s_delete_pipe (void *argument)
{
    pipe_t *pipe = (pipe_t *) argument;
    pipe_destroy (&pipe);
}


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
    if (pipe_attach_local_writer (self->pipe, self) == 0)
        engine_set_next_event (self, ok_event);
    else
        engine_set_next_event (self, error_event);
}


//  --------------------------------------------------------------------------
//  open_pipe_reader
//

static void
open_pipe_reader (client_t *self)
{
    assert (self->pipe);
    if (pipe_attach_local_reader (self->pipe, self) == 0)
        engine_set_next_event (self, ok_event);
    else
        engine_set_next_event (self, error_event);
}


//  --------------------------------------------------------------------------
//  close_pipe_writer
//

static void
close_pipe_writer (client_t *self)
{
    pipe_drop_local_writer (&self->pipe);
}


//  --------------------------------------------------------------------------
//  close_pipe_reader
//

static void
close_pipe_reader (client_t *self)
{
    pipe_drop_local_reader (&self->pipe);
}


//  --------------------------------------------------------------------------
//  process_write_request
//

static void
process_write_request (client_t *self)
{
    if (self->pipe == NULL)
        engine_set_next_event (self, pipe_shut_event);
    else
    if (pipe_accepts_data (self->pipe))
        engine_set_next_event (self, have_reader_event);
    else
    if (zpipes_msg_timeout (self->request))
        engine_set_wakeup_event (self,
            zpipes_msg_timeout (self->request), wakeup_event);
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
    zchunk_t *chunk = zpipes_msg_get_chunk (self->request);
    pipe_send_data (self->pipe, &chunk);
}


//  --------------------------------------------------------------------------
//  process_read_request
//

static void
process_read_request (client_t *self)
{
    if (zpipes_msg_size (self->request) == 0)
        engine_set_next_event (self, zero_read_event);
    else
    if (zlist_size (self->queue))
        engine_set_next_event (self, have_data_event);
    else
    if (!self->pipe)
        //  Read on closed pipe returns READ END
        engine_set_next_event (self, pipe_shut_event);
    else
    if (zpipes_msg_timeout (self->request))
        engine_set_wakeup_event (self,
                zpipes_msg_timeout (self->request), wakeup_event);
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
    
    //  If pipe was closed, we'll do a short read with as much
    //  data as we have pending
    if (required > self->pending && self->pipe == NULL)
        required = self->pending;

    if (self->pipe == NULL && self->pending == 0)
        engine_set_exception (self, pipe_shut_event);
    else
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
        engine_set_exception (self, not_enough_data_event);
}


//  --------------------------------------------------------------------------
//  Handle Zyre traffic

static void
server_process_cluster_command (server_t *self, const char *remote, zmsg_t *msg)
{
    char *request = zmsg_popstr (msg);
    char *pipename = zmsg_popstr (msg);

    //  Lookup or create pipe
    //  TODO: remote pipes need cleaning up with some timeout
    pipe_t *pipe = (pipe_t *) zhash_lookup (self->pipes, pipename);
    if (!pipe)
        pipe = pipe_new (self, pipename);

    if (streq (request, "HAVE WRITER"))
        pipe_attach_remote_writer (pipe, remote);
    else
    if (streq (request, "HAVE READER"))
        pipe_attach_remote_reader (pipe, remote);
    else
    if (streq (request, "DATA")) {
        //  TODO encode these commands as proper protocol
        zframe_t *frame = zmsg_pop (msg);
        zchunk_t *chunk = zchunk_new (zframe_data (frame), zframe_size (frame));
        zframe_destroy (&frame);
        if (pipe->writer == REMOTE_NODE)
            pipe_send_data (pipe, &chunk);
        zchunk_destroy (&chunk);
    }
    else
    if (streq (request, "DROP READER"))
        pipe_drop_remote_reader (&pipe, remote);
    else
    if (streq (request, "DROP WRITER"))
        pipe_drop_remote_writer (&pipe, remote);
    else
        zlog_warning (self->log, "bad request %s from %s", request, remote);

    zstr_free (&pipename);
    zstr_free (&request);
}

static int
zyre_handler (zloop_t *loop, zmq_pollitem_t *item, void *argument)
{
    server_t *self = (server_t *) argument;
    zmsg_t *msg = zyre_recv (self->zyre);
    if (!msg)
        return -1;              //  Interrupted

//     zmsg_dump (msg);
    char *command = zmsg_popstr (msg);
    char *remote = zmsg_popstr (msg);

    if (streq (command, "ENTER"))
        zlog_info (self->log, "ZPIPES server appeared at %s", remote);
    else
    if (streq (command, "EXIT"))
        zlog_info (self->log, "ZPIPES server vanished from %s", remote);
    else
    if (streq (command, "SHOUT")) {
        //  TODO: gentler error handling later; this makes zbroker
        //  vulnerable to DoS attacks
        char *group = zmsg_popstr (msg);
        assert (streq (group, "ZPIPES"));
        server_process_cluster_command (self, remote, msg);
        zstr_free (&group);
    }
    else
    if (streq (command, "WHISPER"))
        server_process_cluster_command (self, remote, msg);
    
    zstr_free (&command);
    zstr_free (&remote);
    zmsg_destroy (&msg);
    
    return 0;
}


//  --------------------------------------------------------------------------
//  Selftest

static int
s_expect_reply (void *dealer, int message_id)
{
    zpipes_msg_t *reply = zpipes_msg_recv (dealer);
    if (!reply) {
        puts ("- interrupted");
        return -1;
    }
    int rc = zpipes_msg_id (reply) == message_id? 0: -1;
    if (rc)
        printf ("W: expected %d, got %d/%s\n",
                message_id,
                zpipes_msg_id (reply),
                zpipes_msg_command (reply));
    zpipes_msg_destroy (&reply);
    return rc;
}

void
zpipes_server_test (bool verbose)
{
    printf (" * zpipes_server: \n");

    //  @selftest
    zctx_t *ctx = zctx_new ();

    //  Prepare test cases
    zpipes_server_t *self = zpipes_server_new ();
    zpipes_server_set (self, "server/animate", verbose? "1": "0");

    assert (self);
    zpipes_server_bind (self, "ipc://@/zpipes/local");

    void *writer = zsocket_new (ctx, ZMQ_DEALER);
    assert (writer);
    zsocket_connect (writer, "ipc://@/zpipes/local");

    void *writer2 = zsocket_new (ctx, ZMQ_DEALER);
    assert (writer2);
    zsocket_connect (writer2, "ipc://@/zpipes/local");

    void *reader = zsocket_new (ctx, ZMQ_DEALER);
    assert (reader);
    zsocket_connect (reader, "ipc://@/zpipes/local");
    
    void *reader2 = zsocket_new (ctx, ZMQ_DEALER);
    assert (reader2);
    zsocket_connect (reader2, "ipc://@/zpipes/local");
    
    zchunk_t *chunk = zchunk_new ("Hello, World", 12);
    int32_t timeout = 100;
    
    //  --------------------------------------------------------------------
    //  Basic tests

    //  Open writer on pipe
    zpipes_msg_send_output (writer, "test pipe");
    if (s_expect_reply (writer, ZPIPES_MSG_OUTPUT_OK))
        assert (false);

    //  Write will timeout if there's no reader
    zpipes_msg_send_write (writer, chunk, timeout);
    if (s_expect_reply (writer, ZPIPES_MSG_WRITE_TIMEOUT))
        assert (false);

    //  Now open reader on pipe
    zpipes_msg_send_input (reader, "test pipe");
    if (s_expect_reply (reader, ZPIPES_MSG_INPUT_OK))
        assert (false);

    //  Read will timeout if there's no data
    zpipes_msg_send_read (reader, 12, timeout);
    if (s_expect_reply (reader, ZPIPES_MSG_READ_TIMEOUT))
        assert (false);

    //  Write should now be successful
    zpipes_msg_send_write (writer, chunk, 0);
    if (s_expect_reply (writer, ZPIPES_MSG_WRITE_OK))
        assert (false);

    //  Read should now be successful
    zpipes_msg_send_read (reader, 12, 0);
    if (s_expect_reply (reader, ZPIPES_MSG_READ_OK))
        assert (false);

    //  Zero read request returns "end of pipe"
    zpipes_msg_send_read (reader, 0, 0);
    if (s_expect_reply (reader, ZPIPES_MSG_READ_END))
        assert (false);

    //  Close writer
    zpipes_msg_send_close (writer);
    if (s_expect_reply (writer, ZPIPES_MSG_CLOSE_OK))
        assert (false);

    //  Any read request returns "end of pipe"
    zpipes_msg_send_read (reader, 12, timeout);
    if (s_expect_reply (reader, ZPIPES_MSG_READ_END))
        assert (false);

    //  Close reader
    zpipes_msg_send_close (reader);
    if (s_expect_reply (reader, ZPIPES_MSG_CLOSE_OK))
        assert (false);

    //  --------------------------------------------------------------------
    //  Test pipelining (request queuing & filtering)

    //  Open reader on pipe
    zpipes_msg_send_input (reader, "test pipe");
    if (s_expect_reply (reader, ZPIPES_MSG_INPUT_OK))
        assert (false);

    //  Open writer on pipe
    zpipes_msg_send_output (writer, "test pipe");
    if (s_expect_reply (writer, ZPIPES_MSG_OUTPUT_OK))
        assert (false);

    //  Pipeline three read requests
    zpipes_msg_send_read (reader, 12, timeout);
    zpipes_msg_send_read (reader, 12, timeout);
    zpipes_msg_send_read (reader, 12, timeout);

    //  First read will return with a timeout
    if (s_expect_reply (reader, ZPIPES_MSG_READ_TIMEOUT))
        assert (false);

    //  Write chunk to pipe
    zpipes_msg_send_write (writer, chunk, 0);
    if (s_expect_reply (writer, ZPIPES_MSG_WRITE_OK))
        assert (false);

    //  Second read will succeed
    if (s_expect_reply (reader, ZPIPES_MSG_READ_OK))
        assert (false);

    //  Send PING, expect PING-OK back
    zpipes_msg_send_ping (reader);
    if (s_expect_reply (reader, ZPIPES_MSG_PING_OK))
        assert (false);

    //  Close writer
    zpipes_msg_send_close (writer);
    if (s_expect_reply (writer, ZPIPES_MSG_CLOSE_OK))
        assert (false);

    //  Third read will report end of pipe
    if (s_expect_reply (reader, ZPIPES_MSG_READ_END))
        assert (false);

    //  Close reader
    zpipes_msg_send_close (reader);
    if (s_expect_reply (reader, ZPIPES_MSG_CLOSE_OK))
        assert (false);

    //  Read now fails as pipe is closed
    zpipes_msg_send_read (reader, 12, timeout);
    if (s_expect_reply (reader, ZPIPES_MSG_INVALID))
        assert (false);

    //  Closing an already closed pipe is an error
    zpipes_msg_send_close (reader);
    if (s_expect_reply (reader, ZPIPES_MSG_INVALID))
        assert (false);

    //  --------------------------------------------------------------------
    //  Test read/close pipelining

    //  Open reader on pipe
    zpipes_msg_send_input (reader, "test pipe");
    if (s_expect_reply (reader, ZPIPES_MSG_INPUT_OK))
        assert (false);

    //  Open writer on pipe
    zpipes_msg_send_output (writer, "test pipe");
    if (s_expect_reply (writer, ZPIPES_MSG_OUTPUT_OK))
        assert (false);

    //  Pipeline two read requests
    zpipes_msg_send_read (reader, 12, timeout);
    zpipes_msg_send_read (reader, 12, timeout);

    //  Send PING, expect PING-OK back
    zpipes_msg_send_ping (reader);
    if (s_expect_reply (reader, ZPIPES_MSG_PING_OK))
        assert (false);

    //  Close reader
    zpipes_msg_send_close (reader);
    
    //  First read now fails
    if (s_expect_reply (reader, ZPIPES_MSG_READ_FAILED))
        assert (false);
    
    if (s_expect_reply (reader, ZPIPES_MSG_CLOSE_OK))
        assert (false);

    //  Second read is now invalid
    if (s_expect_reply (reader, ZPIPES_MSG_INVALID))
        assert (false);
    
    //  Close writer
    zpipes_msg_send_close (writer);
    if (s_expect_reply (writer, ZPIPES_MSG_CLOSE_OK))
        assert (false);

    //  --------------------------------------------------------------------
    //  Test reads and writes of different sizes

    //  Open reader on pipe
    zpipes_msg_send_input (reader, "test pipe");
    if (s_expect_reply (reader, ZPIPES_MSG_INPUT_OK))
        assert (false);

    //  Open writer on pipe
    zpipes_msg_send_output (writer, "test pipe");
    if (s_expect_reply (writer, ZPIPES_MSG_OUTPUT_OK))
        assert (false);

    //  Write chunk to pipe
    zpipes_msg_send_write (writer, chunk, 0);
    if (s_expect_reply (writer, ZPIPES_MSG_WRITE_OK))
        assert (false);

    //  Close writer
    zpipes_msg_send_close (writer);
    if (s_expect_reply (writer, ZPIPES_MSG_CLOSE_OK))
        assert (false);

    //  Read back in several steps
    zpipes_msg_send_read (reader, 1, 0);
    if (s_expect_reply (reader, ZPIPES_MSG_READ_OK))
        assert (false);
    zpipes_msg_send_read (reader, 2, 0);
    if (s_expect_reply (reader, ZPIPES_MSG_READ_OK))
        assert (false);
    zpipes_msg_send_read (reader, 3, 0);
    if (s_expect_reply (reader, ZPIPES_MSG_READ_OK))
        assert (false);
    zpipes_msg_send_read (reader, 3, 0);
    if (s_expect_reply (reader, ZPIPES_MSG_READ_OK))
        assert (false);
    
    //  We get a short read (3 bytes)
    zpipes_msg_send_read (reader, 100, 0);
    if (s_expect_reply (reader, ZPIPES_MSG_READ_OK))
        assert (false);
    
    //  Pipe is now empty
    zpipes_msg_send_read (reader, 100, 0);
    if (s_expect_reply (reader, ZPIPES_MSG_READ_END))
        assert (false);
    
    //  Close reader
    zpipes_msg_send_close (reader);
    if (s_expect_reply (reader, ZPIPES_MSG_CLOSE_OK))
        assert (false);

    //  --------------------------------------------------------------------
    //  Test connection expiry

    //  Set connection timeout to 200 msecs
    zpipes_server_set (self, "server/timeout", "200");

    //  Open reader on pipe
    zpipes_msg_send_input (reader, "test pipe");
    if (s_expect_reply (reader, ZPIPES_MSG_INPUT_OK))
        assert (false);

    //  Read will expire, we don't expect any response for this command
    zpipes_msg_send_read (reader, 12, 0);

    //  Do nothing for long enough for the timeout to hit
    zclock_sleep (300);

    //  Try again, server should now treat the client as disconnected
    zpipes_msg_send_read (reader, 12, 0);
    if (s_expect_reply (reader, ZPIPES_MSG_INVALID))
        assert (false);

    //  Now check that disconnection erases pipe contents

    //  Open reader on pipe
    zpipes_msg_send_input (reader, "test pipe");
    if (s_expect_reply (reader, ZPIPES_MSG_INPUT_OK))
        assert (false);

    //  Open writer on pipe
    zpipes_msg_send_output (writer, "test pipe");
    if (s_expect_reply (writer, ZPIPES_MSG_OUTPUT_OK))
        assert (false);

    //  Write chunk to pipe
    zpipes_msg_send_write (writer, chunk, 0);
    if (s_expect_reply (writer, ZPIPES_MSG_WRITE_OK))
        assert (false);

    //  Do nothing for long enough for the timeout to hit
    //  Both writer and reader should be disconnected
    zclock_sleep (300);

    //  Open reader on pipe
    zpipes_msg_send_input (reader, "test pipe");
    if (s_expect_reply (reader, ZPIPES_MSG_INPUT_OK))
        assert (false);

    //  Open writer on pipe
    zpipes_msg_send_output (writer, "test pipe");
    if (s_expect_reply (writer, ZPIPES_MSG_OUTPUT_OK))
        assert (false);

    //  This read should timeout, as pipe is empty
    zpipes_msg_send_read (reader, 12, timeout);
    if (s_expect_reply (reader, ZPIPES_MSG_READ_TIMEOUT))
        assert (false);

    //  Close writer
    zpipes_msg_send_close (writer);
    if (s_expect_reply (writer, ZPIPES_MSG_CLOSE_OK))
        assert (false);

    //  Close reader
    zpipes_msg_send_close (reader);
    if (s_expect_reply (reader, ZPIPES_MSG_CLOSE_OK))
        assert (false);
    
    //  --------------------------------------------------------------------
    //  Test writer closing while reader still active

    //  Open writer on pipe
    zpipes_msg_send_output (writer, "test pipe");
    if (s_expect_reply (writer, ZPIPES_MSG_OUTPUT_OK))
        assert (false);

    //  Open reader on pipe
    zpipes_msg_send_input (reader, "test pipe");
    if (s_expect_reply (reader, ZPIPES_MSG_INPUT_OK))
        assert (false);

    //  Write one chunk to pipe
    zpipes_msg_send_write (writer, chunk, 0);
    if (s_expect_reply (writer, ZPIPES_MSG_WRITE_OK))
        assert (false);

    //  Close writer, before reader has read data
    zpipes_msg_send_close (writer);
    if (s_expect_reply (writer, ZPIPES_MSG_CLOSE_OK))
        assert (false);

    //  Open writer on same pipe name
    zpipes_msg_send_output (writer, "test pipe");
    if (s_expect_reply (writer, ZPIPES_MSG_OUTPUT_OK))
        assert (false);

    //  Read should still be successful
    zpipes_msg_send_read (reader, 12, 0);
    if (s_expect_reply (reader, ZPIPES_MSG_READ_OK))
        assert (false);

    //  Create second reader and open pipe for input
    zpipes_msg_send_input (reader2, "test pipe");
    if (s_expect_reply (reader2, ZPIPES_MSG_INPUT_OK))
        assert (false);

    //  Write one chunk to pipe, will go to second instance
    zpipes_msg_send_write (writer, chunk, 0);
    if (s_expect_reply (writer, ZPIPES_MSG_WRITE_OK))
        assert (false);

    //  Pipe is terminated and empty
    zpipes_msg_send_read (reader, 0, 0);
    if (s_expect_reply (reader, ZPIPES_MSG_READ_END))
        assert (false);

    //  Reader2 should be successful
    zpipes_msg_send_read (reader2, 12, 0);
    if (s_expect_reply (reader2, ZPIPES_MSG_READ_OK))
        assert (false);

    //  Close reader 
    zpipes_msg_send_close (reader);
    if (s_expect_reply (reader, ZPIPES_MSG_CLOSE_OK))
        assert (false);

    //  Close writer
    zpipes_msg_send_close (writer);
    if (s_expect_reply (writer, ZPIPES_MSG_CLOSE_OK))
        assert (false);
    
    //  Pipe is terminated and empty
    zpipes_msg_send_read (reader2, 0, 0);
    if (s_expect_reply (reader2, ZPIPES_MSG_READ_END))
        assert (false);

    //  Do that again to be sure it wasn't a coincidence :)
    zpipes_msg_send_read (reader2, 0, 0);
    if (s_expect_reply (reader2, ZPIPES_MSG_READ_END))
        assert (false);

    //  Close reader2
    zpipes_msg_send_close (reader2);
    if (s_expect_reply (reader2, ZPIPES_MSG_CLOSE_OK))
        assert (false);

    //  --------------------------------------------------------------------
    //  Test reader closing while writer still active

    //  Open writer on pipe
    zpipes_msg_send_output (writer, "test pipe");
    if (s_expect_reply (writer, ZPIPES_MSG_OUTPUT_OK))
        assert (false);

    //  Open reader on pipe
    zpipes_msg_send_input (reader, "test pipe");
    if (s_expect_reply (reader, ZPIPES_MSG_INPUT_OK))
        assert (false);

    //  Write one chunk to pipe
    zpipes_msg_send_write (writer, chunk, 0);
    if (s_expect_reply (writer, ZPIPES_MSG_WRITE_OK))
        assert (false);

    //  Read should be successful
    zpipes_msg_send_read (reader, 12, 0);
    if (s_expect_reply (reader, ZPIPES_MSG_READ_OK))
        assert (false);

    //  Close reader
    zpipes_msg_send_close (reader);
    if (s_expect_reply (reader, ZPIPES_MSG_CLOSE_OK))
        assert (false);

    //  Write should fail
    zpipes_msg_send_write (writer, chunk, 0);
    if (s_expect_reply (writer, ZPIPES_MSG_WRITE_FAILED))
        assert (false);

    //  Close writer
    zpipes_msg_send_close (writer);
    if (s_expect_reply (writer, ZPIPES_MSG_CLOSE_OK))
        assert (false);

    //  --------------------------------------------------------------------
    //  Two readers or writers on same pipe are not allowed

    //  Open writer on pipe
    zpipes_msg_send_output (writer, "test pipe");
    if (s_expect_reply (writer, ZPIPES_MSG_OUTPUT_OK))
        assert (false);

    //  Open second writer on pipe
    zpipes_msg_send_output (writer2, "test pipe");
    if (s_expect_reply (writer2, ZPIPES_MSG_OUTPUT_FAILED))
        assert (false);

    //  Open reader on pipe
    zpipes_msg_send_input (reader, "test pipe");
    if (s_expect_reply (reader, ZPIPES_MSG_INPUT_OK))
        assert (false);

    //  Open second reader on pipe
    zpipes_msg_send_input (reader2, "test pipe");
    if (s_expect_reply (reader2, ZPIPES_MSG_INPUT_FAILED))
        assert (false);

    //  Close reader
    zpipes_msg_send_close (reader);
    if (s_expect_reply (reader, ZPIPES_MSG_CLOSE_OK))
        assert (false);

    //  Close writer
    zpipes_msg_send_close (writer);
    if (s_expect_reply (writer, ZPIPES_MSG_CLOSE_OK))
        assert (false);
    
    //  --------------------------------------------------------------------
    //  Test short read when writer closes

    //  Open writer on pipe
    zpipes_msg_send_output (writer, "test pipe");
    if (s_expect_reply (writer, ZPIPES_MSG_OUTPUT_OK))
        assert (false);

    //  Open reader on pipe
    zpipes_msg_send_input (reader, "test pipe");
    if (s_expect_reply (reader, ZPIPES_MSG_INPUT_OK))
        assert (false);

    //  Write one chunk to pipe
    zpipes_msg_send_write (writer, chunk, 0);
    if (s_expect_reply (writer, ZPIPES_MSG_WRITE_OK))
        assert (false);

    //  Try to read large amount of data, will block
    zpipes_msg_send_read (reader, 1000, 0);
    
    //  Close writer
    zpipes_msg_send_close (writer);
    if (s_expect_reply (writer, ZPIPES_MSG_CLOSE_OK))
        assert (false);

    //  Reader should now return short read
    if (s_expect_reply (reader, ZPIPES_MSG_READ_OK))
        assert (false);

    //  Pipe is terminated and empty
    zpipes_msg_send_read (reader, 0, 0);
    if (s_expect_reply (reader, ZPIPES_MSG_READ_END))
        assert (false);

    //  Close reader
    zpipes_msg_send_close (reader);
    if (s_expect_reply (reader, ZPIPES_MSG_CLOSE_OK))
        assert (false);

    //  --------------------------------------------------------------------
    zchunk_destroy (&chunk);
    zpipes_server_destroy (&self);
    zctx_destroy (&ctx);
    //  @end

    //  No clean way to wait for a background thread to exit
    //  Under valgrind this will randomly show as leakage
    //  Reduce this by giving server thread time to exit
    zclock_sleep (200);
    printf ("OK\n");
}
