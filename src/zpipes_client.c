/*  =========================================================================
    zpipes_client.c - simple API for zpipes client applications

    Copyright (c) the Contributors as noted in the AUTHORS file.
    This file is part of zserver, the ZeroMQ server project.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.
    =========================================================================
*/

/*
@header
    tbd
@discuss
@end
*/

#include "zbroker_classes.h"

//  ---------------------------------------------------------------------
//  Structure of zpipes_client class

struct _zpipes_client_t {
    zctx_t *ctx;                //  Private CZMQ context
    char *name;                 //  Name of named zpipe
    void *dealer;               //  Dealer socket to zpipes server
    int error;                  //  Last error cause
};


//  Returns 0 if the reply from the broker isn't what we expect
//  TODO: implement timeout when broker doesn't reply at all

static int
s_expect_reply (zpipes_client_t *self, int message_id)
{
    zpipes_msg_t *reply = zpipes_msg_recv (self->dealer);
    assert (reply);
    int rc = zpipes_msg_id (reply) == message_id? 0: -1;
    zpipes_msg_destroy (&reply);
    return rc;
}


//  ---------------------------------------------------------------------
//  Constructor; open ">pipename" for writing, "pipename" for reading

zpipes_client_t *
zpipes_client_new (const char *server_name, const char *pipe_name)
{
    //  Create new pipe API instance
    zpipes_client_t *self = (zpipes_client_t *) zmalloc (sizeof (zpipes_client_t));
    assert (self);
    
    //  Create dealer socket and connect to server IPC port
    self->ctx = zctx_new ();
    assert (self->ctx);
    self->dealer = zsocket_new (self->ctx, ZMQ_DEALER);
    assert (self->dealer);
    int rc = zsocket_connect (self->dealer, "ipc://@/zpipes/%s", server_name);
    assert (rc == 0);

    //  Open pipe for reading or writing
    if (*pipe_name == '>')
        zpipes_msg_send_output (self->dealer, pipe_name + 1);
    else
        zpipes_msg_send_input (self->dealer, pipe_name);

    if (s_expect_reply (self, ZPIPES_MSG_READY))
        assert (false);         //  Cannot happen in current use case
    return self;
}


//  ---------------------------------------------------------------------
//  Destructor

void
zpipes_client_destroy (zpipes_client_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        zpipes_client_t *self = *self_p;
        if (self->dealer) {
            zpipes_msg_send_close (self->dealer);
            if (s_expect_reply (self, ZPIPES_MSG_CLOSED))
                assert (false);     //  Cannot happen in current use case
        }
        zctx_destroy (&self->ctx);
        free (self);
        *self_p = NULL;
    }
}


//  ---------------------------------------------------------------------
//  Write chunk of data to pipe; returns number of bytes written, or -1
//  in case of error, and then sets zpipes_client_error() to EBADF.

ssize_t
zpipes_client_write (zpipes_client_t *self, void *data, size_t size)
{
    assert (self);
    zchunk_t *chunk = zchunk_new (data, size);
    assert (chunk);
    zpipes_msg_send_store (self->dealer, chunk);
    zchunk_destroy (&chunk);
    if (s_expect_reply (self, ZPIPES_MSG_STORED)) {
        self->error = EBADF;
        return -1;
    }
    else
        return size;
}


//  ---------------------------------------------------------------------
//  Read chunk of data from pipe. If timeout is non zero, waits at most
//  that many msecs for data. Returns number of bytes read, or zero if the
//  pipe was closed by the writer, and no more data is available. On a
//  timeout or interrupt, returns -1. To get the actual error code, call
//  zpipes_client_error(), which will be EINTR, EAGAIN, or EBADF.

ssize_t
zpipes_client_read (zpipes_client_t *self, void *data, size_t max_size, int timeout)
{
    assert (self);

    zpipes_msg_send_fetch (self->dealer, timeout);
    zpipes_msg_t *reply = zpipes_msg_recv (self->dealer);
    if (!reply) {
        self->error = EINTR;
        return -1;              //  Interrupted
    }
    ssize_t bytes = 0;
    if (zpipes_msg_id (reply) == ZPIPES_MSG_FETCHED) {
        zchunk_t *chunk = zpipes_msg_chunk (reply);
        bytes = zchunk_size (chunk);
        if (bytes > max_size)
            bytes = max_size;
        memcpy (data, zchunk_data (chunk), bytes);
    }
    else
    if (zpipes_msg_id (reply) == ZPIPES_MSG_END_OF_PIPE)
        bytes = 0;
    else
    if (zpipes_msg_id (reply) == ZPIPES_MSG_TIMEOUT) {
        self->error = EAGAIN;
        bytes = -1;
    }
    else {
        self->error = EBADF;
        return -1;
    }
    zpipes_msg_destroy (&reply);
    return bytes;
}


//  ---------------------------------------------------------------------
//  Returns last error number, if any

int
zpipes_client_error (zpipes_client_t *self)
{
    assert (self);
    return self->error;
}


//  ---------------------------------------------------------------------
// Self test of this class

void
zpipes_client_test (bool verbose)
{
    printf (" * zpipes_client: ");
    //  @selftest
    zpipes_server_t *server = zpipes_server_new ();
    zpipes_server_bind (server, "ipc://@/zpipes/local");

    zpipes_client_t *reader = zpipes_client_new ("local", "test pipe");
    zpipes_client_t *writer = zpipes_client_new ("local", ">test pipe");

    byte buffer [6];
    ssize_t bytes;

    //  Expect timeout error, EAGAIN
    bytes = zpipes_client_read (reader, buffer, 6, 100);
    assert (bytes == -1);
    assert (zpipes_client_error (reader) == EAGAIN);

    bytes = zpipes_client_write (writer, "CHUNK1", 6);
    assert (bytes == 6);
    bytes = zpipes_client_write (writer, "CHUNK2", 6);
    assert (bytes == 6);
    bytes = zpipes_client_write (writer, "CHUNK3", 6);
    assert (bytes == 6);

    bytes = zpipes_client_read (reader, buffer, 6, 200);
    assert (bytes == 6);
    bytes = zpipes_client_read (reader, buffer, 6, 200);
    assert (bytes == 6);
    bytes = zpipes_client_read (reader, buffer, 6, 200);
    assert (bytes == 6);

    //  Now close writer
    zpipes_client_destroy (&writer);

    //  Expect end of pipe (empty chunk)
    bytes = zpipes_client_read (reader, buffer, 6, 200);
    assert (bytes == 0);

    //  Expect illegal action (EBADF) writing on reader
    bytes = zpipes_client_write (reader, "CHUNK1", 6);
    assert (bytes == -1);
    assert (zpipes_client_error (reader) == EBADF);

    zpipes_client_destroy (&reader);
    zpipes_server_destroy (&server);

    //  @end
    printf ("OK\n");
}
