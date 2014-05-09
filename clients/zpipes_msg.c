/*  =========================================================================
    zpipes_msg - ZPIPES protocol

    Codec class for zpipes_msg.

    ** WARNING *************************************************************
    THIS SOURCE FILE IS 100% GENERATED. If you edit this file, you will lose
    your changes at the next build cycle. This is great for temporary printf
    statements. DO NOT MAKE ANY CHANGES YOU WISH TO KEEP. The correct places
    for commits are:

    * The XML model used for this code generation: zpipes_msg.xml
    * The code generation script that built this file: codec_libzmtp
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

#include <zmtp.h>
#include "zchunk.h"
#include "zpipes_msg.h"

//  Structure of our class

struct _zpipes_msg_t {
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
        free (self->pipename);
        free (self->reason);
        zchunk_destroy (&self->chunk);

        //  Free object itself
        free (self);
        *self_p = NULL;
    }
}


//  --------------------------------------------------------------------------
//  Parse a zpipes_msg from zmtp_msg_t. Returns a new object, or NULL if
//  the message could not be parsed, or was NULL.

zpipes_msg_t *
zpipes_msg_decode (zmtp_msg_t **msg_p)
{
    assert (msg_p);
    zmtp_msg_t *msg = *msg_p;
    if (msg == NULL)
        return NULL;
        
    //  Get and check protocol signature
    zpipes_msg_t *self = zpipes_msg_new (0);
    self->needle = zmtp_msg_data (msg);
    self->ceiling = self->needle + zmtp_msg_size (msg);
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
    zmtp_msg_destroy (msg_p);
    return self;

    //  Error returns
    malformed:
        printf ("E: malformed message '%d'\n", self->id);
    empty:
        zmtp_msg_destroy (msg_p);
        zpipes_msg_destroy (&self);
        return (NULL);
}


//  --------------------------------------------------------------------------
//  Encode zpipes_msg into a message. Returns a newly created object
//  or NULL if error. Use when not in control of sending the message.
//  Destroys the zpipes_msg_t instance.

zmtp_msg_t *
zpipes_msg_encode (zpipes_msg_t **self_p)
{
    assert (self_p);
    assert (*self_p);
    zpipes_msg_t *self = *self_p;

    size_t msg_size = 2 + 1;          //  Signature and message ID
    switch (self->id) {
        case ZPIPES_MSG_INPUT:
            //  pipename is a string with 1-byte length
            msg_size++;       //  Size is one octet
            if (self->pipename)
                msg_size += strlen (self->pipename);
            break;
            
        case ZPIPES_MSG_INPUT_OK:
            break;
            
        case ZPIPES_MSG_INPUT_FAILED:
            //  reason is a string with 1-byte length
            msg_size++;       //  Size is one octet
            if (self->reason)
                msg_size += strlen (self->reason);
            break;
            
        case ZPIPES_MSG_OUTPUT:
            //  pipename is a string with 1-byte length
            msg_size++;       //  Size is one octet
            if (self->pipename)
                msg_size += strlen (self->pipename);
            break;
            
        case ZPIPES_MSG_OUTPUT_OK:
            break;
            
        case ZPIPES_MSG_OUTPUT_FAILED:
            //  reason is a string with 1-byte length
            msg_size++;       //  Size is one octet
            if (self->reason)
                msg_size += strlen (self->reason);
            break;
            
        case ZPIPES_MSG_READ:
            //  size is a 4-byte integer
            msg_size += 4;
            //  timeout is a 4-byte integer
            msg_size += 4;
            break;
            
        case ZPIPES_MSG_READ_OK:
            //  chunk is a chunk with 4-byte length
            msg_size += 4;
            if (self->chunk)
                msg_size += zchunk_size (self->chunk);
            break;
            
        case ZPIPES_MSG_READ_END:
            break;
            
        case ZPIPES_MSG_READ_TIMEOUT:
            break;
            
        case ZPIPES_MSG_READ_FAILED:
            //  reason is a string with 1-byte length
            msg_size++;       //  Size is one octet
            if (self->reason)
                msg_size += strlen (self->reason);
            break;
            
        case ZPIPES_MSG_WRITE:
            //  chunk is a chunk with 4-byte length
            msg_size += 4;
            if (self->chunk)
                msg_size += zchunk_size (self->chunk);
            //  timeout is a 4-byte integer
            msg_size += 4;
            break;
            
        case ZPIPES_MSG_WRITE_OK:
            break;
            
        case ZPIPES_MSG_WRITE_TIMEOUT:
            break;
            
        case ZPIPES_MSG_WRITE_FAILED:
            //  reason is a string with 1-byte length
            msg_size++;       //  Size is one octet
            if (self->reason)
                msg_size += strlen (self->reason);
            break;
            
        case ZPIPES_MSG_CLOSE:
            break;
            
        case ZPIPES_MSG_CLOSE_OK:
            break;
            
        case ZPIPES_MSG_CLOSE_FAILED:
            //  reason is a string with 1-byte length
            msg_size++;       //  Size is one octet
            if (self->reason)
                msg_size += strlen (self->reason);
            break;
            
        case ZPIPES_MSG_PING:
            break;
            
        case ZPIPES_MSG_PING_OK:
            break;
            
        case ZPIPES_MSG_INVALID:
            break;
            
        default:
            printf ("E: bad message type '%d', not sent\n", self->id);
            //  No recovery, this is a fatal application error
            assert (false);
    }
    //  Now serialize message into the frame
    zmtp_msg_t *msg = zmtp_msg_new (0, msg_size);
    self->needle = zmtp_msg_data (msg);

    //  Each message frame starts with protocol signature
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
    //  Destroy zpipes_msg object
    zpipes_msg_destroy (self_p);
    return msg;
}


//  --------------------------------------------------------------------------
//  Receive and parse a zpipes_msg from the socket. Returns new object or
//  NULL if error. Will block if there's no message waiting.

zpipes_msg_t *
zpipes_msg_recv (zmtp_dealer_t *input)
{
    assert (input);
    zmtp_msg_t *msg = zmtp_dealer_recv (input);
    
    zpipes_msg_t * zpipes_msg = zpipes_msg_decode (&msg);
    return zpipes_msg;
}


//  --------------------------------------------------------------------------
//  Send the zpipes_msg to the socket, and destroy it
//  Returns 0 if OK, else -1

int
zpipes_msg_send (zpipes_msg_t **self_p, zmtp_dealer_t *output)
{
    assert (self_p);
    assert (*self_p);
    assert (output);

    //  Encode zpipes_msg message to a single message
    zmtp_msg_t *msg = zpipes_msg_encode (self_p);
    int rc = -1;
    if (msg)
        rc = zmtp_dealer_send (output, msg);

    zmtp_msg_destroy (&msg);
    return rc;
}


//  --------------------------------------------------------------------------
//  Send the INPUT to the socket in one step

int
zpipes_msg_send_input (
    zmtp_dealer_t *output,
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
    zmtp_dealer_t *output)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_INPUT_OK);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the INPUT_FAILED to the socket in one step

int
zpipes_msg_send_input_failed (
    zmtp_dealer_t *output,
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
    zmtp_dealer_t *output,
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
    zmtp_dealer_t *output)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_OUTPUT_OK);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the OUTPUT_FAILED to the socket in one step

int
zpipes_msg_send_output_failed (
    zmtp_dealer_t *output,
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
    zmtp_dealer_t *output,
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
    zmtp_dealer_t *output,
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
    zmtp_dealer_t *output)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_READ_END);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the READ_TIMEOUT to the socket in one step

int
zpipes_msg_send_read_timeout (
    zmtp_dealer_t *output)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_READ_TIMEOUT);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the READ_FAILED to the socket in one step

int
zpipes_msg_send_read_failed (
    zmtp_dealer_t *output,
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
    zmtp_dealer_t *output,
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
    zmtp_dealer_t *output)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_WRITE_OK);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the WRITE_TIMEOUT to the socket in one step

int
zpipes_msg_send_write_timeout (
    zmtp_dealer_t *output)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_WRITE_TIMEOUT);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the WRITE_FAILED to the socket in one step

int
zpipes_msg_send_write_failed (
    zmtp_dealer_t *output,
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
    zmtp_dealer_t *output)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_CLOSE);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the CLOSE_OK to the socket in one step

int
zpipes_msg_send_close_ok (
    zmtp_dealer_t *output)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_CLOSE_OK);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the CLOSE_FAILED to the socket in one step

int
zpipes_msg_send_close_failed (
    zmtp_dealer_t *output,
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
    zmtp_dealer_t *output)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_PING);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the PING_OK to the socket in one step

int
zpipes_msg_send_ping_ok (
    zmtp_dealer_t *output)
{
    zpipes_msg_t *self = zpipes_msg_new (ZPIPES_MSG_PING_OK);
    return zpipes_msg_send (&self, output);
}


//  --------------------------------------------------------------------------
//  Send the INVALID to the socket in one step

int
zpipes_msg_send_invalid (
    zmtp_dealer_t *output)
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
zpipes_msg_dump (zpipes_msg_t *self)
{
    assert (self);
    switch (self->id) {
        case ZPIPES_MSG_INPUT:
            puts ("INPUT:");
            if (self->pipename)
                printf ("    pipename='%s'\n", self->pipename);
            else
                printf ("    pipename=\n");
            break;
            
        case ZPIPES_MSG_INPUT_OK:
            puts ("INPUT_OK:");
            break;
            
        case ZPIPES_MSG_INPUT_FAILED:
            puts ("INPUT_FAILED:");
            if (self->reason)
                printf ("    reason='%s'\n", self->reason);
            else
                printf ("    reason=\n");
            break;
            
        case ZPIPES_MSG_OUTPUT:
            puts ("OUTPUT:");
            if (self->pipename)
                printf ("    pipename='%s'\n", self->pipename);
            else
                printf ("    pipename=\n");
            break;
            
        case ZPIPES_MSG_OUTPUT_OK:
            puts ("OUTPUT_OK:");
            break;
            
        case ZPIPES_MSG_OUTPUT_FAILED:
            puts ("OUTPUT_FAILED:");
            if (self->reason)
                printf ("    reason='%s'\n", self->reason);
            else
                printf ("    reason=\n");
            break;
            
        case ZPIPES_MSG_READ:
            puts ("READ:");
            printf ("    size=%ld\n", (long) self->size);
            printf ("    timeout=%ld\n", (long) self->timeout);
            break;
            
        case ZPIPES_MSG_READ_OK:
            puts ("READ_OK:");
            printf ("    chunk={\n");
            if (self->chunk)
                zchunk_print (self->chunk);
            else
                printf ("(NULL)\n");
            printf ("    }\n");
            break;
            
        case ZPIPES_MSG_READ_END:
            puts ("READ_END:");
            break;
            
        case ZPIPES_MSG_READ_TIMEOUT:
            puts ("READ_TIMEOUT:");
            break;
            
        case ZPIPES_MSG_READ_FAILED:
            puts ("READ_FAILED:");
            if (self->reason)
                printf ("    reason='%s'\n", self->reason);
            else
                printf ("    reason=\n");
            break;
            
        case ZPIPES_MSG_WRITE:
            puts ("WRITE:");
            printf ("    chunk={\n");
            if (self->chunk)
                zchunk_print (self->chunk);
            else
                printf ("(NULL)\n");
            printf ("    }\n");
            printf ("    timeout=%ld\n", (long) self->timeout);
            break;
            
        case ZPIPES_MSG_WRITE_OK:
            puts ("WRITE_OK:");
            break;
            
        case ZPIPES_MSG_WRITE_TIMEOUT:
            puts ("WRITE_TIMEOUT:");
            break;
            
        case ZPIPES_MSG_WRITE_FAILED:
            puts ("WRITE_FAILED:");
            if (self->reason)
                printf ("    reason='%s'\n", self->reason);
            else
                printf ("    reason=\n");
            break;
            
        case ZPIPES_MSG_CLOSE:
            puts ("CLOSE:");
            break;
            
        case ZPIPES_MSG_CLOSE_OK:
            puts ("CLOSE_OK:");
            break;
            
        case ZPIPES_MSG_CLOSE_FAILED:
            puts ("CLOSE_FAILED:");
            if (self->reason)
                printf ("    reason='%s'\n", self->reason);
            else
                printf ("    reason=\n");
            break;
            
        case ZPIPES_MSG_PING:
            puts ("PING:");
            break;
            
        case ZPIPES_MSG_PING_OK:
            puts ("PING_OK:");
            break;
            
        case ZPIPES_MSG_INVALID:
            puts ("INVALID:");
            break;
            
    }
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
    char formatted [256];
    va_list argptr;
    va_start (argptr, format);
    snprintf (formatted, 255, format, argptr);
    va_end (argptr);
    formatted [255] = 0;
    free (self->pipename);
    self->pipename = strdup (formatted);
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
    char formatted [256];
    va_list argptr;
    va_start (argptr, format);
    snprintf (formatted, 255, format, argptr);
    va_end (argptr);
    formatted [255] = 0;
    free (self->reason);
    self->reason = strdup (formatted);
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

    //  Encode/decode and verify each message type
    zpipes_msg_t *copy;
    self = zpipes_msg_new (ZPIPES_MSG_INPUT);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    zpipes_msg_set_pipename (self, "Life is short but Now lasts for ever");

    assert (streq (zpipes_msg_pipename (self), "Life is short but Now lasts for ever"));
    zpipes_msg_destroy (&self);
    self = zpipes_msg_new (ZPIPES_MSG_INPUT_OK);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);


    zpipes_msg_destroy (&self);
    self = zpipes_msg_new (ZPIPES_MSG_INPUT_FAILED);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    zpipes_msg_set_reason (self, "Life is short but Now lasts for ever");

    assert (streq (zpipes_msg_reason (self), "Life is short but Now lasts for ever"));
    zpipes_msg_destroy (&self);
    self = zpipes_msg_new (ZPIPES_MSG_OUTPUT);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    zpipes_msg_set_pipename (self, "Life is short but Now lasts for ever");

    assert (streq (zpipes_msg_pipename (self), "Life is short but Now lasts for ever"));
    zpipes_msg_destroy (&self);
    self = zpipes_msg_new (ZPIPES_MSG_OUTPUT_OK);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);


    zpipes_msg_destroy (&self);
    self = zpipes_msg_new (ZPIPES_MSG_OUTPUT_FAILED);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    zpipes_msg_set_reason (self, "Life is short but Now lasts for ever");

    assert (streq (zpipes_msg_reason (self), "Life is short but Now lasts for ever"));
    zpipes_msg_destroy (&self);
    self = zpipes_msg_new (ZPIPES_MSG_READ);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    zpipes_msg_set_size (self, 123);
    zpipes_msg_set_timeout (self, 123);

    assert (zpipes_msg_size (self) == 123);
    assert (zpipes_msg_timeout (self) == 123);
    zpipes_msg_destroy (&self);
    self = zpipes_msg_new (ZPIPES_MSG_READ_OK);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    zchunk_t *read_ok_chunk = zchunk_new ("Captcha Diem", 12);
    zpipes_msg_set_chunk (self, &read_ok_chunk);

    assert (memcmp (zchunk_data (zpipes_msg_chunk (self)), "Captcha Diem", 12) == 0);
    zpipes_msg_destroy (&self);
    self = zpipes_msg_new (ZPIPES_MSG_READ_END);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);


    zpipes_msg_destroy (&self);
    self = zpipes_msg_new (ZPIPES_MSG_READ_TIMEOUT);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);


    zpipes_msg_destroy (&self);
    self = zpipes_msg_new (ZPIPES_MSG_READ_FAILED);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    zpipes_msg_set_reason (self, "Life is short but Now lasts for ever");

    assert (streq (zpipes_msg_reason (self), "Life is short but Now lasts for ever"));
    zpipes_msg_destroy (&self);
    self = zpipes_msg_new (ZPIPES_MSG_WRITE);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    zchunk_t *write_chunk = zchunk_new ("Captcha Diem", 12);
    zpipes_msg_set_chunk (self, &write_chunk);
    zpipes_msg_set_timeout (self, 123);

    assert (memcmp (zchunk_data (zpipes_msg_chunk (self)), "Captcha Diem", 12) == 0);
    assert (zpipes_msg_timeout (self) == 123);
    zpipes_msg_destroy (&self);
    self = zpipes_msg_new (ZPIPES_MSG_WRITE_OK);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);


    zpipes_msg_destroy (&self);
    self = zpipes_msg_new (ZPIPES_MSG_WRITE_TIMEOUT);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);


    zpipes_msg_destroy (&self);
    self = zpipes_msg_new (ZPIPES_MSG_WRITE_FAILED);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    zpipes_msg_set_reason (self, "Life is short but Now lasts for ever");

    assert (streq (zpipes_msg_reason (self), "Life is short but Now lasts for ever"));
    zpipes_msg_destroy (&self);
    self = zpipes_msg_new (ZPIPES_MSG_CLOSE);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);


    zpipes_msg_destroy (&self);
    self = zpipes_msg_new (ZPIPES_MSG_CLOSE_OK);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);


    zpipes_msg_destroy (&self);
    self = zpipes_msg_new (ZPIPES_MSG_CLOSE_FAILED);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);

    zpipes_msg_set_reason (self, "Life is short but Now lasts for ever");

    assert (streq (zpipes_msg_reason (self), "Life is short but Now lasts for ever"));
    zpipes_msg_destroy (&self);
    self = zpipes_msg_new (ZPIPES_MSG_PING);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);


    zpipes_msg_destroy (&self);
    self = zpipes_msg_new (ZPIPES_MSG_PING_OK);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);


    zpipes_msg_destroy (&self);
    self = zpipes_msg_new (ZPIPES_MSG_INVALID);
    
    //  Check that _dup works on empty message
    copy = zpipes_msg_dup (self);
    assert (copy);
    zpipes_msg_destroy (&copy);


    zpipes_msg_destroy (&self);
    //  @end

    printf ("OK\n");
    return 0;
}
