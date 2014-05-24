/*  =========================================================================
    zpipes_test_cluster - test zpipes over a cluster

    Copyright (c) the Contributors as noted in the AUTHORS file.
    This file is part of zbroker, the ZeroMQ broker project.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.
    =========================================================================
*/

#include "zbroker_classes.h"

static void
s_wait (char *message)
{
//     puts (message);
}


int main (void)
{
    char *animate = "0";
    
    zactor_t *hosta = zactor_new (zpipes_server, NULL);
    zstr_sendx (hosta, "BIND", "ipc://@/zpipes/hosta", NULL);
    zstr_sendx (hosta, "SET", "server/animate", animate, NULL);
    zstr_sendx (hosta, "SET", "zyre/interval", "100", NULL);
    zstr_sendx (hosta, "SET", "zyre/nodeid", "hosta", NULL);
    zstr_sendx (hosta, "JOIN CLUSTER", NULL);
    char *reply = zstr_recv (hosta);

    //  If the machine has no usable broadcast interface, the JOIN CLUSTER
    //  command will fail, and then there's no point in continuing...
    if (strneq (reply, "OK")) {
        zclock_log ("W: skipping test, no UDP discovery");
        free (reply);
        zactor_destroy (&hosta);
        return 0;
    }
    free (reply);

    zactor_t *hostb = zactor_new (zpipes_server, NULL);
    zstr_sendx (hostb, "BIND", "ipc://@/zpipes/hostb", NULL);
    zstr_sendx (hostb, "SET", "server/animate", animate, NULL);
    zstr_sendx (hostb, "SET", "zyre/interval", "100", NULL);
    zstr_sendx (hostb, "SET", "zyre/nodeid", "hostb", NULL);
    zstr_sendx (hostb, "JOIN CLUSTER", NULL);
    reply = zstr_recv (hostb);
    assert (streq (reply, "OK"));
    free (reply);
    
    //  Give time for cluster to interconnect
    zclock_sleep (250);

    byte buffer [100];
    ssize_t bytes;

    //  Test 1 - simple read-write
    s_wait ("Open writer");
    zpipes_client_t *writer = zpipes_client_new ("hostb", ">test pipe");

    s_wait ("Open reader");
    zpipes_client_t *reader = zpipes_client_new ("hosta", "test pipe");

    //  Expect timeout error, EAGAIN
    s_wait ("Read impatiently");
    bytes = zpipes_client_read (reader, buffer, 6, 200);
    assert (bytes == -1);
    assert (zpipes_client_error (reader) == EAGAIN);
    
    s_wait ("Write to pipe");
    bytes = zpipes_client_write (writer, "Hello, World", 12, 0);
    assert (bytes == 12);
    
    s_wait ("Read from pipe");
    bytes = zpipes_client_read (reader, buffer, 12, 0);
    assert (bytes == 12);
    
    s_wait ("Write three chunks");
    bytes = zpipes_client_write (writer, "CHUNK1", 6, 200);
    assert (bytes == 6);
    bytes = zpipes_client_write (writer, "CHUNK2", 6, 200);
    assert (bytes == 6);
    bytes = zpipes_client_write (writer, "CHUNK3", 6, 200);
    assert (bytes == 6);

    s_wait ("Read two chunks");
    bytes = zpipes_client_read (reader, buffer, 1, 200);
    assert (bytes == 1);
    bytes = zpipes_client_read (reader, buffer, 10, 200);
    assert (bytes == 10);
    
    s_wait ("Close writer");
    zpipes_client_destroy (&writer);

    //  Expect end of pipe (short read)
    s_wait ("Read short");
    bytes = zpipes_client_read (reader, buffer, 50, 200);
    assert (bytes == 7);

    //  Expect end of pipe (empty chunk)
    s_wait ("Read end of pipe");
    bytes = zpipes_client_read (reader, buffer, 50, 200);
    assert (bytes == 0);

    //  Expect illegal action (EBADF) writing on reader
    s_wait ("Try to write on reader");
    bytes = zpipes_client_write (reader, "CHUNK1", 6, 200);
    assert (bytes == -1);
    assert (zpipes_client_error (reader) == EBADF);

    s_wait ("Close reader");
    zpipes_client_destroy (&reader);
    
    //  Test 2 - pipe reuse
    s_wait ("Open reader");
    reader = zpipes_client_new ("hosta", "test pipe 2");

    s_wait ("Open writer");
    writer = zpipes_client_new ("hostb", ">test pipe 2");

    s_wait ("Close reader");
    zpipes_client_destroy (&reader);

    s_wait ("Close writer");
    zpipes_client_destroy (&writer);

    s_wait ("Open reader reusing pipe name");
    reader = zpipes_client_new ("hosta", "test pipe 2");
    zpipes_client_destroy (&reader);

    zactor_destroy (&hosta);
    zactor_destroy (&hostb);
    return 0;
}
