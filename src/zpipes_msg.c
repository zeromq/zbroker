/*  =========================================================================
    zpipes_msg - ZPIPES protocol

    Codec class for zpipes_msg.

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

/*
@header
    zpipes_msg - ZPIPES protocol
@discuss
@end
*/

#include "../include/zbroker.h"
#include "../include/zpipes_msg.h"

//  Structure of our class

struct _zpipes_msg_t {
    zframe_t *routing_id;               //  Routing_id from ROUTER, if any
    int id;                             //  zpipes_msg message ID
    byte *needle;                       //  Read/write pointer for serialization
    byte *ceiling;                      //  Valid upper limit for read pointer
    char *pipename;                     //  Name of pipe
    char *reason;                       //  Reason for failure
    uint32_t size;                      //  Number of bytes to read
    uint32_t timeout;                   //  Timeout, msecs, or zero
    zchunk_t *chunk;                    //  Chunk of data
};

//  --------------------------------------------------------------------------
//  Network data encoding macros

//  Put a block of octets to the frame
#define PUT_OCTETS(host,size) { \
    memcpy (self->needle, (host), size); \
    self->needle += size; \
}

//  Get a block of octets from the frame
#define GET_OCTETS(host,size) { \
    if (self->needle + size > self->ceiling) \
        goto malformed; \
    memcpy ((host), self->needle, size); \
    self->needle += size; \
}

//  Put a 1-byte number to the frame
#define PUT_NUMBER1(host) { \
    *(byte *) self->needle = (host); \
    self->needle++; \
}

//  Put a 2-byte number to the frame
#define PUT_NUMBER2(host) { \
    self->needle [0] = (byte) (((host) >> 8)  & 255); \
    self->needle [1] = (byte) (((host))       & 255); \
    self->needle += 2; \
}

//  Put a 4-byte number to the frame
#define PUT_NUMBER4(host) { \
    self->needle [0] = (byte) (((host) >> 24) & 255); \
    self->needle [1] = (byte) (((host) >> 16) & 255); \
    self->needle [2] = (byte) (((host) >> 8)  & 255); \
    self->needle [3] = (byte) (((host))       & 255); \
    self->needle += 4; \
}

//  Put a 8-byte number to the frame
#define PUT_NUMBER8(host) { \
    self->needle [0] = (byte) (((host) >> 56) & 255); \
    self->needle [1] = (byte) (((host) >> 48) & 255); \
    self->needle [2] = (byte) (((host) >> 40) & 255); \
    self->needle [3] = (byte) (((host) >> 32) & 255); \
    self->needle [4] = (byte) (((host) >> 24) & 255); \
    self->needle [5] = (byte) (((host) >> 16) & 255); \
    self->needle [6] = (byte) (((host) >> 8)  & 255); \
    self->needle [7] = (byte) (((host))       & 255); \
    self->needle += 8; \
}

//  Get a 1-byte number from the frame
#define GET_NUMBER1(host) { \
    if (self->needle + 1 > self->ceiling) \
        goto malformed; \
    (host) = *(byte *) self->needle; \
    self->needle++; \
}

//  Get a 2-byte number from the frame
#define GET_NUMBER2(host) { \
    if (self->needle + 2 > self->ceiling) \
        goto malformed; \
    (host) = ((uint16_t) (self->needle [0]) << 8) \
           +  (uint16_t) (self->needle [1]); \
    self->needle += 2; \
}

//  Get a 4-byte number from the frame
#define GET_NUMBER4(host) { \
    if (self->needle + 4 > self->ceiling) \
        goto malformed; \
    (host) = ((uint32_t) (self->needle [0]) << 24) \
           + ((uint32_t) (self->needle [1]) << 16) \
           + ((uint32_t) (self->needle [2]) << 8) \
           +  (uint32_t) (self->needle [3]); \
    self->needle += 4; \
}

//  Get a 8-byte number from the frame
#define GET_NUMBER8(host) { \
    if (self->needle + 8 > self->ceiling) \
        goto malformed; \
    (host) = ((uint64_t) (self->needle [0]) << 56) \
           + ((uint64_t) (self->needle [1]) << 48) \
           + ((uint64_t) (self->needle [2]) << 40) \
           + ((uint64_t) (self->needle [3]) << 32) \
           + ((uint64_t) (self->needle [4]) << 24) \
           + ((uint64_t) (self->needle [5]) << 16) \
           + ((uint64_t) (self->needle [6]) << 8) \
           +  (uint64_t) (self->needle [7]); \
    self->needle += 8; \
}

//  Put a string to the frame
#define PUT_STRING(host) { \
    size_t string_size = strlen (host); \
    PUT_NUMBER1 (string_size); \
    memcpy (self->needle, (host), string_size); \
    self->needle += string_size; \
}

//  Get a string from the frame
#define GET_STRING(host) { \
    size_t string_size; \
    GET_NUMBER1 (string_size); \
    if (self->needle + string_size > (self->ceiling)) \
        goto malformed; \
    (host) = (char *) malloc (string_size + 1); \
    memcpy ((host), self->needle, string_size); \
    (host) [string_size] = 0; \
    self->needle += string_size; \
}

//  Put a long string to the frame
#define PUT_LONGSTR(host) { \
    size_t string_size = strlen (host); \
    PUT_NUMBER4 (string_size); \
    memcpy (self->needle, (host), string_size); \
    self->needle += string_size; \
}

//  Get a long string from the frame
#define GET_LONGSTR(host) { \
    size_t string_size; \
    GET_NUMBER4 (string_size); \
    if (self->needle + string_size > (self->ceiling)) \
        goto malformed; \
    (host) = (char *) malloc (string_size + 1); \
    memcpy ((host), self->needle, string_size); \
    (host) [string_size] = 0; \
    self->needle += string_size; \
}


//  --------------------------------------------------------------------------
//  Create a new zpipes_msg

zpipes_msg_t *
zpipes_msg_new (int id)
{
    zpipes_msg_t *self = (zpipes_msg_t *) zmalloc (sizeof (zpipes_msg_t));
    self->id = id;
    return self;
}


//  --------------------------------------------------------------------------
//  Destroy the zpipes_msg

void
zpipes_msg_destroy (zpipes_msg_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        zpipes_msg_t *self = *self_p;

        //  Free class properties
        zframe_destroy (&self->routing_id);
        free (self->pipename);
        free (self->reason);
        zchunk_destroy (&self->chunk);

        //  Free object itself
        free (self);
        *self_p = NULL;
    }
}


//  --------------------------------------------------------------------------
//  Parse a zpipes_msg from zmsg_t. Returns a new object, or NULL if
//  the message could not be parsed, or was NULL. Destroys msg and 
//  nullifies the msg reference.

