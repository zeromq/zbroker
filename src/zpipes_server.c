/*  =========================================================================
    zpipes_server - ZPIPES server

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

/*
@header
    Description of class for man page.
@discuss
    Detailed discussion of the class, if any.
@end
*/

#include <czmq.h>
#include "../include/zpipes_msg.h"
#include "../include/zpipes_server.h"

//  ---------------------------------------------------------------------
//  This structure defines the context for each running server. Store
//  whatever properties and structures you need for the server.
//  

typedef struct {
    //  Add any properties you need here
    int filler;             //  Structure can't be empty (pedantic)
} server_t;

//  Allocate properties and structures for a new server instance.
//  Return 0 if OK, or -1 if there was an error.

static int
server_alloc (server_t *self)
{
    //  Construct properties here
    return 0;
}

//  Free properties and structures for a server instance

static void
server_free (server_t *self)
{
    //  Destroy properties here
}


//  ---------------------------------------------------------------------
//  This structure defines the state for each client connection. It will
//  be passed to each action in the 'self' argument.

typedef struct {
    //  These properties must always be present in the client_t
    server_t *server;           //  Reference to parent server
    zpipes_msg_t *request;      //  Last received request
    zpipes_msg_t *reply;        //  Reply to send out, if any
    //  These properties are specific for this application
} client_t;

//  Allocate properties and structures for a new client connection
//  Return 0 if OK, or -1 if there was an error.

static int
client_alloc (client_t *self)
{
    //  Construct properties here
    return 0;
}

//  Free properties and structures for a client connection

static void
client_free (client_t *self)
{
    //  Destroy properties here
}

//  Include the generated server engine
#include "zpipes_server_engine.h"


//  --------------------------------------------------------------------------
//  Selftest

void
zpipes_server_test (bool verbose)
{
    printf (" * zpipes_server: \n");

    //  @selftest
    zctx_t *ctx = zctx_new ();

    zpipes_server_t *self = zpipes_server_new ();
    assert (self);
    zpipes_server_bind (self, "tcp://127.0.0.1:5670");

    void *dealer = zsocket_new (ctx, ZMQ_DEALER);
    assert (dealer);
    zsocket_set_rcvtimeo (dealer, 2000);
    zsocket_connect (dealer, "tcp://127.0.0.1:5670");

    zpipes_msg_send_input (dealer, "testpipe");
    zpipes_msg_t *reply = zpipes_msg_recv (dealer);
    assert (reply);
    assert (zpipes_msg_id (reply) == ZPIPES_MSG_READY);
    zpipes_msg_destroy (&reply);
    zpipes_server_destroy (&self);
    
    zctx_destroy (&ctx);
    //  @end

    //  No clean way to wait for a background thread to exit
    //  Under valgrind this will randomly show as leakage
    //  Reduce this by giving server thread time to exit
    zclock_sleep (200);
    printf ("OK\n");
}
