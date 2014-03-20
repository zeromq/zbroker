/*  =========================================================================
    zpipes_server - ZPIPES server

    ** WARNING *************************************************************
    THIS SOURCE FILE IS 100% GENERATED. If you edit this file, you will lose
    your changes at the next build cycle. This is great for temporary printf
    statements. DO NOT MAKE ANY CHANGES YOU WISH TO KEEP. The correct places
    for commits are:

    * The XML model used for this code generation: zpipes_server.xml
    * The code generation script that built this file: zproto_server_c
    ************************************************************************

    Copyright contributors as noted in the AUTHORS file.               
    This file is part of zbroker, the ZeroMQ broker project.           
                                                                       
    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.           
    =========================================================================
*/

#ifndef __ZPIPES_SERVER_H_INCLUDED__
#define __ZPIPES_SERVER_H_INCLUDED__

#ifdef __cplusplus
extern "C" {
#endif

//  Opaque class structure
typedef struct _zpipes_server_t zpipes_server_t;

//  @interface
//  Create a new zpipes_server
zpipes_server_t *
    zpipes_server_new (void);

//  Destroy the zpipes_server
void
    zpipes_server_destroy (zpipes_server_t **self_p);

//  Load server configuration data
void
    zpipes_server_configure (zpipes_server_t *self, const char *config_file);

//  Set one configuration path value
void
    zpipes_server_setoption (zpipes_server_t *self, const char *path, const char *value);

//  Binds the server to a specified endpoint
long
    zpipes_server_bind (zpipes_server_t *self, const char *endpoint);

//  Self test of this class
void
    zpipes_server_test (bool verbose);
//  @end

#ifdef __cplusplus
}
#endif

#endif