zpipes_msg_t *
zpipes_msg_decode (zmsg_t **msg_p)
{
    assert (msg_p);
    zmsg_t *msg = *msg_p;
    if (msg == NULL)
        return NULL;
        
    zpipes_msg_t *self = zpipes_msg_new (0);
    //  Read and parse command in frame
    zframe_t *frame = zmsg_pop (msg);
    if (!frame) 
        goto empty;             //  Malformed or empty

    //  Get and check protocol signature
    self->needle = zframe_data (frame);
    self->ceiling = self->needle + zframe_size (frame);
    uint16_t signature;
    GET_NUMBER2 (signature);
    if (signature != (0xAAA0 | 0))
        goto empty;             //  Invalid signature

    //  Get message id and parse per message type
    GET_NUMBER1 (self->id);

    switch (self->id) {
        case ZPIPES_MSG_INPUT:
            GET_STRING (self->pipename);
            break;

        case ZPIPES_MSG_INPUT_OK:
            break;

        case ZPIPES_MSG_INPUT_FAILED:
            GET_STRING (self->reason);
            break;

        case ZPIPES_MSG_OUTPUT:
            GET_STRING (self->pipename);
            break;

        case ZPIPES_MSG_OUTPUT_OK:
            break;

        case ZPIPES_MSG_OUTPUT_FAILED:
            GET_STRING (self->reason);
            break;

        case ZPIPES_MSG_READ:
            GET_NUMBER4 (self->size);
            GET_NUMBER4 (self->timeout);
            break;

        case ZPIPES_MSG_READ_OK:
            {
                size_t chunk_size;
                GET_NUMBER4 (chunk_size);
                if (self->needle + chunk_size > (self->ceiling))
                    goto malformed;
                self->chunk = zchunk_new (self->needle, chunk_size);
                self->needle += chunk_size;
            }
            break;

        case ZPIPES_MSG_READ_END:
            break;

        case ZPIPES_MSG_READ_TIMEOUT:
            break;

        case ZPIPES_MSG_READ_FAILED:
            GET_STRING (self->reason);
            break;

        case ZPIPES_MSG_WRITE:
            {
                size_t chunk_size;
                GET_NUMBER4 (chunk_size);
                if (self->needle + chunk_size > (self->ceiling))
                    goto malformed;
                self->chunk = zchunk_new (self->needle, chunk_size);
                self->needle += chunk_size;
            }
            GET_NUMBER4 (self->timeout);
            break;

        case ZPIPES_MSG_WRITE_OK:
            break;

        case ZPIPES_MSG_WRITE_TIMEOUT:
            break;

        case ZPIPES_MSG_WRITE_FAILED:
            GET_STRING (self->reason);
            break;

        case ZPIPES_MSG_CLOSE:
            break;

        case ZPIPES_MSG_CLOSE_OK:
            break;

        case ZPIPES_MSG_CLOSE_FAILED:
            GET_STRING (self->reason);
            break;

        case ZPIPES_MSG_PING:
            break;

        case ZPIPES_MSG_PING_OK:
            break;

        case ZPIPES_MSG_INVALID:
            break;

        default:
            goto malformed;
    }
    //  Successful return
    zframe_destroy (&frame);
    zmsg_destroy (msg_p);
    return self;

    //  Error returns
    malformed:
        zsys_error ("malformed message '%d'\n", self->id);
    empty:
        zframe_destroy (&frame);
        zmsg_destroy (msg_p);
        zpipes_msg_destroy (&self);
        return (NULL);
}


//  --------------------------------------------------------------------------
//  Encode zpipes_msg into zmsg and destroy it. Returns a newly created
//  object or NULL if error. Use when not in control of sending the message.

