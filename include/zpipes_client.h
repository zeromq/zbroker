/*  =========================================================================
    zpipes_client.h - simple API for zpipes client applications

    Copyright (c) tbd
    =========================================================================
*/

#ifndef __ZPIPES_CLIENT_H_INCLUDED__
#define __ZPIPES_CLIENT_H_INCLUDED__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _zpipes_client_t zpipes_client_t;

// @interface
//  Constructor
CZMQ_EXPORT zpipes_client_t *
    zpipes_client_new (char *broker_name, char *pipe_name);

//  Destructor
CZMQ_EXPORT void
    zpipes_client_destroy (zpipes_client_t **self_p);

//  Write chunk of data to pipe
CZMQ_EXPORT void
    zpipes_client_write (zpipes_client_t *self, void *data, size_t size);

//  Read chunk of data from pipe, blocks until data arrives
//  Returns size of chunk read; if less than max_size, truncates
CZMQ_EXPORT size_t
    zpipes_client_read (zpipes_client_t *self, void *data, size_t max_size);

// Self test of this class
CZMQ_EXPORT void
    zpipes_client_test (bool verbose);
// @end

#ifdef __cplusplus
}
#endif

#endif
