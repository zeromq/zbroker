/*  =========================================================================
    zpipes_msg - ZPIPES protocol
    
    Codec header for zpipes_msg.

    ** WARNING *************************************************************
    THIS SOURCE FILE IS 100% GENERATED. If you edit this file, you will lose
    your changes at the next build cycle. This is great for temporary printf
    statements. DO NOT MAKE ANY CHANGES YOU WISH TO KEEP. The correct places
    for commits are:

    * The XML model used for this code generation: zpipes_msg.xml
    * The code generation script that built this file: zproto_codec_c
    ************************************************************************
    Copyright (c) the Contributors as noted in the AUTHORS file.       
    This file is part of zbroker, the ZeroMQ broker project.           
                                                                       
    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.           
    =========================================================================
*/

#ifndef __ZPIPES_MSG_H_INCLUDED__
#define __ZPIPES_MSG_H_INCLUDED__

/*  These are the zpipes_msg messages:

    INPUT - Create a new pipe for reading
        pipename            string      Name of pipe

    OUTPUT - Create a new pipe for writing
        pipename            string      Name of pipe

    READY - Input or output request was successful

    FAILED - Input or output request failed
        reason              string      Reason for failure

    FETCH - Read next chunk of data from pipe
        timeout             number 4    Timeout, msecs, or zero

    FETCHED - Have data from pipe
        chunk               chunk       Chunk of data

    END_OF_PIPE - Pipe is closed, no more data

    TIMEOUT - Get or put ended with timeout

    STORE - Write chunk of data to pipe
        chunk               chunk       Chunk of data

    STORED - Store was successful

    CLOSE - Close pipe

    CLOSED - Close was successful
*/


#define ZPIPES_MSG_INPUT                    1
#define ZPIPES_MSG_OUTPUT                   2
#define ZPIPES_MSG_READY                    3
#define ZPIPES_MSG_FAILED                   4
#define ZPIPES_MSG_FETCH                    5
#define ZPIPES_MSG_FETCHED                  6
#define ZPIPES_MSG_END_OF_PIPE              7
#define ZPIPES_MSG_TIMEOUT                  8
#define ZPIPES_MSG_STORE                    9
#define ZPIPES_MSG_STORED                   10
#define ZPIPES_MSG_CLOSE                    11
#define ZPIPES_MSG_CLOSED                   12

#ifdef __cplusplus
extern "C" {
#endif

//  Opaque class structure
typedef struct _zpipes_msg_t zpipes_msg_t;

//  @interface
//  Create a new zpipes_msg
zpipes_msg_t *
    zpipes_msg_new (int id);

//  Destroy the zpipes_msg
void
    zpipes_msg_destroy (zpipes_msg_t **self_p);

//  Parse a zpipes_msg from zmsg_t. Returns a new object, or NULL if
//  the message could not be parsed, or was NULL. If the socket type is
//  ZMQ_ROUTER, then parses the first frame as a routing_id. Destroys msg
//  and nullifies the msg refernce.
zpipes_msg_t *
    zpipes_msg_decode (zmsg_t **msg_p, int socket_type);

//  Encode zpipes_msg into zmsg and destroy it. Returns a newly created
//  object or NULL if error. Use when not in control of sending the message.
//  If the socket_type is ZMQ_ROUTER, then stores the routing_id as the
//  first frame of the resulting message.
zmsg_t *
    zpipes_msg_encode (zpipes_msg_t *self, int socket_type);

//  Receive and parse a zpipes_msg from the socket. Returns new object, 
//  or NULL if error. Will block if there's no message waiting.
zpipes_msg_t *
    zpipes_msg_recv (void *input);

//  Receive and parse a zpipes_msg from the socket. Returns new object, 
//  or NULL either if there was no input waiting, or the recv was interrupted.
zpipes_msg_t *
    zpipes_msg_recv_nowait (void *input);

//  Send the zpipes_msg to the output, and destroy it
int
    zpipes_msg_send (zpipes_msg_t **self_p, void *output);

//  Send the zpipes_msg to the output, and do not destroy it
int
    zpipes_msg_send_again (zpipes_msg_t *self, void *output);

//  Send the INPUT to the output in one step
int
    zpipes_msg_send_input (void *output,
        const char *pipename);
    
//  Send the OUTPUT to the output in one step
int
    zpipes_msg_send_output (void *output,
        const char *pipename);
    
//  Send the READY to the output in one step
int
    zpipes_msg_send_ready (void *output);
    
//  Send the FAILED to the output in one step
int
    zpipes_msg_send_failed (void *output,
        const char *reason);
    
//  Send the FETCH to the output in one step
int
    zpipes_msg_send_fetch (void *output,
        uint32_t timeout);
    
//  Send the FETCHED to the output in one step
int
    zpipes_msg_send_fetched (void *output,
        zchunk_t *chunk);
    
//  Send the END_OF_PIPE to the output in one step
int
    zpipes_msg_send_end_of_pipe (void *output);
    
//  Send the TIMEOUT to the output in one step
int
    zpipes_msg_send_timeout (void *output);
    
//  Send the STORE to the output in one step
int
    zpipes_msg_send_store (void *output,
        zchunk_t *chunk);
    
//  Send the STORED to the output in one step
int
    zpipes_msg_send_stored (void *output);
    
//  Send the CLOSE to the output in one step
int
    zpipes_msg_send_close (void *output);
    
//  Send the CLOSED to the output in one step
int
    zpipes_msg_send_closed (void *output);
    
//  Duplicate the zpipes_msg message
zpipes_msg_t *
    zpipes_msg_dup (zpipes_msg_t *self);

//  Print contents of message to stdout
void
    zpipes_msg_dump (zpipes_msg_t *self);

//  Get/set the message routing id
zframe_t *
    zpipes_msg_routing_id (zpipes_msg_t *self);
void
    zpipes_msg_set_routing_id (zpipes_msg_t *self, zframe_t *routing_id);

//  Get the zpipes_msg id and printable command
int
    zpipes_msg_id (zpipes_msg_t *self);
void
    zpipes_msg_set_id (zpipes_msg_t *self, int id);
const char *
    zpipes_msg_command (zpipes_msg_t *self);

//  Get/set the pipename field
const char *
    zpipes_msg_pipename (zpipes_msg_t *self);
void
    zpipes_msg_set_pipename (zpipes_msg_t *self, const char *format, ...);

//  Get/set the reason field
const char *
    zpipes_msg_reason (zpipes_msg_t *self);
void
    zpipes_msg_set_reason (zpipes_msg_t *self, const char *format, ...);

//  Get/set the timeout field
uint32_t
    zpipes_msg_timeout (zpipes_msg_t *self);
void
    zpipes_msg_set_timeout (zpipes_msg_t *self, uint32_t timeout);

//  Get a copy of the chunk field
zchunk_t *
    zpipes_msg_chunk (zpipes_msg_t *self);
//  Get the chunk field and transfer ownership to caller
zchunk_t *
    zpipes_msg_get_chunk (zpipes_msg_t *self);
//  Set the chunk field, transferring ownership from caller
void
    zpipes_msg_set_chunk (zpipes_msg_t *self, zchunk_t **chunk_p);

//  Self test of this class
int
    zpipes_msg_test (bool verbose);
//  @end

#ifdef __cplusplus
}
#endif

#endif