zmsg_t *
zpipes_msg_encode (zpipes_msg_t **self_p)
{
    assert (self_p);
    assert (*self_p);
    
    zpipes_msg_t *self = *self_p;
    zmsg_t *msg = zmsg_new ();

    size_t frame_size = 2 + 1;          //  Signature and message ID
    switch (self->id) {
        case ZPIPES_MSG_INPUT:
            //  pipename is a string with 1-byte length
            frame_size++;       //  Size is one octet
            if (self->pipename)
                frame_size += strlen (self->pipename);
            break;
            
        case ZPIPES_MSG_INPUT_OK:
            break;
            
        case ZPIPES_MSG_INPUT_FAILED:
            //  reason is a string with 1-byte length
            frame_size++;       //  Size is one octet
            if (self->reason)
                frame_size += strlen (self->reason);
            break;
            
        case ZPIPES_MSG_OUTPUT:
            //  pipename is a string with 1-byte length
            frame_size++;       //  Size is one octet
            if (self->pipename)
                frame_size += strlen (self->pipename);
            break;
            
        case ZPIPES_MSG_OUTPUT_OK:
            break;
            
        case ZPIPES_MSG_OUTPUT_FAILED:
            //  reason is a string with 1-byte length
            frame_size++;       //  Size is one octet
            if (self->reason)
                frame_size += strlen (self->reason);
            break;
            
        case ZPIPES_MSG_READ:
            //  size is a 4-byte integer
            frame_size += 4;
            //  timeout is a 4-byte integer
            frame_size += 4;
            break;
            
        case ZPIPES_MSG_READ_OK:
            //  chunk is a chunk with 4-byte length
            frame_size += 4;
            if (self->chunk)
                frame_size += zchunk_size (self->chunk);
            break;
            
        case ZPIPES_MSG_READ_END:
            break;
            
        case ZPIPES_MSG_READ_TIMEOUT:
            break;
            
        case ZPIPES_MSG_READ_FAILED:
            //  reason is a string with 1-byte length
            frame_size++;       //  Size is one octet
            if (self->reason)
                frame_size += strlen (self->reason);
            break;
            
        case ZPIPES_MSG_WRITE:
            //  chunk is a chunk with 4-byte length
            frame_size += 4;
            if (self->chunk)
                frame_size += zchunk_size (self->chunk);
            //  timeout is a 4-byte integer
            frame_size += 4;
            break;
            
        case ZPIPES_MSG_WRITE_OK:
            break;
            
        case ZPIPES_MSG_WRITE_TIMEOUT:
            break;
            
        case ZPIPES_MSG_WRITE_FAILED:
            //  reason is a string with 1-byte length
            frame_size++;       //  Size is one octet
            if (self->reason)
                frame_size += strlen (self->reason);
            break;
            
        case ZPIPES_MSG_CLOSE:
            break;
            
        case ZPIPES_MSG_CLOSE_OK:
            break;
            
        case ZPIPES_MSG_CLOSE_FAILED:
            //  reason is a string with 1-byte length
            frame_size++;       //  Size is one octet
            if (self->reason)
                frame_size += strlen (self->reason);
            break;
            
        case ZPIPES_MSG_PING:
            break;
            
        case ZPIPES_MSG_PING_OK:
            break;
            
        case ZPIPES_MSG_INVALID:
            break;
            
        default:
            zsys_error ("bad message type '%d', not sent\n", self->id);
            //  No recovery, this is a fatal application error
            assert (false);
    }
    //  Now serialize message into the frame
    zframe_t *frame = zframe_new (NULL, frame_size);
    self->needle = zframe_data (frame);
    PUT_NUMBER2 (0xAAA0 | 0);
    PUT_NUMBER1 (self->id);

    switch (self->id) {
        case ZPIPES_MSG_INPUT:
            if (self->pipename) {
                PUT_STRING (self->pipename);
            }
            else
                PUT_NUMBER1 (0);    //  Empty string
            break;

        case ZPIPES_MSG_INPUT_OK:
            break;

        case ZPIPES_MSG_INPUT_FAILED:
            if (self->reason) {
                PUT_STRING (self->reason);
            }
            else
                PUT_NUMBER1 (0);    //  Empty string
            break;

        case ZPIPES_MSG_OUTPUT:
            if (self->pipename) {
                PUT_STRING (self->pipename);
            }
            else
                PUT_NUMBER1 (0);    //  Empty string
            break;

        case ZPIPES_MSG_OUTPUT_OK:
            break;

        case ZPIPES_MSG_OUTPUT_FAILED:
            if (self->reason) {
                PUT_STRING (self->reason);
            }
            else
                PUT_NUMBER1 (0);    //  Empty string
            break;

        case ZPIPES_MSG_READ:
            PUT_NUMBER4 (self->size);
            PUT_NUMBER4 (self->timeout);
            break;

        case ZPIPES_MSG_READ_OK:
            if (self->chunk) {
                PUT_NUMBER4 (zchunk_size (self->chunk));
                memcpy (self->needle,
                        zchunk_data (self->chunk),
                        zchunk_size (self->chunk));
                self->needle += zchunk_size (self->chunk);
            }
            else
                PUT_NUMBER4 (0);    //  Empty chunk
            break;

        case ZPIPES_MSG_READ_END:
            break;

        case ZPIPES_MSG_READ_TIMEOUT:
            break;

        case ZPIPES_MSG_READ_FAILED:
            if (self->reason) {
                PUT_STRING (self->reason);
            }
            else
                PUT_NUMBER1 (0);    //  Empty string
            break;

        case ZPIPES_MSG_WRITE:
            if (self->chunk) {
                PUT_NUMBER4 (zchunk_size (self->chunk));
                memcpy (self->needle,
                        zchunk_data (self->chunk),
                        zchunk_size (self->chunk));
                self->needle += zchunk_size (self->chunk);
            }
            else
                PUT_NUMBER4 (0);    //  Empty chunk
            PUT_NUMBER4 (self->timeout);
            break;

        case ZPIPES_MSG_WRITE_OK:
            break;

        case ZPIPES_MSG_WRITE_TIMEOUT:
            break;

        case ZPIPES_MSG_WRITE_FAILED:
            if (self->reason) {
                PUT_STRING (self->reason);
            }
            else
                PUT_NUMBER1 (0);    //  Empty string
            break;

        case ZPIPES_MSG_CLOSE:
            break;

        case ZPIPES_MSG_CLOSE_OK:
            break;

        case ZPIPES_MSG_CLOSE_FAILED:
            if (self->reason) {
                PUT_STRING (self->reason);
            }
            else
                PUT_NUMBER1 (0);    //  Empty string
            break;

        case ZPIPES_MSG_PING:
            break;

        case ZPIPES_MSG_PING_OK:
            break;

        case ZPIPES_MSG_INVALID:
            break;

    }
    //  Now send the data frame
    if (zmsg_append (msg, &frame)) {
        zmsg_destroy (&msg);
        zpipes_msg_destroy (self_p);
        return NULL;
    }
    //  Destroy zpipes_msg object
    zpipes_msg_destroy (self_p);
    return msg;
}


//  --------------------------------------------------------------------------
//  Receive and parse a zpipes_msg from the socket. Returns new object or
//  NULL if error. Will block if there's no message waiting.

zpipes_msg_t *
zpipes_msg_recv (void *input)
{
    assert (input);
    zmsg_t *msg = zmsg_recv (input);
    //  If message came from a router socket, first frame is routing_id
    zframe_t *routing_id = NULL;
    if (zsocket_type (zsock_resolve (input)) == ZMQ_ROUTER) {
        routing_id = zmsg_pop (msg);
        //  If message was not valid, forget about it
        if (!routing_id || !zmsg_next (msg))
            return NULL;        //  Malformed or empty
    }
    zpipes_msg_t *zpipes_msg = zpipes_msg_decode (&msg);
    if (zpipes_msg && zsocket_type (zsock_resolve (input)) == ZMQ_ROUTER)
        zpipes_msg->routing_id = routing_id;

    return zpipes_msg;
}


//  --------------------------------------------------------------------------
//  Receive and parse a zpipes_msg from the socket. Returns new object,
//  or NULL either if there was no input waiting, or the recv was interrupted.

zpipes_msg_t *
zpipes_msg_recv_nowait (void *input)
{
    assert (input);
    zmsg_t *msg = zmsg_recv_nowait (input);
    //  If message came from a router socket, first frame is routing_id
    zframe_t *routing_id = NULL;
    if (zsocket_type (zsock_resolve (input)) == ZMQ_ROUTER) {
        routing_id = zmsg_pop (msg);
        //  If message was not valid, forget about it
        if (!routing_id || !zmsg_next (msg))
            return NULL;        //  Malformed or empty
    }
    zpipes_msg_t *zpipes_msg = zpipes_msg_decode (&msg);
    if (zpipes_msg && zsocket_type (zsock_resolve (input)) == ZMQ_ROUTER)
        zpipes_msg->routing_id = routing_id;

    return zpipes_msg;
}


//  --------------------------------------------------------------------------
//  Send the zpipes_msg to the socket, and destroy it
//  Returns 0 if OK, else -1

int
zpipes_msg_send (zpipes_msg_t **self_p, void *output)
{
    assert (self_p);
    assert (*self_p);
    assert (output);

    //  Save routing_id if any, as encode will destroy it
    zpipes_msg_t *self = *self_p;
    zframe_t *routing_id = self->routing_id;
    self->routing_id = NULL;

    //  Encode zpipes_msg message to a single zmsg
    zmsg_t *msg = zpipes_msg_encode (&self);
    
    //  If we're sending to a ROUTER, send the routing_id first
    if (zsocket_type (zsock_resolve (output)) == ZMQ_ROUTER) {
        assert (routing_id);
        zmsg_prepend (msg, &routing_id);
    }
    else
        zframe_destroy (&routing_id);
        
    if (msg && zmsg_send (&msg, output) == 0)
        return 0;
    else
        return -1;              //  Failed to encode, or send
}


