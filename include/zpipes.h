/*  =========================================================================
    zpipes - start/stop zpipes file sharing service

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.
    =========================================================================*/

#ifndef __ZPIPES_H_INCLUDED__
#define __ZPIPES_H_INCLUDED__

//  TODO: rename to zbroker.h
//  ZPIPES version macros for compile-time API detection

#define ZPIPES_VERSION_MAJOR 0
#define ZPIPES_VERSION_MINOR 0
#define ZPIPES_VERSION_PATCH 3

#define ZPIPES_MAKE_VERSION(major, minor, patch) \
    ((major) * 10000 + (minor) * 100 + (patch))
#define ZPIPES_VERSION \
    ZPIPES_MAKE_VERSION(ZPIPES_VERSION_MAJOR, ZPIPES_VERSION_MINOR, ZPIPES_VERSION_PATCH)

#include <czmq.h>
#if CZMQ_VERSION < 20200
#   error "zpipes needs CZMQ/2.2.0 or later"
#endif

#include <zyre.h>
#if ZYRE_VERSION < 10100
#   error "zpipes needs Zyre/1.1.0 or later"
#endif

//  Public API
#include "zpipes_msg.h"
#include "zpipes_server.h"
#include "zpipes_client.h"

#endif
