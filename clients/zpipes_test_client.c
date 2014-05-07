/*  =========================================================================
    zpipes_test_client.c - stand-alone test case for thin client

    Copyright (c) the Contributors as noted in the AUTHORS file.
    This file is part of zbroker, the ZeroMQ broker project.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.
    =========================================================================
*/


#include <zmtp.h>
#include "zchunk.h"
#include "zpipes_msg.h"
#include "zpipes_client.h"

int main (void)
{
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
    return 0;
}
