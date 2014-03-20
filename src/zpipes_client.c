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

#include "zpipes_classes.h"

//  ---------------------------------------------------------------------
//  Structure of zpipes_client class

struct _zpipes_client_t {
    zctx_t *ctx;                //  Private CZMQ context
    char *name;                 //  Name of named zpipe
    void *dealer;               //  Dealer socket to zpipes broker
};


//  ---------------------------------------------------------------------
//  Constructor

zpipes_client_t *
zpipes_client_new (char *broker_name, char *zpipes_client_name)
{
    //  Create new pipe API instance
    zpipes_client_t *self = (zpipes_client_t *) zmalloc (sizeof (zpipes_client_t));
    assert (self);
    self->name = strdup (zpipes_client_name);

    //  Create dealer socket and connect to broker IPC port
    self->ctx = zctx_new ();
    self->dealer = zsocket_new (self->ctx, ZMQ_DEALER);
    if (self->dealer) {
        int rc = zsocket_connect (self->dealer, "ipc://@/zpipes/%s", broker_name);
        assert (rc == 0);

        //  Open the pipe and wait for broker confirmation
        zstr_sendm (self->dealer, "OPEN");
        zstr_send (self->dealer, self->name);
        char *ack = zstr_recv (self->dealer);
        free (ack);
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
            //  Close the pipe and wait for broker confirmation
            zstr_sendm (self->dealer, "CLOSE");
            zstr_send (self->dealer, self->name);
            char *ack = zstr_recv (self->dealer);
            free (ack);
        }
        //  Destroy this pipe API instance
        zctx_destroy (&self->ctx);
        free (self->name);
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
        //  Send chunk to pipe and wait for broker confirmation
        zstr_sendm (self->dealer, "WRITE");
        zstr_sendm (self->dealer, self->name);
        zmq_send (self->dealer, data, size, 0);
        char *ack = zstr_recv (self->dealer);
        free (ack);
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
        zstr_sendm (self->dealer, "READ");
        zstr_send (self->dealer, self->name);
        int rc = zmq_recv (self->dealer, data, max_size, 0);
        return (size_t) rc;
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
    zpipes_t *broker = zpipes_new ("local");

    zpipes_client_t *pitcher = zpipes_client_new ("local", "shared pipe");
    zpipes_client_t *catcher = zpipes_client_new ("local", "shared pipe");

    zpipes_client_write (pitcher, "CHUNK1", 6);
    zpipes_client_write (pitcher, "CHUNK2", 6);
    zpipes_client_write (pitcher, "CHUNK3", 6);

    byte buffer [6];
    size_t bytes;
    bytes = zpipes_client_read (catcher, buffer, 6);
    assert (bytes == 6);
    bytes = zpipes_client_read (catcher, buffer, 6);
    assert (bytes == 6);
    bytes = zpipes_client_read (catcher, buffer, 6);
    assert (bytes == 6);

    zpipes_client_destroy (&pitcher);
    zpipes_client_destroy (&catcher);

    zpipes_destroy (&broker);

    //  @end
    printf ("OK\n");
}
