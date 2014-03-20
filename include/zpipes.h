/*  =========================================================================
    zpipes - start/stop zpipes file sharing service

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.
    =========================================================================*/

#ifndef __ZPIPES_H_INCLUDED__
#define __ZPIPES_H_INCLUDED__

//  ZPIPES version macros for compile-time API detection

#define ZPIPES_VERSION_MAJOR 0
#define ZPIPES_VERSION_MINOR 0
#define ZPIPES_VERSION_PATCH 2

#define ZPIPES_MAKE_VERSION(major, minor, patch) \
    ((major) * 10000 + (minor) * 100 + (patch))
#define ZPIPES_VERSION \
    ZPIPES_MAKE_VERSION(ZPIPES_VERSION_MAJOR, ZPIPES_VERSION_MINOR, ZPIPES_VERSION_PATCH)

#include <czmq.h>
#if CZMQ_VERSION < 20100
#   error "zpipes needs CZMQ/2.1.0 or later"
#endif

#include <zyre.h>
#if ZYRE_VERSION < 10100
#   error "zpipes needs Zyre/1.1.0 or later"
#endif

//  The public API consists of the "zpipes_t" class plus other
//  external API classes

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _zpipes_t zpipes_t;

//  @interface
//  Constructor, creates a new zpipes broker
CZMQ_EXPORT zpipes_t *
    zpipes_new (const char *path);

//  Destructor, ends and destroys a zpipes broker
CZMQ_EXPORT void
    zpipes_destroy (zpipes_t **self_p);

//  Self test of this class
CZMQ_EXPORT void
    zpipes_test (bool verbose);
//  @end

#ifdef __cplusplus
}
#endif

#include "zpipes_msg.h"

#endif