//  --------------------------------------------------------------------------
//  Send the zpipes_msg to the output, and do not destroy it

int
zpipes_msg_send_again (zpipes_msg_t *self, void *output)
{
    assert (self);
    assert (output);
    self = zpipes_msg_dup (self);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Encode INPUT message

zmsg_t * 
zpipes_msg_encode_input (
    const char *pipename)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_INPUT);
    zpipes_msg_set_pipename (self, pipename);
    return zpipes_msg_encode (&self);
}


//  --------------------------------------------------------------------------
//  Encode INPUT_OK message

zmsg_t * 
zpipes_msg_encode_input_ok (
)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_INPUT_OK);
    return zpipes_msg_encode (&self);
}


//  --------------------------------------------------------------------------
//  Encode INPUT_FAILED message

zmsg_t * 
zpipes_msg_encode_input_failed (
    const char *reason)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_INPUT_FAILED);
    zpipes_msg_set_reason (self, reason);
    return zpipes_msg_encode (&self);
}


//  --------------------------------------------------------------------------
//  Encode OUTPUT message

zmsg_t * 
zpipes_msg_encode_output (
    const char *pipename)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_OUTPUT);
    zpipes_msg_set_pipename (self, pipename);
    return zpipes_msg_encode (&self);
}


//  --------------------------------------------------------------------------
//  Encode OUTPUT_OK message

zmsg_t * 
zpipes_msg_encode_output_ok (
)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_OUTPUT_OK);
    return zpipes_msg_encode (&self);
}


//  --------------------------------------------------------------------------
//  Encode OUTPUT_FAILED message

zmsg_t * 
zpipes_msg_encode_output_failed (
    const char *reason)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_OUTPUT_FAILED);
    zpipes_msg_set_reason (self, reason);
    return zpipes_msg_encode (&self);
}


//  --------------------------------------------------------------------------
//  Encode READ message

zmsg_t * 
zpipes_msg_encode_read (
    uint32_t size,
    uint32_t timeout)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_READ);
    zpipes_msg_set_size (self, size);
    zpipes_msg_set_timeout (self, timeout);
    return zpipes_msg_encode (&self);
}


//  --------------------------------------------------------------------------
//  Encode READ_OK message

zmsg_t * 
zpipes_msg_encode_read_ok (
    zchunk_t *chunk)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_READ_OK);
    zchunk_t *chunk_copy = zchunk_dup (chunk);
    zpipes_msg_set_chunk (self, &chunk_copy);
    return zpipes_msg_encode (&self);
}


//  --------------------------------------------------------------------------
//  Encode READ_END message

zmsg_t * 
zpipes_msg_encode_read_end (
)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_READ_END);
    return zpipes_msg_encode (&self);
}


//  --------------------------------------------------------------------------
//  Encode READ_TIMEOUT message

zmsg_t * 
zpipes_msg_encode_read_timeout (
)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_READ_TIMEOUT);
    return zpipes_msg_encode (&self);
}


//  --------------------------------------------------------------------------
//  Encode READ_FAILED message

zmsg_t * 
zpipes_msg_encode_read_failed (
    const char *reason)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_READ_FAILED);
    zpipes_msg_set_reason (self, reason);
    return zpipes_msg_encode (&self);
}


//  --------------------------------------------------------------------------
//  Encode WRITE message

zmsg_t * 
zpipes_msg_encode_write (
    zchunk_t *chunk,
    uint32_t timeout)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_WRITE);
    zchunk_t *chunk_copy = zchunk_dup (chunk);
    zpipes_msg_set_chunk (self, &chunk_copy);
    zpipes_msg_set_timeout (self, timeout);
    return zpipes_msg_encode (&self);
}


//  --------------------------------------------------------------------------
//  Encode WRITE_OK message

zmsg_t * 
zpipes_msg_encode_write_ok (
)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_WRITE_OK);
    return zpipes_msg_encode (&self);
}


//  --------------------------------------------------------------------------
//  Encode WRITE_TIMEOUT message

zmsg_t * 
zpipes_msg_encode_write_timeout (
)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_WRITE_TIMEOUT);
    return zpipes_msg_encode (&self);
}


//  --------------------------------------------------------------------------
//  Encode WRITE_FAILED message

zmsg_t * 
zpipes_msg_encode_write_failed (
    const char *reason)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_WRITE_FAILED);
    zpipes_msg_set_reason (self, reason);
    return zpipes_msg_encode (&self);
}


//  --------------------------------------------------------------------------
//  Encode CLOSE message

zmsg_t * 
zpipes_msg_encode_close (
)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_CLOSE);
    return zpipes_msg_encode (&self);
}


//  --------------------------------------------------------------------------
//  Encode CLOSE_OK message

zmsg_t * 
zpipes_msg_encode_close_ok (
)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_CLOSE_OK);
    return zpipes_msg_encode (&self);
}


//  --------------------------------------------------------------------------
//  Encode CLOSE_FAILED message

zmsg_t * 
zpipes_msg_encode_close_failed (
    const char *reason)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_CLOSE_FAILED);
    zpipes_msg_set_reason (self, reason);
    return zpipes_msg_encode (&self);
}


//  --------------------------------------------------------------------------
//  Encode PING message

zmsg_t * 
zpipes_msg_encode_ping (
)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_PING);
    return zpipes_msg_encode (&self);
}


//  --------------------------------------------------------------------------
//  Encode PING_OK message

zmsg_t * 
zpipes_msg_encode_ping_ok (
)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_PING_OK);
    return zpipes_msg_encode (&self);
}


//  --------------------------------------------------------------------------
//  Encode INVALID message

zmsg_t * 
zpipes_msg_encode_invalid (
)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_INVALID);
    return zpipes_msg_encode (&self);
}


//  --------------------------------------------------------------------------
//  Send the INPUT to the socket in one step

int
zpipes_msg_send_input (
    void *output,
    const char *pipename)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_INPUT);
    zpipes_msg_set_pipename (self, pipename);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the INPUT_OK to the socket in one step

int
zpipes_msg_send_input_ok (
    void *output)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_INPUT_OK);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the INPUT_FAILED to the socket in one step

int
zpipes_msg_send_input_failed (
    void *output,
    const char *reason)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_INPUT_FAILED);
    zpipes_msg_set_reason (self, reason);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the OUTPUT to the socket in one step

int
zpipes_msg_send_output (
    void *output,
    const char *pipename)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_OUTPUT);
    zpipes_msg_set_pipename (self, pipename);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the OUTPUT_OK to the socket in one step

