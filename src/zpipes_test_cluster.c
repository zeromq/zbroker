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

int main (void)
{
    zsys_info ("zpipes_test_cluster: *** starting cluster tests ***");
    
    zactor_t *green = zactor_new (zpipes_server, NULL);
    zstr_sendx (green, "BIND", "ipc://@/zpipes/green", NULL);
    zstr_sendx (green, "SET", "zyre/interval", "100", NULL);
    zstr_sendx (green, "SET", "zyre/nodeid", "green", NULL);
    
    //  JOIN CLUSTER message always replies with OK/SNAFU
    zstr_sendx (green, "JOIN CLUSTER", NULL);
    char *reply = zstr_recv (green);

    //  If the machine has no usable broadcast interface, the JOIN CLUSTER
    //  command will fail, and then there's no point in continuing...
    if (strneq (reply, "OK")) {
        zclock_log ("W: skipping test, no UDP discovery");
        free (reply);
        zactor_destroy (&green);
        return 0;
    }
    free (reply);

    zactor_t *orange = zactor_new (zpipes_server, NULL);
    zstr_sendx (orange, "BIND", "ipc://@/zpipes/orange", NULL);
    zstr_sendx (orange, "SET", "zyre/interval", "100", NULL);
    zstr_sendx (orange, "SET", "zyre/nodeid", "orange", NULL);
    
    //  JOIN CLUSTER message always replies with OK/SNAFU
    zstr_sendx (orange, "JOIN CLUSTER", NULL);
    reply = zstr_recv (orange);
    assert (streq (reply, "OK"));
    free (reply);
    
    //  Give time for cluster to interconnect
    zclock_sleep (250);

    byte buffer [100];
    ssize_t bytes;

    //  Test 1 - simple read-write
    zsys_info ("zpipes_test_cluster: open writer");
    zpipes_client_t *writer = zpipes_client_new ("orange", ">test pipe");

    zsys_info ("zpipes_test_cluster: open reader");
    zpipes_client_t *reader = zpipes_client_new ("green", "test pipe");

    //  Expect timeout error, EAGAIN
    zsys_info ("zpipes_test_cluster: read impatiently");
    bytes = zpipes_client_read (reader, buffer, 6, 200);
    assert (bytes == -1);
    assert (zpipes_client_error (reader) == EAGAIN);
    
    zsys_info ("zpipes_test_cluster: write to pipe");
    bytes = zpipes_client_write (writer, "Hello, World", 12, 0);
    assert (bytes == 12);
    
    zsys_info ("zpipes_test_cluster: read from pipe");
    bytes = zpipes_client_read (reader, buffer, 12, 0);
    assert (bytes == 12);
    
    zsys_info ("zpipes_test_cluster: write three chunks");
    bytes = zpipes_client_write (writer, "CHUNK1", 6, 200);
    assert (bytes == 6);
    bytes = zpipes_client_write (writer, "CHUNK2", 6, 200);
    assert (bytes == 6);
    bytes = zpipes_client_write (writer, "CHUNK3", 6, 200);
    assert (bytes == 6);

    zsys_info ("zpipes_test_cluster: read two chunks");
    bytes = zpipes_client_read (reader, buffer, 1, 200);
    assert (bytes == 1);
    bytes = zpipes_client_read (reader, buffer, 10, 200);
    assert (bytes == 10);
    
    zsys_info ("zpipes_test_cluster: close writer");
    zpipes_client_destroy (&writer);

    //  Expect end of pipe (short read)
    zsys_info ("zpipes_test_cluster: read short");
    bytes = zpipes_client_read (reader, buffer, 50, 200);
    assert (bytes == 7);

    //  Expect end of pipe (empty chunk)
    zsys_info ("zpipes_test_cluster: read end of pipe");
    bytes = zpipes_client_read (reader, buffer, 50, 200);
    assert (bytes == 0);

    //  Expect illegal action (EBADF) writing on reader
    zsys_info ("zpipes_test_cluster: try to write on reader");
    bytes = zpipes_client_write (reader, "CHUNK1", 6, 200);
    assert (bytes == -1);
    assert (zpipes_client_error (reader) == EBADF);

    zsys_info ("zpipes_test_cluster: close reader");
    zpipes_client_destroy (&reader);
    
    //  Test 2 - pipe reuse
    zsys_info ("zpipes_test_cluster: open reader");
    reader = zpipes_client_new ("green", "test pipe 2");

    zsys_info ("zpipes_test_cluster: open writer");
    writer = zpipes_client_new ("orange", ">test pipe 2");

    zsys_info ("zpipes_test_cluster: close reader");
    zpipes_client_destroy (&reader);

    zsys_info ("zpipes_test_cluster: close writer");
    zpipes_client_destroy (&writer);

    zsys_info ("zpipes_test_cluster: open reader reusing pipe name");
    reader = zpipes_client_new ("green", "test pipe 2");
    zpipes_client_destroy (&reader);

    zactor_destroy (&green);
    zactor_destroy (&orange);
    zsys_info ("zpipes_test_cluster: *** ending cluster tests ***");
    return 0;
}
