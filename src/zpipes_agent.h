/*  =========================================================================
    zpipes_agent - work with background zpipes agent

    -------------------------------------------------------------------------
    Copyright contributors as noted in the AUTHORS file.
    This file is part of zbroker, the ZeroMQ broker project.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.
    =========================================================================
*/

#ifndef __ZPIPES_AGENT_H_INCLUDED__
#define __ZPIPES_AGENT_H_INCLUDED__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _zpipes_agent_t zpipes_agent_t;

//  Background engine
void
    zpipes_agent_main (void *args, zctx_t *ctx, void *pipe);

//  Self test of this class
void
    zpipes_agent_test (bool verbose);

#ifdef __cplusplus
}
#endif

#endif
