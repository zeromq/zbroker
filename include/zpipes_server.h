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

    Copyright (c) the Contributors as noted in the AUTHORS file.       
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

//  @interface
//  To work with zpipes_server, use the CZMQ zactor API:
//
//  Create new zpipes_server instance, passing logging prefix:
//      zactor_t *zpipes_server = zactor_new (zpipes_server, "myname");
//  
//  Destroy zpipes_server instance
//      zactor_destroy (&zpipes_server);
//  
//  Send zmsg instance message to zpipes_server instance:
//      zactor_send (zpipes_server, &msg);
//  
//  Send command to zpipes_server instance:
//      zstr_sendx (zpipes_server, "COMMAND", "ARG1", "ARG2", NULL);
//  
//  Receive zmsg message from zpipes_server instance:
//      zmsg_t *msg = zactor_recv (zpipes_server);
//
//  This is the zpipes_server constructor as a zactor_fn:
//
CZMQ_EXPORT void
    zpipes_server (zsock_t *pipe, void *args);

//  Self test of this class
CZMQ_EXPORT void
    zpipes_server_test (bool verbose);
//  @end

#ifdef __cplusplus
}
#endif

#endif
