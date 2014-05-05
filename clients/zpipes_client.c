/*  =========================================================================
    zpipes_client.c - API for zpipes client applications

    Copyright (c) the Contributors as noted in the AUTHORS file.
    This file is part of zbroker, the ZeroMQ server project.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.
    =========================================================================
*/

/*
@header
    Provides an API to the ZPIPES infrastructure.
@discuss
@end
*/

#include <zmtp.h>
#include "zchunk.h"
#include "zpipes_msg.h"
#include "zpipes_client.h"


//  ---------------------------------------------------------------------
//  Structure of zpipes_client class

struct _zpipes_client_t {
    zmtp_dealer_t *dealer;      //  Dealer socket to zpipes server
    int error;                  //  Last error cause
};


//  Return 0 if the reply from the broker is what we expect, else return
//  -1. This includes interrupts.
//  TODO: implement timeout when broker doesn't reply at all
//  TODO: this loop should also PING the broker every second

static int
s_expect_reply (zpipes_client_t *self, int message_id)
{
    zpipes_msg_t *reply = zpipes_msg_recv (self->dealer);
    if (!reply)
        return -1;
    int rc = zpipes_msg_id (reply) == message_id? 0: -1;
    zpipes_msg_destroy (&reply);
    return rc;
}


//  ---------------------------------------------------------------------
//  Constructor; open ">pipename" for writing, "pipename" for reading
//  Returns a new client instance, or NULL if there was an error (e.g.
//  two readers trying to access same pipe).

zpipes_client_t *
zpipes_client_new (const char *server_name, const char *pipe_name)
{
    //  Create new pipe API instance
    zpipes_client_t *self = (zpipes_client_t *) zmalloc (sizeof (zpipes_client_t));
    assert (self);
    
    //  Create dealer socket and connect to server IPC port
    self->dealer = zmtp_dealer_new ();
    assert (self->dealer);

    char endpoint [256];
    snprintf (endpoint, 255, "ipc://@/zpipes/%s", server_name);
    endpoint [255] = 0;
    int rc = zmtp_dealer_ipc_connect (self->dealer, endpoint);
    assert (rc == 0);

    //  Open pipe for reading or writing
    if (*pipe_name == '>') {
        zpipes_msg_send_output (self->dealer, pipe_name + 1);
        if (s_expect_reply (self, ZPIPES_MSG_OUTPUT_OK))
            zpipes_client_destroy (&self);
    }
    else {
        zpipes_msg_send_input (self->dealer, pipe_name);
        if (s_expect_reply (self, ZPIPES_MSG_INPUT_OK))
            zpipes_client_destroy (&self);
    }
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
            //  Get reply, ignore it: could be ok or an error depending
            //  on what the client did before.
            zpipes_msg_t *reply = zpipes_msg_recv (self->dealer);
            zpipes_msg_destroy (&reply);
        }
        free (self);
        *self_p = NULL;
    }
}


//  ---------------------------------------------------------------------
//  Write chunk of data to pipe; returns number of bytes written, or -1
//  in case of error. To get the actual error code, call
//  zpipes_client_error(), which will be EINTR, EAGAIN, or EBADF.

ssize_t
zpipes_client_write (zpipes_client_t *self, void *data, size_t size, int timeout)
{
    assert (self);
    zchunk_t *chunk = zchunk_new (data, size);
    assert (chunk);
    zpipes_msg_t *request = zpipes_msg_new (ZPIPES_MSG_WRITE);
    zpipes_msg_set_chunk (request, &chunk);
    zpipes_msg_set_timeout (request, timeout);
    zpipes_msg_send (&request, self->dealer);

    zpipes_msg_t *reply = zpipes_msg_recv (self->dealer);
    if (!reply) {
        self->error = EINTR;
        return -1;              //  Interrupted
    }
    ssize_t rc = size;
    if (zpipes_msg_id (reply) == ZPIPES_MSG_WRITE_TIMEOUT) {
        self->error = EAGAIN;
        rc = -1;
    }
    else
    if (zpipes_msg_id (reply) == ZPIPES_MSG_WRITE_FAILED) {
        //  TODO: better error code?
        //  This happens if we close a pipe while there's a pending write
        self->error = EINTR;
        rc = -1;
    }
    else
    if (zpipes_msg_id (reply) == ZPIPES_MSG_INVALID) {
        self->error = EBADF;
        rc = -1;
    }
    zpipes_msg_destroy (&reply);
    return rc;
}


