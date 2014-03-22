/*  =========================================================================
    zpipes_client.c - simple API for zpipes client applications

    Copyright contributors as noted in the AUTHORS file.
    This file is part of zbroker, the ZeroMQ broker project.

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

#include "../include/zpipes.h"
#include "../include/zpipes_msg.h"

//  ---------------------------------------------------------------------
//  Structure of zpipes_client class

struct _zpipes_client_t {
    zctx_t *ctx;                //  Private CZMQ context
    char *name;                 //  Name of named zpipe
    void *dealer;               //  Dealer socket to zpipes broker
};


static void
s_expect_reply (zpipes_client_t *self, int message_id)
{
    zpipes_msg_t *reply = zpipes_msg_recv (self->dealer);
    assert (reply);
    //  Current behavior when faced with unexpected reply is to die
    assert (zpipes_msg_id (reply) == message_id);
    zpipes_msg_destroy (&reply);
}


//  ---------------------------------------------------------------------
//  Constructor

zpipes_client_t *
zpipes_client_new (const char *broker_name, const char *pipe_name)
{
    //  Create new pipe API instance
    zpipes_client_t *self = (zpipes_client_t *) zmalloc (sizeof (zpipes_client_t));
    assert (self);

    //  Create dealer socket and connect to broker IPC port
    self->ctx = zctx_new ();
    self->dealer = zsocket_new (self->ctx, ZMQ_DEALER);
    if (self->dealer) {
        int rc = zsocket_connect (self->dealer, "ipc://@/zpipes/%s", broker_name);
        assert (rc == 0);
        if (*pipe_name == '>')
            zpipes_msg_send_output (self->dealer, pipe_name + 1);
        else
            zpipes_msg_send_input (self->dealer, pipe_name);
        s_expect_reply (self, ZPIPES_MSG_READY);
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
            s_expect_reply (self, ZPIPES_MSG_CLOSED);
        }
        zctx_destroy (&self->ctx);
        free (self);
        *self_p = NULL;
    }
}


//  ---------------------------------------------------------------------
//  Write chunk of data to pipe

void
zpipes_client_write (zpipes_client_t *self, void *data, size_t size)
{
    assert (self);
    if (self->dealer) {
        zchunk_t *chunk = zchunk_new (data, size);
        assert (chunk);
        zpipes_msg_send_store (self->dealer, chunk);
        zchunk_destroy (&chunk);
        s_expect_reply (self, ZPIPES_MSG_STORED);
    }
}


//  ---------------------------------------------------------------------
//  Read chunk of data from pipe, blocks until data arrives
//  Returns size of chunk read; if less than max_size, truncates

size_t
zpipes_client_read (zpipes_client_t *self, void *data, size_t max_size)
{
    assert (self);
    if (self->dealer) {
        //  Use timeout of 200 msecs for now
        zpipes_msg_send_fetch (self->dealer, 200);
        zpipes_msg_t *reply = zpipes_msg_recv (self->dealer);
        assert (reply);
        assert (zpipes_msg_id (reply) == ZPIPES_MSG_FETCHED);
        //  Return chunk data
        zchunk_t *chunk = zpipes_msg_chunk (reply);
        size_t bytes = zchunk_size (chunk);
        if (bytes > max_size)
            bytes = max_size;
        memcpy (data, zchunk_data (chunk), bytes);
        zpipes_msg_destroy (&reply);
        return bytes;
    }
    else
        return 0;
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

    zpipes_client_write (writer, "CHUNK1", 6);
    zpipes_client_write (writer, "CHUNK2", 6);
    zpipes_client_write (writer, "CHUNK3", 6);

    byte buffer [6];
    size_t bytes;
    bytes = zpipes_client_read (reader, buffer, 6);
    assert (bytes == 6);
    bytes = zpipes_client_read (reader, buffer, 6);
    assert (bytes == 6);
    bytes = zpipes_client_read (reader, buffer, 6);
    assert (bytes == 6);

    zpipes_client_destroy (&writer);
    zpipes_client_destroy (&reader);
    zpipes_server_destroy (&server);

    //  @end
    printf ("OK\n");
}