int
zpipes_msg_send_output_ok (
    void *output)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_OUTPUT_OK);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the OUTPUT_FAILED to the socket in one step

int
zpipes_msg_send_output_failed (
    void *output,
    const char *reason)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_OUTPUT_FAILED);
    zpipes_msg_set_reason (self, reason);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the READ to the socket in one step

int
zpipes_msg_send_read (
    void *output,
    uint32_t size,
    uint32_t timeout)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_READ);
    zpipes_msg_set_size (self, size);
    zpipes_msg_set_timeout (self, timeout);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the READ_OK to the socket in one step

int
zpipes_msg_send_read_ok (
    void *output,
    zchunk_t *chunk)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_READ_OK);
    zchunk_t *chunk_copy = zchunk_dup (chunk);
    zpipes_msg_set_chunk (self, &chunk_copy);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the READ_END to the socket in one step

int
zpipes_msg_send_read_end (
    void *output)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_READ_END);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the READ_TIMEOUT to the socket in one step

int
zpipes_msg_send_read_timeout (
    void *output)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_READ_TIMEOUT);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the READ_FAILED to the socket in one step

int
zpipes_msg_send_read_failed (
    void *output,
    const char *reason)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_READ_FAILED);
    zpipes_msg_set_reason (self, reason);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the WRITE to the socket in one step

int
zpipes_msg_send_write (
    void *output,
    zchunk_t *chunk,
    uint32_t timeout)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_WRITE);
    zchunk_t *chunk_copy = zchunk_dup (chunk);
    zpipes_msg_set_chunk (self, &chunk_copy);
    zpipes_msg_set_timeout (self, timeout);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the WRITE_OK to the socket in one step

int
zpipes_msg_send_write_ok (
    void *output)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_WRITE_OK);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the WRITE_TIMEOUT to the socket in one step

int
zpipes_msg_send_write_timeout (
    void *output)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_WRITE_TIMEOUT);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the WRITE_FAILED to the socket in one step

int
zpipes_msg_send_write_failed (
    void *output,
    const char *reason)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_WRITE_FAILED);
    zpipes_msg_set_reason (self, reason);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the CLOSE to the socket in one step

int
zpipes_msg_send_close (
    void *output)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_CLOSE);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the CLOSE_OK to the socket in one step

int
zpipes_msg_send_close_ok (
    void *output)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_CLOSE_OK);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the CLOSE_FAILED to the socket in one step

int
zpipes_msg_send_close_failed (
    void *output,
    const char *reason)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_CLOSE_FAILED);
    zpipes_msg_set_reason (self, reason);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the PING to the socket in one step

int
zpipes_msg_send_ping (
    void *output)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_PING);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the PING_OK to the socket in one step

int
zpipes_msg_send_ping_ok (
    void *output)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_PING_OK);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the INVALID to the socket in one step

int
zpipes_msg_send_invalid (
    void *output)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_INVALID);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Duplicate the zpipes_msg message

zpipes_msg_t *
zpipes_msg_dup (zpipes_msg_t *self)
{
    if (!self)
        return NULL;
        
    zpipes_msg_t *copy = zpipes_msg_new (self->id);
    if (self->routing_id)
        copy->routing_id = zframe_dup (self->routing_id);
    switch (self->id) {
        case ZPIPES_MSG_INPUT:
            copy->pipename = self->pipename? strdup (self->pipename): NULL;
            break;

        case ZPIPES_MSG_INPUT_OK:
            break;

        case ZPIPES_MSG_INPUT_FAILED:
            copy->reason = self->reason? strdup (self->reason): NULL;
            break;

        case ZPIPES_MSG_OUTPUT:
            copy->pipename = self->pipename? strdup (self->pipename): NULL;
            break;

        case ZPIPES_MSG_OUTPUT_OK:
            break;

        case ZPIPES_MSG_OUTPUT_FAILED:
            copy->reason = self->reason? strdup (self->reason): NULL;
            break;

        case ZPIPES_MSG_READ:
            copy->size = self->size;
            copy->timeout = self->timeout;
            break;

        case ZPIPES_MSG_READ_OK:
            copy->chunk = self->chunk? zchunk_dup (self->chunk): NULL;
            break;

        case ZPIPES_MSG_READ_END:
            break;

        case ZPIPES_MSG_READ_TIMEOUT:
            break;

        case ZPIPES_MSG_READ_FAILED:
            copy->reason = self->reason? strdup (self->reason): NULL;
            break;

        case ZPIPES_MSG_WRITE:
            copy->chunk = self->chunk? zchunk_dup (self->chunk): NULL;
            copy->timeout = self->timeout;
            break;

        case ZPIPES_MSG_WRITE_OK:
            break;

        case ZPIPES_MSG_WRITE_TIMEOUT:
            break;

        case ZPIPES_MSG_WRITE_FAILED:
            copy->reason = self->reason? strdup (self->reason): NULL;
            break;

        case ZPIPES_MSG_CLOSE:
            break;

        case ZPIPES_MSG_CLOSE_OK:
            break;

        case ZPIPES_MSG_CLOSE_FAILED:
            copy->reason = self->reason? strdup (self->reason): NULL;
            break;

        case ZPIPES_MSG_PING:
            break;

        case ZPIPES_MSG_PING_OK:
            break;

        case ZPIPES_MSG_INVALID:
            break;

    }
    return copy;
}


//  --------------------------------------------------------------------------
//  Print contents of message to stdout

