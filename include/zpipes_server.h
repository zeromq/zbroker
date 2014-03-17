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
