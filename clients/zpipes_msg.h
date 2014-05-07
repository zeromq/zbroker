/*  =========================================================================
    zpipes_msg - ZPIPES protocol
    
    Codec header for zpipes_msg.

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

#ifndef __ZPIPES_MSG_H_INCLUDED__
#define __ZPIPES_MSG_H_INCLUDED__

/*  These are the zpipes_msg messages:

    INPUT - Create a new pipe for reading
        pipename            string      Name of pipe

    INPUT_OK - Input request was successful

    INPUT_FAILED - Input request failed
        reason              string      Reason for failure

    OUTPUT - Create a new pipe for writing
        pipename            string      Name of pipe

    OUTPUT_OK - Output request was successful

    OUTPUT_FAILED - Output request failed
        reason              string      Reason for failure

    READ - Read a chunk of data from pipe
        size                number 4    Number of bytes to read
        timeout             number 4    Timeout, msecs, or zero

    READ_OK - Read was successful
        chunk               chunk       Chunk of data

    READ_END - Pipe is closed, no more data

    READ_TIMEOUT - Read ended with timeout

    READ_FAILED - Read failed due to error
        reason              string      Reason for failure

    WRITE - Write chunk of data to pipe
        chunk               chunk       Chunk of data
        timeout             number 4    Timeout, msecs, or zero

    WRITE_OK - Write was successful

    WRITE_TIMEOUT - Write ended with timeout

    WRITE_FAILED - Read failed due to error
        reason              string      Reason for failure

    CLOSE - Close pipe

    CLOSE_OK - Close was successful

    CLOSE_FAILED - Close failed due to error
        reason              string      Reason for failure

    PING - Signal liveness

    PING_OK - Respond to ping

    INVALID - Command was invalid at this time
*/


#define ZPIPES_MSG_INPUT                    1
#define ZPIPES_MSG_INPUT_OK                 2
#define ZPIPES_MSG_INPUT_FAILED             3
#define ZPIPES_MSG_OUTPUT                   4
#define ZPIPES_MSG_OUTPUT_OK                5
#define ZPIPES_MSG_OUTPUT_FAILED            6
#define ZPIPES_MSG_READ                     7
#define ZPIPES_MSG_READ_OK                  8
#define ZPIPES_MSG_READ_END                 9
#define ZPIPES_MSG_READ_TIMEOUT             10
#define ZPIPES_MSG_READ_FAILED              11
#define ZPIPES_MSG_WRITE                    12
#define ZPIPES_MSG_WRITE_OK                 13
#define ZPIPES_MSG_WRITE_TIMEOUT            14
#define ZPIPES_MSG_WRITE_FAILED             15
#define ZPIPES_MSG_CLOSE                    16
#define ZPIPES_MSG_CLOSE_OK                 17
#define ZPIPES_MSG_CLOSE_FAILED             18
#define ZPIPES_MSG_PING                     19
#define ZPIPES_MSG_PING_OK                  20
#define ZPIPES_MSG_INVALID                  21

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

//  Parse a zpipes_msg from zmtp_msg_t. Returns a new object, or NULL 
//  if the message could not be parsed, or was NULL. Nullifies the msg
//  reference.
zpipes_msg_t *
    zpipes_msg_decode (zmtp_msg_t **msg_p);

//  Encode zpipes_msg into a message. Returns a newly created object
//  or NULL if error. Use when not in control of sending the message.
//  Destroys the zpipes_msg_t instance.
zmtp_msg_t *
    zpipes_msg_encode (zpipes_msg_t **self_p);

//  Receive and parse a zpipes_msg from the socket. Returns new object,
//  or NULL if error. Will block if there's no message waiting.
zpipes_msg_t *
    zpipes_msg_recv (zmtp_dealer_t *input);

//  Send the zpipes_msg to the output, and destroy it
int
    zpipes_msg_send (zpipes_msg_t **self_p, zmtp_dealer_t *output);

//  Send the INPUT to the output in one step
int
    zpipes_msg_send_input (zmtp_dealer_t *output,
        const char *pipename);

//  Send the INPUT_OK to the output in one step
int
    zpipes_msg_send_input_ok (zmtp_dealer_t *output);

//  Send the INPUT_FAILED to the output in one step
int
    zpipes_msg_send_input_failed (zmtp_dealer_t *output,
        const char *reason);

//  Send the OUTPUT to the output in one step
int
    zpipes_msg_send_output (zmtp_dealer_t *output,
        const char *pipename);

//  Send the OUTPUT_OK to the output in one step
int
    zpipes_msg_send_output_ok (zmtp_dealer_t *output);

//  Send the OUTPUT_FAILED to the output in one step
int
    zpipes_msg_send_output_failed (zmtp_dealer_t *output,
        const char *reason);

//  Send the READ to the output in one step
int
    zpipes_msg_send_read (zmtp_dealer_t *output,
        uint32_t size,
        uint32_t timeout);

//  Send the READ_OK to the output in one step
int
    zpipes_msg_send_read_ok (zmtp_dealer_t *output,
        zchunk_t *chunk);

//  Send the READ_END to the output in one step
int
    zpipes_msg_send_read_end (zmtp_dealer_t *output);

//  Send the READ_TIMEOUT to the output in one step
int
    zpipes_msg_send_read_timeout (zmtp_dealer_t *output);

//  Send the READ_FAILED to the output in one step
int
    zpipes_msg_send_read_failed (zmtp_dealer_t *output,
        const char *reason);

//  Send the WRITE to the output in one step
int
    zpipes_msg_send_write (zmtp_dealer_t *output,
        zchunk_t *chunk,
        uint32_t timeout);

//  Send the WRITE_OK to the output in one step
int
    zpipes_msg_send_write_ok (zmtp_dealer_t *output);

//  Send the WRITE_TIMEOUT to the output in one step
int
    zpipes_msg_send_write_timeout (zmtp_dealer_t *output);

//  Send the WRITE_FAILED to the output in one step
int
    zpipes_msg_send_write_failed (zmtp_dealer_t *output,
        const char *reason);

//  Send the CLOSE to the output in one step
int
    zpipes_msg_send_close (zmtp_dealer_t *output);

//  Send the CLOSE_OK to the output in one step
int
    zpipes_msg_send_close_ok (zmtp_dealer_t *output);

//  Send the CLOSE_FAILED to the output in one step
int
    zpipes_msg_send_close_failed (zmtp_dealer_t *output,
        const char *reason);

//  Send the PING to the output in one step
int
    zpipes_msg_send_ping (zmtp_dealer_t *output);

//  Send the PING_OK to the output in one step
int
    zpipes_msg_send_ping_ok (zmtp_dealer_t *output);

//  Send the INVALID to the output in one step
int
    zpipes_msg_send_invalid (zmtp_dealer_t *output);

//  Duplicate the zpipes_msg message
zpipes_msg_t *
    zpipes_msg_dup (zpipes_msg_t *self);

//  Print contents of message to stdout
void
    zpipes_msg_dump (zpipes_msg_t *self);

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

//  Get/set the size field
uint32_t
    zpipes_msg_size (zpipes_msg_t *self);
void
    zpipes_msg_set_size (zpipes_msg_t *self, uint32_t size);

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