void
zpipes_msg_print (zpipes_msg_t *self)
{
    assert (self);
    switch (self->id) {
        case ZPIPES_MSG_INPUT:
            zsys_debug ("ZPIPES_MSG_INPUT:");
            if (self->pipename)
                zsys_debug ("    pipename='%s'", self->pipename);
            else
                zsys_debug ("    pipename=");
            break;
            
        case ZPIPES_MSG_INPUT_OK:
            zsys_debug ("ZPIPES_MSG_INPUT_OK:");
            break;
            
        case ZPIPES_MSG_INPUT_FAILED:
            zsys_debug ("ZPIPES_MSG_INPUT_FAILED:");
            if (self->reason)
                zsys_debug ("    reason='%s'", self->reason);
            else
                zsys_debug ("    reason=");
            break;
            
        case ZPIPES_MSG_OUTPUT:
            zsys_debug ("ZPIPES_MSG_OUTPUT:");
            if (self->pipename)
                zsys_debug ("    pipename='%s'", self->pipename);
            else
                zsys_debug ("    pipename=");
            break;
            
        case ZPIPES_MSG_OUTPUT_OK:
            zsys_debug ("ZPIPES_MSG_OUTPUT_OK:");
            break;
            
        case ZPIPES_MSG_OUTPUT_FAILED:
            zsys_debug ("ZPIPES_MSG_OUTPUT_FAILED:");
            if (self->reason)
                zsys_debug ("    reason='%s'", self->reason);
            else
                zsys_debug ("    reason=");
            break;
            
        case ZPIPES_MSG_READ:
            zsys_debug ("ZPIPES_MSG_READ:");
            zsys_debug ("    size=%ld", (long) self->size);
            zsys_debug ("    timeout=%ld", (long) self->timeout);
            break;
            
        case ZPIPES_MSG_READ_OK:
            zsys_debug ("ZPIPES_MSG_READ_OK:");
            zsys_debug ("    chunk=[ ... ]");
            break;
            
        case ZPIPES_MSG_READ_END:
            zsys_debug ("ZPIPES_MSG_READ_END:");
            break;
            
        case ZPIPES_MSG_READ_TIMEOUT:
            zsys_debug ("ZPIPES_MSG_READ_TIMEOUT:");
            break;
            
        case ZPIPES_MSG_READ_FAILED:
            zsys_debug ("ZPIPES_MSG_READ_FAILED:");
            if (self->reason)
                zsys_debug ("    reason='%s'", self->reason);
            else
                zsys_debug ("    reason=");
            break;
            
        case ZPIPES_MSG_WRITE:
            zsys_debug ("ZPIPES_MSG_WRITE:");
            zsys_debug ("    chunk=[ ... ]");
            zsys_debug ("    timeout=%ld", (long) self->timeout);
            break;
            
        case ZPIPES_MSG_WRITE_OK:
            zsys_debug ("ZPIPES_MSG_WRITE_OK:");
            break;
            
        case ZPIPES_MSG_WRITE_TIMEOUT:
            zsys_debug ("ZPIPES_MSG_WRITE_TIMEOUT:");
            break;
            
        case ZPIPES_MSG_WRITE_FAILED:
            zsys_debug ("ZPIPES_MSG_WRITE_FAILED:");
            if (self->reason)
                zsys_debug ("    reason='%s'", self->reason);
            else
                zsys_debug ("    reason=");
            break;
            
        case ZPIPES_MSG_CLOSE:
            zsys_debug ("ZPIPES_MSG_CLOSE:");
            break;
            
        case ZPIPES_MSG_CLOSE_OK:
            zsys_debug ("ZPIPES_MSG_CLOSE_OK:");
            break;
            
        case ZPIPES_MSG_CLOSE_FAILED:
            zsys_debug ("ZPIPES_MSG_CLOSE_FAILED:");
            if (self->reason)
                zsys_debug ("    reason='%s'", self->reason);
            else
                zsys_debug ("    reason=");
            break;
            
        case ZPIPES_MSG_PING:
            zsys_debug ("ZPIPES_MSG_PING:");
            break;
            
        case ZPIPES_MSG_PING_OK:
            zsys_debug ("ZPIPES_MSG_PING_OK:");
            break;
            
        case ZPIPES_MSG_INVALID:
            zsys_debug ("ZPIPES_MSG_INVALID:");
            break;
            
    }
}


//  --------------------------------------------------------------------------
//  Get/set the message routing_id

zframe_t *
zpipes_msg_routing_id (zpipes_msg_t *self)
{
    assert (self);
    return self->routing_id;
}

void
zpipes_msg_set_routing_id (zpipes_msg_t *self, zframe_t *routing_id)
{
    if (self->routing_id)
        zframe_destroy (&self->routing_id);
    self->routing_id = zframe_dup (routing_id);
}


//  --------------------------------------------------------------------------
//  Get/set the zpipes_msg id

int
zpipes_msg_id (zpipes_msg_t *self)
{
    assert (self);
    return self->id;
}

void
zpipes_msg_set_id (zpipes_msg_t *self, int id)
{
    self->id = id;
}

//  --------------------------------------------------------------------------
//  Return a printable command string

const char *
zpipes_msg_command (zpipes_msg_t *self)
{
    assert (self);
    switch (self->id) {
        case ZPIPES_MSG_INPUT:
            return ("INPUT");
            break;
        case ZPIPES_MSG_INPUT_OK:
            return ("INPUT_OK");
            break;
        case ZPIPES_MSG_INPUT_FAILED:
            return ("INPUT_FAILED");
            break;
        case ZPIPES_MSG_OUTPUT:
            return ("OUTPUT");
            break;
        case ZPIPES_MSG_OUTPUT_OK:
            return ("OUTPUT_OK");
            break;
        case ZPIPES_MSG_OUTPUT_FAILED:
            return ("OUTPUT_FAILED");
            break;
        case ZPIPES_MSG_READ:
            return ("READ");
            break;
        case ZPIPES_MSG_READ_OK:
            return ("READ_OK");
            break;
        case ZPIPES_MSG_READ_END:
            return ("READ_END");
            break;
        case ZPIPES_MSG_READ_TIMEOUT:
            return ("READ_TIMEOUT");
            break;
        case ZPIPES_MSG_READ_FAILED:
            return ("READ_FAILED");
            break;
        case ZPIPES_MSG_WRITE:
            return ("WRITE");
            break;
        case ZPIPES_MSG_WRITE_OK:
            return ("WRITE_OK");
            break;
        case ZPIPES_MSG_WRITE_TIMEOUT:
            return ("WRITE_TIMEOUT");
            break;
        case ZPIPES_MSG_WRITE_FAILED:
            return ("WRITE_FAILED");
            break;
        case ZPIPES_MSG_CLOSE:
            return ("CLOSE");
            break;
        case ZPIPES_MSG_CLOSE_OK:
            return ("CLOSE_OK");
            break;
        case ZPIPES_MSG_CLOSE_FAILED:
            return ("CLOSE_FAILED");
            break;
        case ZPIPES_MSG_PING:
            return ("PING");
            break;
        case ZPIPES_MSG_PING_OK:
            return ("PING_OK");
            break;
        case ZPIPES_MSG_INVALID:
            return ("INVALID");
            break;
    }
    return "?";
}

//  --------------------------------------------------------------------------
//  Get/set the pipename field

const char *
zpipes_msg_pipename (zpipes_msg_t *self)
{
    assert (self);
    return self->pipename;
}

void
zpipes_msg_set_pipename (zpipes_msg_t *self, const char *format, ...)
{
    //  Format pipename from provided arguments
    assert (self);
    va_list argptr;
    va_start (argptr, format);
    free (self->pipename);
    self->pipename = zsys_vprintf (format, argptr);
    va_end (argptr);
}


//  --------------------------------------------------------------------------
//  Get/set the reason field

const char *
zpipes_msg_reason (zpipes_msg_t *self)
{
    assert (self);
    return self->reason;
}