//  ---------------------------------------------------------------------
//  Read from the pipe. If the timeout is non-zero, waits at most that
//  many msecs for data. Returns number of bytes read, or zero if the
//  pipe was closed by the writer, and no more data is available. On a
//  timeout or interrupt, returns -1. To get the actual error code, call
//  zpipes_client_error(), which will be EINTR, EAGAIN, or EBADF.

ssize_t
zpipes_client_read (zpipes_client_t *self, void *data, size_t size, int timeout)
{
    assert (self);

    zpipes_msg_send_read (self->dealer, size, timeout);
    zpipes_msg_t *reply = zpipes_msg_recv (self->dealer);
    if (!reply) {
        self->error = EINTR;
        return -1;              //  Interrupted
    }
    ssize_t rc = 0;
    if (zpipes_msg_id (reply) == ZPIPES_MSG_READ_OK) {
        zchunk_t *chunk = zpipes_msg_chunk (reply);
        ssize_t bytes = zchunk_size (chunk);
        assert (bytes <= size);
        memcpy (data, zchunk_data (chunk), bytes);
        rc = bytes;
    }
    else
    if (zpipes_msg_id (reply) == ZPIPES_MSG_READ_END)
        rc = 0;
    else
    if (zpipes_msg_id (reply) == ZPIPES_MSG_READ_TIMEOUT) {
        self->error = EAGAIN;
        rc = -1;
    }
    else
    if (zpipes_msg_id (reply) == ZPIPES_MSG_READ_FAILED) {
        //  TODO: better error code?
        //  This happens if we close a pipe while there's a pending read
        self->error = EINTR;
        rc = -1;
    }
    else
    if (zpipes_msg_id (reply) == ZPIPES_MSG_INVALID) {
        self->error = EBADF;
        rc = -1;
    }
    zpipes_msg_destroy (&reply);
    return rc;
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
    puts ("Please start zbroker and press [Enter]");
    getchar ();
    
    zpipes_client_t *reader = zpipes_client_new ("local", "test pipe");
    zpipes_client_t *writer = zpipes_client_new ("local", ">test pipe");

    byte buffer [100];
    ssize_t bytes;

    //  Expect timeout error, EAGAIN
    bytes = zpipes_client_read (reader, buffer, 6, 100);
    assert (bytes == -1);
    assert (zpipes_client_error (reader) == EAGAIN);

    bytes = zpipes_client_write (writer, "CHUNK1", 6, 100);
    assert (bytes == 6);
    bytes = zpipes_client_write (writer, "CHUNK2", 6, 100);
    assert (bytes == 6);
    bytes = zpipes_client_write (writer, "CHUNK3", 6, 100);
    assert (bytes == 6);

    bytes = zpipes_client_read (reader, buffer, 1, 100);
    assert (bytes == 1);
    bytes = zpipes_client_read (reader, buffer, 10, 100);
    assert (bytes == 10);

    //  Now close writer
    zpipes_client_destroy (&writer);

    //  Expect end of pipe (short read)
    bytes = zpipes_client_read (reader, buffer, 50, 100);
    assert (bytes == 7);
    
    //  Expect end of pipe (empty chunk)
    bytes = zpipes_client_read (reader, buffer, 50, 100);
    assert (bytes == 0);

    //  Expect illegal action (EBADF) writing on reader
    bytes = zpipes_client_write (reader, "CHUNK1", 6, 100);
    assert (bytes == -1);
    assert (zpipes_client_error (reader) == EBADF);

    zpipes_client_destroy (&reader);
    
    //  @end
    printf ("OK\n");
}
