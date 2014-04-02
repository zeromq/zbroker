/*  =========================================================================
    zpipes_server_engine - ZPIPES server engine

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

//  The server runs as a background thread so that we can run multiple
//  classs at once. The API talks to the server thread over an inproc
//  pipe.

static void
s_server_task (void *args, zctx_t *ctx, void *pipe);

//  ---------------------------------------------------------------------
//  Structure of the front-end API class for zpipes_server

struct _zpipes_server_t {
    zctx_t *ctx;        //  CZMQ context
    void *pipe;         //  Pipe through to server
};


//  --------------------------------------------------------------------------
//  Create a new zpipes_server and a new server instance

zpipes_server_t *
zpipes_server_new (void)
{
    zpipes_server_t *self = (zpipes_server_t *) zmalloc (sizeof (zpipes_server_t));
    assert (self);

    //  Start a background thread for each server instance
    self->ctx = zctx_new ();
    self->pipe = zthread_fork (self->ctx, s_server_task, NULL);
    if (self->pipe) {
        char *status = zstr_recv (self->pipe);
        if (strneq (status, "OK"))
            zpipes_server_destroy (&self);
        zstr_free (&status);
    }
    else {
        free (self);
        self = NULL;
    }
    return self;
}


//  --------------------------------------------------------------------------
//  Destroy the zpipes_server and stop the server

void
zpipes_server_destroy (zpipes_server_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        zpipes_server_t *self = *self_p;
        zstr_send (self->pipe, "TERMINATE");
        char *string = zstr_recv (self->pipe);
        free (string);
        zctx_destroy (&self->ctx);
        free (self);
        *self_p = NULL;
    }
}


//  --------------------------------------------------------------------------
//  Load server configuration data
void
zpipes_server_configure (zpipes_server_t *self, const char *config_file)
{
    zstr_sendm (self->pipe, "CONFIGURE");
    zstr_send (self->pipe, config_file);
}


//  --------------------------------------------------------------------------
//  Set one configuration option value

void
zpipes_server_set (zpipes_server_t *self, const char *path, const char *value)
{
    zstr_sendm (self->pipe, "SET");
    zstr_sendm (self->pipe, path);
    zstr_send  (self->pipe, value);
}


//  --------------------------------------------------------------------------
//  Binds the server to an endpoint, formatted as printf string

long
zpipes_server_bind (zpipes_server_t *self, const char *format, ...)
{
    assert (self);
    assert (format);
    
    //  Format endpoint from provided arguments
    va_list argptr;
    va_start (argptr, format);
    char *endpoint = zsys_vprintf (format, argptr);
    va_end (argptr);

    //  Send BIND command to server task
    zstr_sendm (self->pipe, "BIND");
    zstr_send (self->pipe, endpoint);
    char *reply = zstr_recv (self->pipe);
    long reply_value = atol (reply);
    free (reply);
    free (endpoint);
    return reply_value;
}


//  ---------------------------------------------------------------------
//  State machine constants

typedef enum {
    start_state = 1,
    before_writing_state = 2,
    writing_state = 3,
    processing_write_state = 4,
    before_reading_state = 5,
    reading_state = 6,
    processing_read_state = 7,
    external_state = 8,
    internal_state = 9
} state_t;

typedef enum {
    terminate_event = -1,
    NULL_event = 0,
    output_event = 1,
    input_event = 2,
    ok_event = 3,
    error_event = 4,
    write_event = 5,
    read_event = 6,
    close_event = 7,
    expired_event = 8,
    have_reader_event = 9,
    wakeup_event = 10,
    read_nothing_event = 11,
    have_data_event = 12,
    not_enough_data_event = 13,
    pipe_terminated_event = 14,
    ping_event = 15
} event_t;

//  Names for animation
static char *
s_state_name [] = {
    "(NONE)",
    "start",
    "before writing",
    "writing",
    "processing write",
    "before reading",
    "reading",
    "processing read",
    "external",
    "internal"
};

static char *
s_event_name [] = {
    "(NONE)",
    "OUTPUT",
    "INPUT",
    "ok",
    "error",
    "WRITE",
    "READ",
    "CLOSE",
    "expired",
    "have reader",
    "wakeup",
    "read nothing",
    "have data",
    "not enough data",
    "pipe terminated",
    "PING"
};
 

//  ---------------------------------------------------------------------
//  Context for the server task. This embeds the application-level
//  server context at its start, so that a pointer to server_t can
//  be cast to s_server_t for our internal use.

typedef struct {
    server_t server;            //  Application-level server context
    zctx_t *ctx;                //  Each thread has its own CZMQ context
    void *pipe;                 //  Socket to back to caller API
    void *router;               //  Socket to talk to clients
    int port;                   //  Server port bound to
    zhash_t *clients;           //  Clients we're connected to
    zconfig_t *config;          //  Configuration tree
    zlog_t *log;                //  Server logger
    uint client_count;          //  Client identifier counter
    size_t timeout;             //  Default client expiry timeout
    size_t monitor;             //  Server monitor interval in msec
    int64_t monitor_at;         //  Next monitor at this time
    bool terminated;            //  Server is shutting down
} s_server_t;


//  ---------------------------------------------------------------------
//  Context for each connected client. This embeds the application-level
//  client context at its start, so that a pointer to client_t can
//  be cast to s_client_t for our internal use.

typedef struct {
    client_t client;            //  Application-level client context
    s_server_t *server;         //  Parent server context
    char *hashkey;              //  Key into server->clients hash
    zframe_t *routing_id;       //  Routing_id back to client
    uint unique_id;             //  Client identifier in server
    zlist_t *mailbox;           //  Incoming messages
    state_t state;              //  Current state
    event_t event;              //  Current event
    event_t next_event;         //  The next event
    event_t exception;          //  Exception event, if any
    int64_t wakeup_at;          //  Wake up at this time
    event_t wakeup_event;       //  Wake up with this event
    int64_t expires_at;         //  Expires at this time
    size_t timeout;             //  Actual connection timeout
} s_client_t;

static void
    s_client_execute (s_client_t *client, int event);
static void
    lookup_or_create_pipe (client_t *self);
static void
    open_pipe_writer (client_t *self);
static void
    open_pipe_reader (client_t *self);
static void
    look_for_pipe_reader (client_t *self);
static void
    close_pipe_writer (client_t *self);
static void
    pass_data_to_reader (client_t *self);
static void
    look_for_pipe_data (client_t *self);
static void
    close_pipe_reader (client_t *self);
static void
    collect_data_to_send (client_t *self);

//  ---------------------------------------------------------------------
//  These methods are an internal API for actions

//  Set the next event, needed in at least one action in an internal
//  state; otherwise the state machine will wait for a message on the
//  router socket and treat that as the event.

static void
set_next_event (client_t *self, event_t event)
{
    if (self)
        ((s_client_t *) self)->next_event = event;
}

//  Raise an exception with 'event', halting any actions in progress.
//  Continues execution of actions defined for the exception event.

static void
raise_exception (client_t *self, event_t event)
{
    if (self)
        ((s_client_t *) self)->exception = (event);
}

//  Set wakeup alarm after 'delay' msecs. The next state should
//  handle the wakeup event. The alarm is cancelled on any other
//  event.

static void
set_wakeup_event (client_t *self, size_t delay, event_t event)
{
    if (self) {
        ((s_client_t *) self)->wakeup_at = zclock_time () + (delay);
        ((s_client_t *) self)->wakeup_event = (event);
    }
}

//  Execute 'event' on specified client. Use this to send events to
//  other clients. Cancels any wakeup alarm on that client.

static void
send_event (client_t *self, event_t event)
{
    if (self)
        s_client_execute ((s_client_t *) self, event);
}


//  Pedantic compilers don't like unused functions
static void
s_satisfy_pedantic_compilers (void)
{
    set_next_event (NULL, 0);
    raise_exception (NULL, 0);
    set_wakeup_event (NULL, 0, 0);
    send_event (NULL, 0);
}


//  ---------------------------------------------------------------------
//  Generic methods on protocol messages

static event_t
s_protocol_event (zpipes_msg_t *request)
{
    assert (request);
    switch (zpipes_msg_id (request)) {
        case ZPIPES_MSG_INPUT:
            return input_event;
            break;
        case ZPIPES_MSG_OUTPUT:
            return output_event;
            break;
        case ZPIPES_MSG_READ:
            return read_event;
            break;
        case ZPIPES_MSG_WRITE:
            return write_event;
            break;
        case ZPIPES_MSG_CLOSE:
            return close_event;
            break;
        case ZPIPES_MSG_PING:
            return ping_event;
            break;
        default:
            //  Invalid zpipes_msg_t
            return terminate_event;
    }
}


//  ---------------------------------------------------------------------
//  Client methods

static s_client_t *
s_client_new (s_server_t *server, zframe_t *routing_id)
{
    s_client_t *self = (s_client_t *) zmalloc (sizeof (s_client_t));
    assert (self);
    assert ((s_client_t *) &self->client == self);
    
    self->server = server;
    self->hashkey = zframe_strhex (routing_id);
    self->routing_id = zframe_dup (routing_id);
    self->mailbox = zlist_new ();
    self->unique_id = ++(server->client_count);
    
    self->client.server = (server_t *) server;
    self->client.reply = zpipes_msg_new (0);
    zpipes_msg_set_routing_id (self->client.reply, self->routing_id);

    //  Give application chance to initialize and set next event
    self->state = start_state;
    self->event = NULL_event;
    client_initialize (&self->client);
    return self;
}

static void
s_client_destroy (s_client_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        s_client_t *self = *self_p;
        zpipes_msg_destroy (&self->client.request);
        zpipes_msg_destroy (&self->client.reply);

        //  Empty and destroy mailbox
        zpipes_msg_t *request = (zpipes_msg_t *) zlist_first (self->mailbox);
        while (request) {
            zpipes_msg_destroy (&request);
            request = (zpipes_msg_t *) zlist_next (self->mailbox);
        }
        zlist_destroy (&self->mailbox);
        zframe_destroy (&self->routing_id);
        client_terminate (&self->client);
        free (self->hashkey);
        free (self);
        *self_p = NULL;
    }
}

//  Callback when we remove client from 'clients' hash table
static void
s_client_free (void *argument)
{
    s_client_t *client = (s_client_t *) argument;
    s_client_destroy (&client);
}

//  Client hash function that calculates tickless timer
static int
s_client_tickless (const char *key, void *client, void *argument)
{
    s_client_t *self = (s_client_t *) client;
    uint64_t *tickless = (uint64_t *) argument;
    if (self->expires_at
    &&  *tickless > self->expires_at)
        *tickless = self->expires_at;
    if (self->wakeup_at
    &&  *tickless > self->wakeup_at)
        *tickless = self->wakeup_at;
    return 0;
}

//  Client hash function to execute timers, if any.
//  This method might be replaced with a sorted list of timers;
//  to be tested & tuned with 10K clients; could be a utility
//  class in CZMQ

static int
s_client_timer (const char *key, void *client, void *argument)
{
    s_client_t *self = (s_client_t *) client;
    
    //  Expire client after timeout seconds of silence
    if (self->expires_at
    &&  self->expires_at <= zclock_time ()) {
        s_client_execute (self, expired_event);
        //  In case dialog doesn't handle expiry by destroying 
        //  client, cancel expiry timer to prevent busy-looping
        self->expires_at = 0;
    }
    else
    if (self->wakeup_at
    &&  self->wakeup_at <= zclock_time ())
        s_client_execute (self, self->wakeup_event);
        
    return 0;
}

//  Do we accept a request off the mailbox? If so, return the event for
//  the message, and load the message into the current client request.
//  If not, return NULL_event;

static event_t
s_client_filter_mailbox (s_client_t *self)
{
    assert (self);
    
    zpipes_msg_t *request = (zpipes_msg_t *) zlist_first (self->mailbox);
    while (request) {
        //  Check whether current state can process event
        //  This should be changed to a pre-built lookup table
        event_t event = s_protocol_event (request);
        bool event_is_valid = false;
        if (self->state == start_state)
            event_is_valid = true;
        if (self->state == writing_state)
            event_is_valid = true;
        if (self->state == processing_write_state && event == close_event)
            event_is_valid = true;
        if (self->state == processing_write_state && event == ping_event)
            event_is_valid = true;
        if (self->state == reading_state)
            event_is_valid = true;
        if (self->state == processing_read_state && event == close_event)
            event_is_valid = true;
        if (self->state == processing_read_state && event == ping_event)
            event_is_valid = true;
        if (self->state == external_state)
            event_is_valid = true;
        if (self->state == internal_state && event == ping_event)
            event_is_valid = true;
        if (event_is_valid) {
            zlist_remove (self->mailbox, request);
            zpipes_msg_destroy (&self->client.request);
            self->client.request = request;
            return event;
        }
        request = (zpipes_msg_t *) zlist_next (self->mailbox);
    }
    return NULL_event;
}


//  Execute state machine as long as we have events

static void
s_client_execute (s_client_t *self, int event)
{
    printf ("\n");              //  Delimit each client block
    self->next_event = event;
    self->wakeup_at = 0;        //  Cancel wakeup alarm if any
    while (self->next_event != NULL_event) {
        self->event = self->next_event;
        self->next_event = NULL_event;
        self->exception = NULL_event;
        zclock_log ("%6d: %s:",
            self->unique_id, s_state_name [self->state]);
        zclock_log ("%6d:     %s",
            self->unique_id, s_event_name [self->event]);

        switch (self->state) {
            case start_state:
                if (self->event == output_event) {
                    if (!self->exception) {
                        //  lookup or create pipe
                        zclock_log ("%6d:         $ lookup or create pipe", self->unique_id);
                        lookup_or_create_pipe (&self->client);
                    }
                    if (!self->exception) {
                        //  open pipe writer
                        zclock_log ("%6d:         $ open pipe writer", self->unique_id);
                        open_pipe_writer (&self->client);
                    }
                    if (!self->exception)
                        self->state = before_writing_state;
                }
                else
                if (self->event == input_event) {
                    if (!self->exception) {
                        //  lookup or create pipe
                        zclock_log ("%6d:         $ lookup or create pipe", self->unique_id);
                        lookup_or_create_pipe (&self->client);
                    }
                    if (!self->exception) {
                        //  open pipe reader
                        zclock_log ("%6d:         $ open pipe reader", self->unique_id);
                        open_pipe_reader (&self->client);
                    }
                    if (!self->exception)
                        self->state = before_reading_state;
                }
                else
                if (self->event == ping_event) {
                    if (!self->exception) {
                        //  send ping_ok
                        zclock_log ("%6d:         $ send PING_OK", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_PING_OK);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                }
                else
                if (self->event == have_reader_event) {
                }
                else
                if (self->event == have_data_event) {
                }
                else
                if (self->event == expired_event) {
                    if (!self->exception) {
                        //  terminate
                        zclock_log ("%6d:         $ terminate", self->unique_id);
                        self->next_event = terminate_event;
                    }
                }
                else {
                    //  Handle unexpected protocol events
                    if (!self->exception) {
                        //  send invalid
                        zclock_log ("%6d:         $ send INVALID", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_INVALID);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception) {
                        //  terminate
                        zclock_log ("%6d:         $ terminate", self->unique_id);
                        self->next_event = terminate_event;
                    }
                }
                break;

            case before_writing_state:
                if (self->event == ok_event) {
                    if (!self->exception) {
                        //  send output_ok
                        zclock_log ("%6d:         $ send OUTPUT_OK", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_OUTPUT_OK);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception)
                        self->state = writing_state;
                }
                else
                if (self->event == error_event) {
                    if (!self->exception) {
                        //  send output_failed
                        zclock_log ("%6d:         $ send OUTPUT_FAILED", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_OUTPUT_FAILED);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception) {
                        //  terminate
                        zclock_log ("%6d:         $ terminate", self->unique_id);
                        self->next_event = terminate_event;
                    }
                }
                else {
                    //  Handle unexpected internal events
                    zlog_warning (self->server->log,
                        "%6d: unhandled event %s in %s",
                        self->unique_id,
                        s_event_name [self->event],
                        s_state_name [self->state]);
                    assert (false);
                }
                break;

            case writing_state:
                if (self->event == write_event) {
                    if (!self->exception) {
                        //  look for pipe reader
                        zclock_log ("%6d:         $ look for pipe reader", self->unique_id);
                        look_for_pipe_reader (&self->client);
                    }
                    if (!self->exception)
                        self->state = processing_write_state;
                }
                else
                if (self->event == read_event) {
                    if (!self->exception) {
                        //  send invalid
                        zclock_log ("%6d:         $ send INVALID", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_INVALID);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception) {
                        //  terminate
                        zclock_log ("%6d:         $ terminate", self->unique_id);
                        self->next_event = terminate_event;
                    }
                }
                else
                if (self->event == close_event) {
                    if (!self->exception) {
                        //  close pipe writer
                        zclock_log ("%6d:         $ close pipe writer", self->unique_id);
                        close_pipe_writer (&self->client);
                    }
                    if (!self->exception) {
                        //  send close_ok
                        zclock_log ("%6d:         $ send CLOSE_OK", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_CLOSE_OK);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception)
                        self->state = start_state;
                }
                else
                if (self->event == expired_event) {
                    if (!self->exception) {
                        //  close pipe writer
                        zclock_log ("%6d:         $ close pipe writer", self->unique_id);
                        close_pipe_writer (&self->client);
                    }
                    if (!self->exception) {
                        //  terminate
                        zclock_log ("%6d:         $ terminate", self->unique_id);
                        self->next_event = terminate_event;
                    }
                }
                else
                if (self->event == ping_event) {
                    if (!self->exception) {
                        //  send ping_ok
                        zclock_log ("%6d:         $ send PING_OK", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_PING_OK);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                }
                else
                if (self->event == have_reader_event) {
                }
                else
                if (self->event == have_data_event) {
                }
                else
                if (self->event == expired_event) {
                    if (!self->exception) {
                        //  terminate
                        zclock_log ("%6d:         $ terminate", self->unique_id);
                        self->next_event = terminate_event;
                    }
                }
                else {
                    //  Handle unexpected protocol events
                    if (!self->exception) {
                        //  send invalid
                        zclock_log ("%6d:         $ send INVALID", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_INVALID);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception) {
                        //  terminate
                        zclock_log ("%6d:         $ terminate", self->unique_id);
                        self->next_event = terminate_event;
                    }
                }
                break;

            case processing_write_state:
                if (self->event == have_reader_event) {
                    if (!self->exception) {
                        //  pass data to reader
                        zclock_log ("%6d:         $ pass data to reader", self->unique_id);
                        pass_data_to_reader (&self->client);
                    }
                    if (!self->exception) {
                        //  send write_ok
                        zclock_log ("%6d:         $ send WRITE_OK", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_WRITE_OK);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception)
                        self->state = writing_state;
                }
                else
                if (self->event == wakeup_event) {
                    if (!self->exception) {
                        //  send write_timeout
                        zclock_log ("%6d:         $ send WRITE_TIMEOUT", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_WRITE_TIMEOUT);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception)
                        self->state = writing_state;
                }
                else
                if (self->event == close_event) {
                    if (!self->exception) {
                        //  send write_failed
                        zclock_log ("%6d:         $ send WRITE_FAILED", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_WRITE_FAILED);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception) {
                        //  close pipe writer
                        zclock_log ("%6d:         $ close pipe writer", self->unique_id);
                        close_pipe_writer (&self->client);
                    }
                    if (!self->exception) {
                        //  send close_ok
                        zclock_log ("%6d:         $ send CLOSE_OK", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_CLOSE_OK);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception)
                        self->state = start_state;
                }
                else
                if (self->event == expired_event) {
                    if (!self->exception) {
                        //  close pipe writer
                        zclock_log ("%6d:         $ close pipe writer", self->unique_id);
                        close_pipe_writer (&self->client);
                    }
                    if (!self->exception) {
                        //  terminate
                        zclock_log ("%6d:         $ terminate", self->unique_id);
                        self->next_event = terminate_event;
                    }
                }
                else
                if (self->event == ping_event) {
                    if (!self->exception) {
                        //  send ping_ok
                        zclock_log ("%6d:         $ send PING_OK", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_PING_OK);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                }
                else {
                    //  Handle unexpected internal events
                    zlog_warning (self->server->log,
                        "%6d: unhandled event %s in %s",
                        self->unique_id,
                        s_event_name [self->event],
                        s_state_name [self->state]);
                    assert (false);
                }
                break;

            case before_reading_state:
                if (self->event == ok_event) {
                    if (!self->exception) {
                        //  send input_ok
                        zclock_log ("%6d:         $ send INPUT_OK", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_INPUT_OK);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception)
                        self->state = reading_state;
                }
                else
                if (self->event == error_event) {
                    if (!self->exception) {
                        //  send input_failed
                        zclock_log ("%6d:         $ send INPUT_FAILED", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_INPUT_FAILED);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception) {
                        //  terminate
                        zclock_log ("%6d:         $ terminate", self->unique_id);
                        self->next_event = terminate_event;
                    }
                }
                else {
                    //  Handle unexpected internal events
                    zlog_warning (self->server->log,
                        "%6d: unhandled event %s in %s",
                        self->unique_id,
                        s_event_name [self->event],
                        s_state_name [self->state]);
                    assert (false);
                }
                break;

            case reading_state:
                if (self->event == read_event) {
                    if (!self->exception) {
                        //  look for pipe data
                        zclock_log ("%6d:         $ look for pipe data", self->unique_id);
                        look_for_pipe_data (&self->client);
                    }
                    if (!self->exception)
                        self->state = processing_read_state;
                }
                else
                if (self->event == read_nothing_event) {
                    if (!self->exception) {
                        //  send read_end
                        zclock_log ("%6d:         $ send READ_END", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_READ_END);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                }
                else
                if (self->event == write_event) {
                    if (!self->exception) {
                        //  send invalid
                        zclock_log ("%6d:         $ send INVALID", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_INVALID);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception) {
                        //  terminate
                        zclock_log ("%6d:         $ terminate", self->unique_id);
                        self->next_event = terminate_event;
                    }
                }
                else
                if (self->event == close_event) {
                    if (!self->exception) {
                        //  close pipe reader
                        zclock_log ("%6d:         $ close pipe reader", self->unique_id);
                        close_pipe_reader (&self->client);
                    }
                    if (!self->exception) {
                        //  send close_ok
                        zclock_log ("%6d:         $ send CLOSE_OK", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_CLOSE_OK);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception)
                        self->state = start_state;
                }
                else
                if (self->event == expired_event) {
                    if (!self->exception) {
                        //  close pipe reader
                        zclock_log ("%6d:         $ close pipe reader", self->unique_id);
                        close_pipe_reader (&self->client);
                    }
                    if (!self->exception) {
                        //  terminate
                        zclock_log ("%6d:         $ terminate", self->unique_id);
                        self->next_event = terminate_event;
                    }
                }
                else
                if (self->event == ping_event) {
                    if (!self->exception) {
                        //  send ping_ok
                        zclock_log ("%6d:         $ send PING_OK", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_PING_OK);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                }
                else
                if (self->event == have_reader_event) {
                }
                else
                if (self->event == have_data_event) {
                }
                else
                if (self->event == expired_event) {
                    if (!self->exception) {
                        //  terminate
                        zclock_log ("%6d:         $ terminate", self->unique_id);
                        self->next_event = terminate_event;
                    }
                }
                else {
                    //  Handle unexpected protocol events
                    if (!self->exception) {
                        //  send invalid
                        zclock_log ("%6d:         $ send INVALID", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_INVALID);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception) {
                        //  terminate
                        zclock_log ("%6d:         $ terminate", self->unique_id);
                        self->next_event = terminate_event;
                    }
                }
                break;

            case processing_read_state:
                if (self->event == have_data_event) {
                    if (!self->exception) {
                        //  collect data to send
                        zclock_log ("%6d:         $ collect data to send", self->unique_id);
                        collect_data_to_send (&self->client);
                    }
                    if (!self->exception) {
                        //  send read_ok
                        zclock_log ("%6d:         $ send READ_OK", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_READ_OK);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception)
                        self->state = reading_state;
                }
                else
                if (self->event == not_enough_data_event) {
                }
                else
                if (self->event == pipe_terminated_event) {
                    if (!self->exception) {
                        //  send read_end
                        zclock_log ("%6d:         $ send READ_END", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_READ_END);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception)
                        self->state = reading_state;
                }
                else
                if (self->event == wakeup_event) {
                    if (!self->exception) {
                        //  send read_timeout
                        zclock_log ("%6d:         $ send READ_TIMEOUT", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_READ_TIMEOUT);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception)
                        self->state = reading_state;
                }
                else
                if (self->event == close_event) {
                    if (!self->exception) {
                        //  send read_failed
                        zclock_log ("%6d:         $ send READ_FAILED", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_READ_FAILED);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception) {
                        //  close pipe reader
                        zclock_log ("%6d:         $ close pipe reader", self->unique_id);
                        close_pipe_reader (&self->client);
                    }
                    if (!self->exception) {
                        //  send close_ok
                        zclock_log ("%6d:         $ send CLOSE_OK", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_CLOSE_OK);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception)
                        self->state = start_state;
                }
                else
                if (self->event == expired_event) {
                    if (!self->exception) {
                        //  close pipe reader
                        zclock_log ("%6d:         $ close pipe reader", self->unique_id);
                        close_pipe_reader (&self->client);
                    }
                    if (!self->exception) {
                        //  terminate
                        zclock_log ("%6d:         $ terminate", self->unique_id);
                        self->next_event = terminate_event;
                    }
                }
                else
                if (self->event == ping_event) {
                    if (!self->exception) {
                        //  send ping_ok
                        zclock_log ("%6d:         $ send PING_OK", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_PING_OK);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                }
                else {
                    //  Handle unexpected internal events
                    zlog_warning (self->server->log,
                        "%6d: unhandled event %s in %s",
                        self->unique_id,
                        s_event_name [self->event],
                        s_state_name [self->state]);
                    assert (false);
                }
                break;

            case external_state:
                if (self->event == ping_event) {
                    if (!self->exception) {
                        //  send ping_ok
                        zclock_log ("%6d:         $ send PING_OK", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_PING_OK);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                }
                else
                if (self->event == have_reader_event) {
                }
                else
                if (self->event == have_data_event) {
                }
                else
                if (self->event == expired_event) {
                    if (!self->exception) {
                        //  terminate
                        zclock_log ("%6d:         $ terminate", self->unique_id);
                        self->next_event = terminate_event;
                    }
                }
                else {
                    //  Handle unexpected protocol events
                    if (!self->exception) {
                        //  send invalid
                        zclock_log ("%6d:         $ send INVALID", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_INVALID);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception) {
                        //  terminate
                        zclock_log ("%6d:         $ terminate", self->unique_id);
                        self->next_event = terminate_event;
                    }
                }
                break;

            case internal_state:
                if (self->event == ping_event) {
                    if (!self->exception) {
                        //  send ping_ok
                        zclock_log ("%6d:         $ send PING_OK", self->unique_id);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_PING_OK);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                }
                else {
                    //  Handle unexpected internal events
                    zlog_warning (self->server->log,
                        "%6d: unhandled event %s in %s",
                        self->unique_id,
                        s_event_name [self->event],
                        s_state_name [self->state]);
                    assert (false);
                }
                break;
        }
        //  If we had an exception event, interrupt normal programming
        if (self->exception) {
            zclock_log ("%6d:         ! %s",
                self->unique_id, s_event_name [self->exception]);
            self->next_event = self->exception;
        }
        if (self->next_event == terminate_event) {
            //  Automatically calls s_client_destroy
            zhash_delete (self->server->clients, self->hashkey);
            break;
        }
        else {
            zclock_log ("%6d:         > %s",
                self->unique_id, s_state_name [self->state]);
            if (self->next_event == NULL_event)
                //  Get next valid message from mailbox, if any
                self->next_event = s_client_filter_mailbox (self);
        }
    }
}


//  Server methods

static void
s_server_config_self (s_server_t *self)
{
    //  Built-in server configuration options
    //  
    //  Default client timeout is 60 seconds, if state machine defines
    //  an expired event; otherwise there is no timeout.
    self->timeout = atoi (
        zconfig_resolve (self->config, "server/timeout", "60000"));

    //  Monitor the server every this often, if the state machine defines
    //  a server-monitor event.
    self->monitor = atoi (
        zconfig_resolve (self->config, "server/monitor", "5"));
    self->monitor_at = zclock_time () + self->monitor;

    //  Do we want to run server in the background?
    int background = atoi (
        zconfig_resolve (self->config, "server/background", "0"));
    if (!background)
        zlog_set_foreground (self->log, true);
}

static s_server_t *
s_server_new (zctx_t *ctx, void *pipe)
{
    s_server_t *self = (s_server_t *) zmalloc (sizeof (s_server_t));
    assert (self);
    assert ((s_server_t *) &self->server == self);
    server_initialize (&self->server);
    
    self->ctx = ctx;
    self->pipe = pipe;
    self->router = zsocket_new (self->ctx, ZMQ_ROUTER);
    self->clients = zhash_new ();
    self->log = zlog_new ("zpipes_server");
    self->config = zconfig_new ("root", NULL);
    s_server_config_self (self);

    s_satisfy_pedantic_compilers ();
    return self;
}

static void
s_server_destroy (s_server_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        s_server_t *self = *self_p;
        server_terminate (&self->server);
        zsocket_destroy (self->ctx, self->router);
        zconfig_destroy (&self->config);
        zhash_destroy (&self->clients);
        free (self);
        *self_p = NULL;
    }
}

//  Apply configuration tree:
//   * apply server configuration
//   * print any echo items in top-level sections
//   * apply sections that match methods

static void
s_server_apply_config (s_server_t *self)
{
    //  Apply echo commands and class methods
    zconfig_t *section = zconfig_locate (self->config, "zpipes_server");
    if (section)
        section = zconfig_child (section);

    while (section) {
        zconfig_t *entry = zconfig_child (section);
        while (entry) {
            if (streq (zconfig_name (entry), "echo"))
                zlog_notice (self->log, "%s", zconfig_value (entry));
            entry = zconfig_next (entry);
        }
        if (streq (zconfig_name (section), "bind")) {
            char *endpoint = zconfig_resolve (section, "endpoint", "?");
            self->port = zsocket_bind (self->router, "%s", endpoint);
        }
        section = zconfig_next (section);
    }
    s_server_config_self (self);
}

//  Process message from pipe
static void
s_server_control_message (s_server_t *self)
{
    zmsg_t *msg = zmsg_recv (self->pipe);
    char *method = zmsg_popstr (msg);
    if (streq (method, "BIND")) {
        char *endpoint = zmsg_popstr (msg);
        self->port = zsocket_bind (self->router, "%s", endpoint);
        zstr_sendf (self->pipe, "%d", self->port);
        free (endpoint);
    }
    else
    if (streq (method, "CONFIGURE")) {
        char *config_file = zmsg_popstr (msg);
        zconfig_destroy (&self->config);
        self->config = zconfig_load (config_file);
        if (self->config)
            s_server_apply_config (self);
        else {
            zlog_warning (self->log,
                "cannot load config file '%s'\n", config_file);
            self->config = zconfig_new ("root", NULL);
        }
        free (config_file);
    }
    else
    if (streq (method, "SET")) {
        char *path = zmsg_popstr (msg);
        char *value = zmsg_popstr (msg);
        zconfig_put (self->config, path, value);
        s_server_config_self (self);
        free (path);
        free (value);
    }
    else
    if (streq (method, "TERMINATE")) {
        zstr_send (self->pipe, "OK");
        self->terminated = true;
    }
    free (method);
    zmsg_destroy (&msg);
}

//  Handle a message (a protocol request) from the client

static void
s_server_client_message (s_server_t *self)
{
    zpipes_msg_t *request = zpipes_msg_recv (self->router);
    if (!request)
        return;         //  Interrupted; do nothing

    char *hashkey = zframe_strhex (zpipes_msg_routing_id (request));
    s_client_t *client = (s_client_t *) zhash_lookup (self->clients, hashkey);
    if (client == NULL) {
        client = s_client_new (self, zpipes_msg_routing_id (request));
        zhash_insert (self->clients, hashkey, client);
        zhash_freefn (self->clients, hashkey, s_client_free);
    }
    free (hashkey);

    //  Any input from client counts as activity
    client->expires_at = zclock_time () + self->timeout;

    //  Queue request and possibly pass it to client state machine
    zlist_append (client->mailbox, request);
    event_t event = s_client_filter_mailbox (client);
    if (event != NULL_event)
        s_client_execute (client, event);
}


//  Finally here's the server thread itself, which polls its two
//  sockets and processes incoming messages
static void
s_server_task (void *args, zctx_t *ctx, void *pipe)
{
    s_server_t *self = s_server_new (ctx, pipe);
    assert (self);
    zlog_notice (self->log, "Starting zpipes_server service");
    zstr_send (self->pipe, "OK");

    zmq_pollitem_t items [] = {
        { self->pipe, 0, ZMQ_POLLIN, 0 },
        { self->router, 0, ZMQ_POLLIN, 0 }
    };
    self->monitor_at = zclock_time () + self->monitor;
    while (!self->terminated && !zctx_interrupted) {
        //  Calculate tickless timer, up to monitor time
        uint64_t tickless = zclock_time () + self->monitor;
        zhash_foreach (self->clients, s_client_tickless, &tickless);

        //  Poll until at most next timer event
        int rc = zmq_poll (items, 2,
            (tickless - zclock_time ()) * ZMQ_POLL_MSEC);
        if (rc == -1)
            break;              //  Context has been shut down

        //  Process incoming message from either socket
        if (items [0].revents & ZMQ_POLLIN)
            s_server_control_message (self);

        if (items [1].revents & ZMQ_POLLIN)
            s_server_client_message (self);

        //  Execute client timer events
        zhash_foreach (self->clients, s_client_timer, NULL);
        
        //  If clock went past timeout, then monitor server
        if (zclock_time () >= self->monitor_at) {
            self->monitor_at = zclock_time () + self->monitor;
        }
    }
    zlog_notice (self->log, "Terminating zpipes_server service");
    s_server_destroy (&self);
}