void
zpipes_msg_set_reason (zpipes_msg_t *self, const char *format, ...)
{
    //  Format reason from provided arguments
    assert (self);
    va_list argptr;
    va_start (argptr, format);
    free (self->reason);
    self->reason = zsys_vprintf (format, argptr);
    va_end (argptr);
}


//  --------------------------------------------------------------------------
//  Get/set the size field

uint32_t
zpipes_msg_size (zpipes_msg_t *self)
{
    assert (self);
    return self->size;
}

void
zpipes_msg_set_size (zpipes_msg_t *self, uint32_t size)
{
    assert (self);
    self->size = size;
}


//  --------------------------------------------------------------------------
//  Get/set the timeout field

uint32_t
zpipes_msg_timeout (zpipes_msg_t *self)
{
    assert (self);
    return self->timeout;
}

void
zpipes_msg_set_timeout (zpipes_msg_t *self, uint32_t timeout)
{
    assert (self);
    self->timeout = timeout;
}


//  --------------------------------------------------------------------------
//  Get the chunk field without transferring ownership

zchunk_t *
zpipes_msg_chunk (zpipes_msg_t *self)
{
    assert (self);
    return self->chunk;
}

//  Get the chunk field and transfer ownership to caller

zchunk_t *
zpipes_msg_get_chunk (zpipes_msg_t *self)
{
    zchunk_t *chunk = self->chunk;
    self->chunk = NULL;
    return chunk;
}

//  Set the chunk field, transferring ownership from caller

void
zpipes_msg_set_chunk (zpipes_msg_t *self, zchunk_t **chunk_p)
{
    assert (self);
    assert (chunk_p);
    zchunk_destroy (&self->chunk);
    self->chunk = *chunk_p;
    *chunk_p = NULL;
}



//  --------------------------------------------------------------------------
//  Selftest

