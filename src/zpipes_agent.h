/*  =========================================================================
    zpipes_agent - work with background zpipes agent

    -------------------------------------------------------------------------
    Copyright contributors as noted in the AUTHORS file.
    This file is part of zbroker, the ZeroMQ broker project.

    This is free software; you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License as published by the
    Free Software Foundation; either version 3 of the License, or (at your
    option) any later version.

    This software is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABIL-
    ITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General
    Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.
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
