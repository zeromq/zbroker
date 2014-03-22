/*  =========================================================================
    zpipes_client.h - simple API for zpipes client applications

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.
    =========================================================================
*/

#ifndef __ZPIPES_CLIENT_H_INCLUDED__
#define __ZPIPES_CLIENT_H_INCLUDED__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _zpipes_client_t zpipes_client_t;

// @interface
//  Constructor; open ">pipename" for writing, "pipename" for reading
CZMQ_EXPORT zpipes_client_t *
    zpipes_client_new (const char *broker_name, const char *pipe_name);

//  Destructor; closes pipe
CZMQ_EXPORT void
    zpipes_client_destroy (zpipes_client_t **self_p);

//  Write chunk of data to pipe
CZMQ_EXPORT void
    zpipes_client_write (zpipes_client_t *self, void *data, size_t size);

//  Read chunk of data from pipe. If timeout is non zero, waits at most
//  that many msecs for data. Returns number of bytes read, or zero if
//  timeout expired, or if pipe was closed by the writer, and no more
//  data is available.
CZMQ_EXPORT size_t
    zpipes_client_read (zpipes_client_t *self, void *data, size_t max_size, int timeout);

// Self test of this class
CZMQ_EXPORT void
    zpipes_client_test (bool verbose);
// @end

#ifdef __cplusplus
}
#endif

#endif
