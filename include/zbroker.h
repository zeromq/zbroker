/*  =========================================================================
    zbroker - ZeroMQ broker project public API

    Copyright (c) the Contributors as noted in the AUTHORS file.
    This file is part of zbroker, the ZeroMQ broker project.
    
    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.
    =========================================================================*/

#ifndef __ZBROKER_H_INCLUDED__
#define __ZBROKER_H_INCLUDED__

//  ZBROKER version macros for compile-time API detection

#define ZBROKER_VERSION_MAJOR 0
#define ZBROKER_VERSION_MINOR 0
#define ZBROKER_VERSION_PATCH 3

#define ZBROKER_MAKE_VERSION(major, minor, patch) \
    ((major) * 10000 + (minor) * 100 + (patch))
#define ZBROKER_VERSION \
    ZBROKER_MAKE_VERSION(ZBROKER_VERSION_MAJOR, ZBROKER_VERSION_MINOR, ZBROKER_VERSION_PATCH)

#include <czmq.h>
#if CZMQ_VERSION < 20200
#   error "zbroker needs CZMQ/2.2.0 or later"
#endif

#include <zyre.h>
#if ZYRE_VERSION < 10100
#   error "zbroker needs Zyre/1.1.0 or later"
#endif

//  Public API
#include "zpipes_msg.h"
#include "zpipes_server.h"
#include "zpipes_client.h"

#endif