int
zpipes_msg_test (bool verbose)
{
    printf (" * zpipes_msg: ");

    //  @selftest
    //  Simple create/destroy test
    zpipes_msg_t *self = zpipes_msg_new (0);
    assert (self);
    zpipes_msg_destroy (&self);

    //  Create pair of sockets we can send through
    zsock_t *input = zsock_new (ZMQ_ROUTER);
    assert (input);
    zsock_connect (input, "inproc://selftest-zpipes_msg");

    zsock_t *output = zsock_new (ZMQ_DEALER);
    assert (output);
    zsock_bind (output, "inproc://selftest-zpipes_msg");

    //  Encode/send/decode and verify each message type
    int instance;
    zpipes_msg_t *copy;
    self = zpipes_msg_new (ZPIPES_MSG_INPUT);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    zpipes_msg_set_pipename (self, "Life is short but Now lasts for ever");
    //  Send twice from same object
    zpipes_msg_send_again (self, output);
    zpipes_msg_send (&self, output);

    for (instance = 0; instance < 2; instance++) {
        self = zpipes_msg_recv (input);
        assert (self);
        assert (zpipes_msg_routing_id (self));
        
        assert (streq (zpipes_msg_pipename (self), "Life is short but Now lasts for ever"));
        zpipes_msg_destroy (&self);
    }
    self = zpipes_msg_new (ZPIPES_MSG_INPUT_OK);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    //  Send twice from same object
    zpipes_msg_send_again (self, output);
    zpipes_msg_send (&self, output);

    for (instance = 0; instance < 2; instance++) {
        self = zpipes_msg_recv (input);
        assert (self);
        assert (zpipes_msg_routing_id (self));
        
        zpipes_msg_destroy (&self);
    }
    self = zpipes_msg_new (ZPIPES_MSG_INPUT_FAILED);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    zpipes_msg_set_reason (self, "Life is short but Now lasts for ever");
    //  Send twice from same object
    zpipes_msg_send_again (self, output);
    zpipes_msg_send (&self, output);

    for (instance = 0; instance < 2; instance++) {
        self = zpipes_msg_recv (input);
        assert (self);
        assert (zpipes_msg_routing_id (self));
        
        assert (streq (zpipes_msg_reason (self), "Life is short but Now lasts for ever"));
        zpipes_msg_destroy (&self);
    }
    self = zpipes_msg_new (ZPIPES_MSG_OUTPUT);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    zpipes_msg_set_pipename (self, "Life is short but Now lasts for ever");
    //  Send twice from same object
    zpipes_msg_send_again (self, output);
    zpipes_msg_send (&self, output);

    for (instance = 0; instance < 2; instance++) {
        self = zpipes_msg_recv (input);
        assert (self);
        assert (zpipes_msg_routing_id (self));
        
        assert (streq (zpipes_msg_pipename (self), "Life is short but Now lasts for ever"));
        zpipes_msg_destroy (&self);
    }
    self = zpipes_msg_new (ZPIPES_MSG_OUTPUT_OK);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    //  Send twice from same object
    zpipes_msg_send_again (self, output);
    zpipes_msg_send (&self, output);

    for (instance = 0; instance < 2; instance++) {
        self = zpipes_msg_recv (input);
        assert (self);
        assert (zpipes_msg_routing_id (self));
        
        zpipes_msg_destroy (&self);
    }
    self = zpipes_msg_new (ZPIPES_MSG_OUTPUT_FAILED);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    zpipes_msg_set_reason (self, "Life is short but Now lasts for ever");
    //  Send twice from same object
    zpipes_msg_send_again (self, output);
    zpipes_msg_send (&self, output);

    for (instance = 0; instance < 2; instance++) {
        self = zpipes_msg_recv (input);
        assert (self);
        assert (zpipes_msg_routing_id (self));
        
        assert (streq (zpipes_msg_reason (self), "Life is short but Now lasts for ever"));
        zpipes_msg_destroy (&self);
    }
    self = zpipes_msg_new (ZPIPES_MSG_READ);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    zpipes_msg_set_size (self, 123);
    zpipes_msg_set_timeout (self, 123);
    //  Send twice from same object
    zpipes_msg_send_again (self, output);
    zpipes_msg_send (&self, output);

    for (instance = 0; instance < 2; instance++) {
        self = zpipes_msg_recv (input);
        assert (self);
        assert (zpipes_msg_routing_id (self));
        
        assert (zpipes_msg_size (self) == 123);
        assert (zpipes_msg_timeout (self) == 123);
        zpipes_msg_destroy (&self);
    }
    self = zpipes_msg_new (ZPIPES_MSG_READ_OK);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    zchunk_t *read_ok_chunk = zchunk_new ("Captcha Diem", 12);
    zpipes_msg_set_chunk (self, &read_ok_chunk);
    //  Send twice from same object
    zpipes_msg_send_again (self, output);
    zpipes_msg_send (&self, output);

    for (instance = 0; instance < 2; instance++) {
        self = zpipes_msg_recv (input);
        assert (self);
        assert (zpipes_msg_routing_id (self));
        
        assert (memcmp (zchunk_data (zpipes_msg_chunk (self)), "Captcha Diem", 12) == 0);
        zpipes_msg_destroy (&self);
    }
    self = zpipes_msg_new (ZPIPES_MSG_READ_END);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    //  Send twice from same object
    zpipes_msg_send_again (self, output);
    zpipes_msg_send (&self, output);

    for (instance = 0; instance < 2; instance++) {
        self = zpipes_msg_recv (input);
        assert (self);
        assert (zpipes_msg_routing_id (self));
        
        zpipes_msg_destroy (&self);
    }
    self = zpipes_msg_new (ZPIPES_MSG_READ_TIMEOUT);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    //  Send twice from same object
    zpipes_msg_send_again (self, output);
    zpipes_msg_send (&self, output);

    for (instance = 0; instance < 2; instance++) {
        self = zpipes_msg_recv (input);
        assert (self);
        assert (zpipes_msg_routing_id (self));
        
        zpipes_msg_destroy (&self);
    }
    self = zpipes_msg_new (ZPIPES_MSG_READ_FAILED);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    zpipes_msg_set_reason (self, "Life is short but Now lasts for ever");
    //  Send twice from same object
    zpipes_msg_send_again (self, output);
    zpipes_msg_send (&self, output);

    for (instance = 0; instance < 2; instance++) {
        self = zpipes_msg_recv (input);
        assert (self);
        assert (zpipes_msg_routing_id (self));
        
        assert (streq (zpipes_msg_reason (self), "Life is short but Now lasts for ever"));
        zpipes_msg_destroy (&self);
    }
    self = zpipes_msg_new (ZPIPES_MSG_WRITE);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    zchunk_t *write_chunk = zchunk_new ("Captcha Diem", 12);
    zpipes_msg_set_chunk (self, &write_chunk);
    zpipes_msg_set_timeout (self, 123);
    //  Send twice from same object
    zpipes_msg_send_again (self, output);
    zpipes_msg_send (&self, output);

    for (instance = 0; instance < 2; instance++) {
        self = zpipes_msg_recv (input);
        assert (self);
        assert (zpipes_msg_routing_id (self));
        
        assert (memcmp (zchunk_data (zpipes_msg_chunk (self)), "Captcha Diem", 12) == 0);
        assert (zpipes_msg_timeout (self) == 123);
        zpipes_msg_destroy (&self);
    }
    self = zpipes_msg_new (ZPIPES_MSG_WRITE_OK);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    //  Send twice from same object
    zpipes_msg_send_again (self, output);
    zpipes_msg_send (&self, output);

    for (instance = 0; instance < 2; instance++) {
        self = zpipes_msg_recv (input);
        assert (self);
        assert (zpipes_msg_routing_id (self));
        
        zpipes_msg_destroy (&self);
    }
    self = zpipes_msg_new (ZPIPES_MSG_WRITE_TIMEOUT);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    //  Send twice from same object
    zpipes_msg_send_again (self, output);
    zpipes_msg_send (&self, output);

    for (instance = 0; instance < 2; instance++) {
        self = zpipes_msg_recv (input);
        assert (self);
        assert (zpipes_msg_routing_id (self));
        
        zpipes_msg_destroy (&self);
    }
    self = zpipes_msg_new (ZPIPES_MSG_WRITE_FAILED);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    zpipes_msg_set_reason (self, "Life is short but Now lasts for ever");
    //  Send twice from same object
    zpipes_msg_send_again (self, output);
    zpipes_msg_send (&self, output);

    for (instance = 0; instance < 2; instance++) {
        self = zpipes_msg_recv (input);
        assert (self);
        assert (zpipes_msg_routing_id (self));
        
        assert (streq (zpipes_msg_reason (self), "Life is short but Now lasts for ever"));
        zpipes_msg_destroy (&self);
    }
    self = zpipes_msg_new (ZPIPES_MSG_CLOSE);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    //  Send twice from same object
    zpipes_msg_send_again (self, output);
    zpipes_msg_send (&self, output);

    for (instance = 0; instance < 2; instance++) {
        self = zpipes_msg_recv (input);
        assert (self);
        assert (zpipes_msg_routing_id (self));
        
        zpipes_msg_destroy (&self);
    }
    self = zpipes_msg_new (ZPIPES_MSG_CLOSE_OK);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    //  Send twice from same object
    zpipes_msg_send_again (self, output);
    zpipes_msg_send (&self, output);

    for (instance = 0; instance < 2; instance++) {
        self = zpipes_msg_recv (input);
        assert (self);
        assert (zpipes_msg_routing_id (self));
        
        zpipes_msg_destroy (&self);
    }
    self = zpipes_msg_new (ZPIPES_MSG_CLOSE_FAILED);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    zpipes_msg_set_reason (self, "Life is short but Now lasts for ever");
    //  Send twice from same object
    zpipes_msg_send_again (self, output);
    zpipes_msg_send (&self, output);

    for (instance = 0; instance < 2; instance++) {
        self = zpipes_msg_recv (input);
        assert (self);
        assert (zpipes_msg_routing_id (self));
        
        assert (streq (zpipes_msg_reason (self), "Life is short but Now lasts for ever"));
        zpipes_msg_destroy (&self);
    }
    self = zpipes_msg_new (ZPIPES_MSG_PING);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    //  Send twice from same object
    zpipes_msg_send_again (self, output);
    zpipes_msg_send (&self, output);

    for (instance = 0; instance < 2; instance++) {
        self = zpipes_msg_recv (input);
        assert (self);
        assert (zpipes_msg_routing_id (self));
        
        zpipes_msg_destroy (&self);
    }
    self = zpipes_msg_new (ZPIPES_MSG_PING_OK);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    //  Send twice from same object
    zpipes_msg_send_again (self, output);
    zpipes_msg_send (&self, output);

    for (instance = 0; instance < 2; instance++) {
        self = zpipes_msg_recv (input);
        assert (self);
        assert (zpipes_msg_routing_id (self));
        
        zpipes_msg_destroy (&self);
    }
    self = zpipes_msg_new (ZPIPES_MSG_INVALID);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    //  Send twice from same object
    zpipes_msg_send_again (self, output);
    zpipes_msg_send (&self, output);

    for (instance = 0; instance < 2; instance++) {
        self = zpipes_msg_recv (input);
        assert (self);
        assert (zpipes_msg_routing_id (self));
        
        zpipes_msg_destroy (&self);
    }

    zsock_destroy (&input);
    zsock_destroy (&output);
    //  @end

    printf ("OK\n");
    return 0;
}
