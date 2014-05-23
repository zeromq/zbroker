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
    reader_dropped_event = 9,
    have_reader_event = 10,
    wakeup_event = 11,
    pipe_shut_event = 12,
    writer_dropped_event = 13,
    have_data_event = 14,
    not_enough_data_event = 15,
    zero_read_event = 16,
    ping_event = 17,
    have_writer_event = 18
} event_t;

//  Names for animation and error reporting
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
    "reader dropped",
    "have reader",
    "wakeup",
    "pipe shut",
    "writer dropped",
    "have data",
    "not enough data",
    "zero read",
    "PING",
    "have writer"
};
 

//  ---------------------------------------------------------------------
//  Context for the whole server task. This embeds the application-level
//  server context at its start (the entire structure, not a reference),
//  so we can cast a pointer between server_t and s_server_t arbitrarily.

typedef struct {
    server_t server;            //  Application-level server context
    zsock_t *pipe;              //  Socket to back to caller API
    zsock_t *router;            //  Socket to talk to clients
    int port;                   //  Server port bound to
    zloop_t *loop;              //  Reactor for server sockets
    zhash_t *clients;           //  Clients we're connected to
    zconfig_t *config;          //  Configuration tree
    zlog_t *log;                //  Server logger
    uint client_id;             //  Client identifier counter
    size_t timeout;             //  Default client expiry timeout
    bool animate;               //  Is animation enabled?
} s_server_t;


//  ---------------------------------------------------------------------
//  Context for each connected client. This embeds the application-level
//  client context at its start (the entire structure, not a reference),
//  so we can cast a pointer between client_t and s_client_t arbitrarily.

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
    int expiry_timer;           //  zloop timer for client timeouts
    int wakeup_timer;           //  zloop timer for client alarms
    event_t wakeup_event;       //  Wake up with this event
    char log_prefix [31];       //  Log prefix string
} s_client_t;

static int
    server_initialize (server_t *self);
static void
    server_terminate (server_t *self);
static zmsg_t *
    server_method (server_t *self, const char *method, zmsg_t *msg);
static int
    client_initialize (client_t *self);
static void
    client_terminate (client_t *self);
static void
    s_client_execute (s_client_t *client, int event);
static int
    s_client_wakeup (zloop_t *loop, int timer_id, void *argument);
static void
    lookup_or_create_pipe (client_t *self);
static void
    open_pipe_writer (client_t *self);
static void
    open_pipe_reader (client_t *self);
static void
    process_write_request (client_t *self);
static void
    close_pipe_writer (client_t *self);
static void
    pass_data_to_reader (client_t *self);
static void
    process_read_request (client_t *self);
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
engine_set_next_event (client_t *client, event_t event)
{
    if (client) {
        s_client_t *self = (s_client_t *) client;
        self->next_event = event;
    }
}

//  Raise an exception with 'event', halting any actions in progress.
//  Continues execution of actions defined for the exception event.

static void
engine_set_exception (client_t *client, event_t event)
{
    if (client) {
        s_client_t *self = (s_client_t *) client;
        self->exception = event;
    }
}

//  Set wakeup alarm after 'delay' msecs. The next state should
//  handle the wakeup event. The alarm is cancelled on any other
//  event.

static void
engine_set_wakeup_event (client_t *client, size_t delay, event_t event)
{
    if (client) {
        s_client_t *self = (s_client_t *) client;
        if (self->wakeup_timer) {
            zloop_timer_end (self->server->loop, self->wakeup_timer);
            self->wakeup_timer = 0;
        }
        self->wakeup_timer = zloop_timer (
            self->server->loop, delay, 1, s_client_wakeup, self);
        self->wakeup_event = event;
    }
}

//  Execute 'event' on specified client. Use this to send events to
//  other clients. Cancels any wakeup alarm on that client.

static void
engine_send_event (client_t *client, event_t event)
{
    if (client) {
        s_client_t *self = (s_client_t *) client;
        s_client_execute (self, event);
    }
}

//  Poll socket for activity, invoke handler on any received message.
//  Handler must be a CZMQ zloop_fn function; receives server as arg.

static void
engine_handle_socket (server_t *server, zsock_t *socket, zloop_reader_fn handler)
{
    if (server) {
        s_server_t *self = (s_server_t *) server;
        if (handler) {
            int rc = zloop_reader (self->loop, socket, handler, self);
            assert (rc == 0);
            zloop_reader_set_tolerant (self->loop, socket);
        }
        else
            zloop_reader_end (self->loop, socket);
    }
}

//  Register monitor function that will be called at regular intervals
//  by the server engine

static void
engine_set_monitor (server_t *server, size_t interval, zloop_timer_fn monitor)
{
    if (server) {
        s_server_t *self = (s_server_t *) server;
        int rc = zloop_timer (self->loop, interval, 0, monitor, self);
        assert (rc >= 0);
    }
}

//  Send log data for a specific client to the server log. Accepts a
//  printf format.

static void
engine_log (client_t *client, const char *format, ...)
{
    if (client) {
        s_client_t *self = (s_client_t *) client;
        va_list argptr;
        va_start (argptr, format);
        char *string = zsys_vprintf (format, argptr);
        va_end (argptr);
        zlog_debug (self->server->log, "%s: %s", self->log_prefix, string);
        free (string);
    }
}

//  Send log data to the server log. Accepts a printf format.

static void
engine_server_log (server_t *server, const char *format, ...)
{
    if (server) {
        s_server_t *self = (s_server_t *) server;
        va_list argptr;
        va_start (argptr, format);
        char *string = zsys_vprintf (format, argptr);
        va_end (argptr);
        zlog_debug (self->log, "%s", string);
        free (string);
    }
}

//  Set log file prefix; this string will be added to log data, to make
//  log data more searchable. The string is truncated to ~20 chars.

static void
engine_set_log_prefix (client_t *client, const char *string)
{
    if (client) {
        s_client_t *self = (s_client_t *) client;
        snprintf (self->log_prefix, sizeof (self->log_prefix) - 1,
            "%6d:%-20s", self->unique_id, string);
    }
}

//  Pedantic compilers don't like unused functions, so we call the whole
//  API, passing null references. It's nasty and horrid and sufficient.

static void
s_satisfy_pedantic_compilers (void)
{
    engine_set_next_event (NULL, 0);
    engine_set_exception (NULL, 0);
    engine_set_wakeup_event (NULL, 0, 0);
    engine_send_event (NULL, 0);
    engine_handle_socket (NULL, 0, NULL);
    engine_set_monitor (NULL, 0, NULL);
    engine_log (NULL, NULL);
    engine_server_log (NULL, NULL);
    engine_set_log_prefix (NULL, NULL);
}


//  ---------------------------------------------------------------------
//  Generic methods on protocol messages
//  TODO: replace with lookup table, since ID is one byte

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
    self->unique_id = server->client_id++;
    engine_set_log_prefix (&self->client, "");

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
        
        if (self->wakeup_timer)
            zloop_timer_end (self->server->loop, self->wakeup_timer);
        if (self->expiry_timer)
            zloop_timer_end (self->server->loop, self->expiry_timer);

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
        bool event_is_valid;
        if (self->state == start_state)
            event_is_valid = true;
        else
        if (self->state == writing_state)
            event_is_valid = true;
        else
        if (self->state == processing_write_state && event == close_event)
            event_is_valid = true;
        else
        if (self->state == processing_write_state && event == ping_event)
            event_is_valid = true;
        else
        if (self->state == reading_state)
            event_is_valid = true;
        else
        if (self->state == processing_read_state && event == close_event)
            event_is_valid = true;
        else
        if (self->state == processing_read_state && event == ping_event)
            event_is_valid = true;
        else
        if (self->state == external_state)
            event_is_valid = true;
        else
        if (self->state == internal_state && event == ping_event)
            event_is_valid = true;
        else
            event_is_valid = false;
            
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
    self->next_event = event;
    //  Cancel wakeup timer, if any was pending
    if (self->wakeup_timer) {
        zloop_timer_end (self->server->loop, self->wakeup_timer);
        self->wakeup_timer = 0;
    }
    while (self->next_event != NULL_event) {
        self->event = self->next_event;
        self->next_event = NULL_event;
        self->exception = NULL_event;
        if (self->server->animate) {
            zlog_debug (self->server->log,
                "%s: %s:", self->log_prefix, s_state_name [self->state]);
            zlog_debug (self->server->log,
                "%s:     %s", self->log_prefix, s_event_name [self->event]);
        }
        switch (self->state) {
            case start_state:
                if (self->event == output_event) {
                    if (!self->exception) {
                        //  lookup or create pipe
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ lookup or create pipe", self->log_prefix);
                        lookup_or_create_pipe (&self->client);
                    }
                    if (!self->exception) {
                        //  open pipe writer
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ open pipe writer", self->log_prefix);
                        open_pipe_writer (&self->client);
                    }
                    if (!self->exception)
                        self->state = before_writing_state;
                }
                else
                if (self->event == input_event) {
                    if (!self->exception) {
                        //  lookup or create pipe
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ lookup or create pipe", self->log_prefix);
                        lookup_or_create_pipe (&self->client);
                    }
                    if (!self->exception) {
                        //  open pipe reader
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ open pipe reader", self->log_prefix);
                        open_pipe_reader (&self->client);
                    }
                    if (!self->exception)
                        self->state = before_reading_state;
                }
                else
                if (self->event == ping_event) {
                    if (!self->exception) {
                        //  send ping_ok
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send PING_OK", self->log_prefix);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_PING_OK);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                }
                else
                if (self->event == have_writer_event) {
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
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ terminate", self->log_prefix);
                        self->next_event = terminate_event;
                    }
                }
                else {
                    //  Handle unexpected protocol events
                    if (!self->exception) {
                        //  send invalid
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send INVALID", self->log_prefix);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_INVALID);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception) {
                        //  terminate
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ terminate", self->log_prefix);
                        self->next_event = terminate_event;
                    }
                }
                break;

            case before_writing_state:
                if (self->event == ok_event) {
                    if (!self->exception) {
                        //  send output_ok
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send OUTPUT_OK", self->log_prefix);
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
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send OUTPUT_FAILED", self->log_prefix);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_OUTPUT_FAILED);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception) {
                        //  terminate
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ terminate", self->log_prefix);
                        self->next_event = terminate_event;
                    }
                }
                else {
                    //  Handle unexpected internal events
                    zlog_warning (self->server->log, "%s: unhandled event %s in %s",
                        self->log_prefix,
                        s_event_name [self->event],
                        s_state_name [self->state]);
                    assert (false);
                }
                break;

            case writing_state:
                if (self->event == write_event) {
                    if (!self->exception) {
                        //  process write request
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ process write request", self->log_prefix);
                        process_write_request (&self->client);
                    }
                    if (!self->exception)
                        self->state = processing_write_state;
                }
                else
                if (self->event == read_event) {
                    if (!self->exception) {
                        //  send invalid
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send INVALID", self->log_prefix);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_INVALID);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                }
                else
                if (self->event == close_event) {
                    if (!self->exception) {
                        //  close pipe writer
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ close pipe writer", self->log_prefix);
                        close_pipe_writer (&self->client);
                    }
                    if (!self->exception) {
                        //  send close_ok
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send CLOSE_OK", self->log_prefix);
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
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ close pipe writer", self->log_prefix);
                        close_pipe_writer (&self->client);
                    }
                    if (!self->exception) {
                        //  terminate
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ terminate", self->log_prefix);
                        self->next_event = terminate_event;
                    }
                }
                else
                if (self->event == reader_dropped_event) {
                    if (!self->exception) {
                        //  close pipe writer
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ close pipe writer", self->log_prefix);
                        close_pipe_writer (&self->client);
                    }
                }
                else
                if (self->event == ping_event) {
                    if (!self->exception) {
                        //  send ping_ok
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send PING_OK", self->log_prefix);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_PING_OK);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                }
                else
                if (self->event == have_writer_event) {
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
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ terminate", self->log_prefix);
                        self->next_event = terminate_event;
                    }
                }
                else {
                    //  Handle unexpected protocol events
                    if (!self->exception) {
                        //  send invalid
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send INVALID", self->log_prefix);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_INVALID);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception) {
                        //  terminate
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ terminate", self->log_prefix);
                        self->next_event = terminate_event;
                    }
                }
                break;

            case processing_write_state:
                if (self->event == have_reader_event) {
                    if (!self->exception) {
                        //  pass data to reader
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ pass data to reader", self->log_prefix);
                        pass_data_to_reader (&self->client);
                    }
                    if (!self->exception) {
                        //  send write_ok
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send WRITE_OK", self->log_prefix);
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
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send WRITE_TIMEOUT", self->log_prefix);
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
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send WRITE_FAILED", self->log_prefix);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_WRITE_FAILED);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception) {
                        //  close pipe writer
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ close pipe writer", self->log_prefix);
                        close_pipe_writer (&self->client);
                    }
                    if (!self->exception) {
                        //  send close_ok
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send CLOSE_OK", self->log_prefix);
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
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ close pipe writer", self->log_prefix);
                        close_pipe_writer (&self->client);
                    }
                    if (!self->exception) {
                        //  terminate
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ terminate", self->log_prefix);
                        self->next_event = terminate_event;
                    }
                }
                else
                if (self->event == pipe_shut_event) {
                    if (!self->exception) {
                        //  send write_failed
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send WRITE_FAILED", self->log_prefix);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_WRITE_FAILED);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception)
                        self->state = writing_state;
                }
                else
                if (self->event == reader_dropped_event) {
                    if (!self->exception) {
                        //  close pipe writer
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ close pipe writer", self->log_prefix);
                        close_pipe_writer (&self->client);
                    }
                }
                else
                if (self->event == ping_event) {
                    if (!self->exception) {
                        //  send ping_ok
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send PING_OK", self->log_prefix);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_PING_OK);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                }
                else {
                    //  Handle unexpected internal events
                    zlog_warning (self->server->log, "%s: unhandled event %s in %s",
                        self->log_prefix,
                        s_event_name [self->event],
                        s_state_name [self->state]);
                    assert (false);
                }
                break;

            case before_reading_state:
                if (self->event == ok_event) {
                    if (!self->exception) {
                        //  send input_ok
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send INPUT_OK", self->log_prefix);
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
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send INPUT_FAILED", self->log_prefix);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_INPUT_FAILED);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception) {
                        //  terminate
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ terminate", self->log_prefix);
                        self->next_event = terminate_event;
                    }
                }
                else {
                    //  Handle unexpected internal events
                    zlog_warning (self->server->log, "%s: unhandled event %s in %s",
                        self->log_prefix,
                        s_event_name [self->event],
                        s_state_name [self->state]);
                    assert (false);
                }
                break;

            case reading_state:
                if (self->event == read_event) {
                    if (!self->exception) {
                        //  process read request
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ process read request", self->log_prefix);
                        process_read_request (&self->client);
                    }
                    if (!self->exception)
                        self->state = processing_read_state;
                }
                else
                if (self->event == write_event) {
                    if (!self->exception) {
                        //  send invalid
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send INVALID", self->log_prefix);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_INVALID);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                }
                else
                if (self->event == close_event) {
                    if (!self->exception) {
                        //  close pipe reader
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ close pipe reader", self->log_prefix);
                        close_pipe_reader (&self->client);
                    }
                    if (!self->exception) {
                        //  send close_ok
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send CLOSE_OK", self->log_prefix);
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
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ close pipe reader", self->log_prefix);
                        close_pipe_reader (&self->client);
                    }
                    if (!self->exception) {
                        //  terminate
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ terminate", self->log_prefix);
                        self->next_event = terminate_event;
                    }
                }
                else
                if (self->event == writer_dropped_event) {
                    if (!self->exception) {
                        //  close pipe reader
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ close pipe reader", self->log_prefix);
                        close_pipe_reader (&self->client);
                    }
                }
                else
                if (self->event == ping_event) {
                    if (!self->exception) {
                        //  send ping_ok
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send PING_OK", self->log_prefix);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_PING_OK);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                }
                else
                if (self->event == have_writer_event) {
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
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ terminate", self->log_prefix);
                        self->next_event = terminate_event;
                    }
                }
                else {
                    //  Handle unexpected protocol events
                    if (!self->exception) {
                        //  send invalid
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send INVALID", self->log_prefix);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_INVALID);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception) {
                        //  terminate
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ terminate", self->log_prefix);
                        self->next_event = terminate_event;
                    }
                }
                break;

            case processing_read_state:
                if (self->event == have_data_event) {
                    if (!self->exception) {
                        //  collect data to send
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ collect data to send", self->log_prefix);
                        collect_data_to_send (&self->client);
                    }
                    if (!self->exception) {
                        //  send read_ok
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send READ_OK", self->log_prefix);
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
                if (self->event == zero_read_event) {
                    if (!self->exception) {
                        //  send read_end
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send READ_END", self->log_prefix);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_READ_END);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception)
                        self->state = reading_state;
                }
                else
                if (self->event == pipe_shut_event) {
                    if (!self->exception) {
                        //  send read_end
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send READ_END", self->log_prefix);
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
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send READ_TIMEOUT", self->log_prefix);
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
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send READ_FAILED", self->log_prefix);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_READ_FAILED);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception) {
                        //  close pipe reader
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ close pipe reader", self->log_prefix);
                        close_pipe_reader (&self->client);
                    }
                    if (!self->exception) {
                        //  send close_ok
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send CLOSE_OK", self->log_prefix);
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
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ close pipe reader", self->log_prefix);
                        close_pipe_reader (&self->client);
                    }
                    if (!self->exception) {
                        //  terminate
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ terminate", self->log_prefix);
                        self->next_event = terminate_event;
                    }
                }
                else
                if (self->event == writer_dropped_event) {
                    if (!self->exception) {
                        //  close pipe reader
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ close pipe reader", self->log_prefix);
                        close_pipe_reader (&self->client);
                    }
                    if (!self->exception) {
                        //  collect data to send
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ collect data to send", self->log_prefix);
                        collect_data_to_send (&self->client);
                    }
                    if (!self->exception) {
                        //  send read_ok
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send READ_OK", self->log_prefix);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_READ_OK);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception)
                        self->state = reading_state;
                }
                else
                if (self->event == ping_event) {
                    if (!self->exception) {
                        //  send ping_ok
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send PING_OK", self->log_prefix);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_PING_OK);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                }
                else {
                    //  Handle unexpected internal events
                    zlog_warning (self->server->log, "%s: unhandled event %s in %s",
                        self->log_prefix,
                        s_event_name [self->event],
                        s_state_name [self->state]);
                    assert (false);
                }
                break;

            case external_state:
                if (self->event == ping_event) {
                    if (!self->exception) {
                        //  send ping_ok
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send PING_OK", self->log_prefix);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_PING_OK);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                }
                else
                if (self->event == have_writer_event) {
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
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ terminate", self->log_prefix);
                        self->next_event = terminate_event;
                    }
                }
                else {
                    //  Handle unexpected protocol events
                    if (!self->exception) {
                        //  send invalid
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send INVALID", self->log_prefix);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_INVALID);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                    if (!self->exception) {
                        //  terminate
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ terminate", self->log_prefix);
                        self->next_event = terminate_event;
                    }
                }
                break;

            case internal_state:
                if (self->event == ping_event) {
                    if (!self->exception) {
                        //  send ping_ok
                        if (self->server->animate)
                            zlog_debug (self->server->log,
                                "%s:         $ send PING_OK", self->log_prefix);
                        zpipes_msg_set_id (self->client.reply, ZPIPES_MSG_PING_OK);
                        zpipes_msg_send (&self->client.reply, self->server->router);
                        self->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
                    }
                }
                else {
                    //  Handle unexpected internal events
                    zlog_warning (self->server->log, "%s: unhandled event %s in %s",
                        self->log_prefix,
                        s_event_name [self->event],
                        s_state_name [self->state]);
                    assert (false);
                }
                break;
        }
        //  If we had an exception event, interrupt normal programming
        if (self->exception) {
            if (self->server->animate)
                zlog_debug (self->server->log,
                    "%s:         ! %s",
                    self->log_prefix, s_event_name [self->exception]);

            self->next_event = self->exception;
        }
        if (self->next_event == terminate_event) {
            //  Automatically calls s_client_destroy
            zhash_delete (self->server->clients, self->hashkey);
            break;
        }
        else {
            if (self->server->animate)
                zlog_debug (self->server->log,
                    "%s:         > %s",
                    self->log_prefix, s_state_name [self->state]);

            if (self->next_event == NULL_event)
                //  Get next valid message from mailbox, if any
                self->next_event = s_client_filter_mailbox (self);
        }
    }
}

//  zloop callback when client inactivity timer expires

static int
s_client_expired (zloop_t *loop, int timer_id, void *argument)
{
    s_client_t *self = (s_client_t *) argument;
    s_client_execute (self, expired_event);
    return 0;
}

//  zloop callback when client wakeup timer expires

static int
s_client_wakeup (zloop_t *loop, int timer_id, void *argument)
{
    s_client_t *self = (s_client_t *) argument;
    s_client_execute (self, self->wakeup_event);
    return 0;
}


//  Server methods

static void
s_server_config_self (s_server_t *self)
{
    //  Built-in server configuration options
    //  
    //  Animation is disabled by default
    self->animate = atoi (
        zconfig_resolve (self->config, "server/animate", "0"));

    //  Default client timeout is 60 seconds
    self->timeout = atoi (
        zconfig_resolve (self->config, "server/timeout", "60000"));

    //  Do we want to run server in the background?
    int background = atoi (
        zconfig_resolve (self->config, "server/background", "0"));
    if (!background)
        zlog_set_foreground (self->log, true);
}

static s_server_t *
s_server_new (zsock_t *pipe)
{
    s_server_t *self = (s_server_t *) zmalloc (sizeof (s_server_t));
    assert (self);
    assert ((s_server_t *) &self->server == self);

    self->pipe = pipe;
    self->router = zsock_new (ZMQ_ROUTER);
    self->clients = zhash_new ();
    self->log = zlog_new ("zpipes_server");
    self->config = zconfig_new ("root", NULL);
    self->loop = zloop_new ();
    srandom ((unsigned int) zclock_time ());
    self->client_id = randof (1000);
    s_server_config_self (self);

    //  Initialize application server context
    self->server.log = self->log;
    self->server.config = self->config;
    server_initialize (&self->server);

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
        zsock_destroy (&self->router);
        zconfig_destroy (&self->config);
        zhash_destroy (&self->clients);
        zloop_destroy (&self->loop);
        zlog_destroy (&self->log);
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
        if (streq (zconfig_name (section), "echo"))
            zlog_notice (self->log, "%s", zconfig_value (section));
        else
        if (streq (zconfig_name (section), "bind")) {
            char *endpoint = zconfig_resolve (section, "endpoint", "?");
            int rc = zsock_bind (self->router, "%s", endpoint);
            assert (rc != -1);
        }
        section = zconfig_next (section);
    }
    s_server_config_self (self);
}

//  Process message from pipe

static int
s_server_api_message (zloop_t *loop, zsock_t *reader, void *argument)
{
    s_server_t *self = (s_server_t *) argument;
    zmsg_t *msg = zmsg_recv (self->pipe);
    if (!msg)
        return -1;              //  Interrupted; exit zloop
    char *method = zmsg_popstr (msg);
    if (streq (method, "$TERM")) {
        free (method);
        zmsg_destroy (&msg);
        return -1;
    }
    else
    if (streq (method, "BIND")) {
        char *endpoint = zmsg_popstr (msg);
        self->port = zsock_bind (self->router, "%s", endpoint);
        assert (self->port != -1);
        zstr_sendf (self->pipe, "%d", self->port);
        free (endpoint);
    }
    else
    if (streq (method, "CONFIGURE")) {
        char *config_file = zmsg_popstr (msg);
        zconfig_destroy (&self->config);
        self->config = zconfig_load (config_file);
        if (self->config) {
            s_server_apply_config (self);
            self->server.config = self->config;
        }
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
    else {
        //  Execute custom method
        zmsg_t *reply = server_method (&self->server, method, msg);
        //  If reply isn't null, send it to caller
        zmsg_send (&reply, self->pipe);
    }
    free (method);
    zmsg_destroy (&msg);
    return 0;
}

//  Handle a message (a protocol request) from the client

static int
s_server_client_message (zloop_t *loop, zsock_t *reader, void *argument)
{
    s_server_t *self = (s_server_t *) argument;
    zpipes_msg_t *msg = zpipes_msg_recv (self->router);
    if (!msg)
        return -1;              //  Interrupted; exit zloop

    char *hashkey = zframe_strhex (zpipes_msg_routing_id (msg));
    s_client_t *client = (s_client_t *) zhash_lookup (self->clients, hashkey);
    if (client == NULL) {
        client = s_client_new (self, zpipes_msg_routing_id (msg));
        zhash_insert (self->clients, hashkey, client);
        zhash_freefn (self->clients, hashkey, s_client_free);
    }
    free (hashkey);

    //  Any input from client counts as activity
    if (client->expiry_timer)
        zloop_timer_end (self->loop, client->expiry_timer);
    //  Reset expiry timer
    client->expiry_timer = zloop_timer (
        self->loop, self->timeout, 1, s_client_expired, client);
        
    //  Queue request and possibly pass it to client state machine
    zlist_append (client->mailbox, msg);
    event_t event = s_client_filter_mailbox (client);
    if (event != NULL_event)
        s_client_execute (client, event);
        
    return 0;
}

//  Watch server config file and reload if changed

static int
s_watch_server_config (zloop_t *loop, int timer_id, void *argument)
{
    s_server_t *self = (s_server_t *) argument;
    if (zconfig_has_changed (self->config)
    &&  zconfig_reload (&self->config) == 0) {
        s_server_config_self (self);
        self->server.config = self->config;
        zlog_notice (self->log, "reloaded configuration from %s",
            zconfig_filename (self->config));
    }
    return 0;
}


//  ---------------------------------------------------------------------
//  This is the server actor, which polls its two sockets and processes
//  incoming messages

void
zpipes_server (zsock_t *pipe, void *args)
{
    //  Initialize
    s_server_t *self = s_server_new (pipe);
    assert (self);
    zlog_notice (self->log, "starting zpipes_server service");
    zsock_signal (pipe, 0);

    //  Set-up server monitor to watch for config file changes
    engine_set_monitor ((server_t *) self, 1000, s_watch_server_config);
    //  Set up handler for the two main sockets the server uses
    engine_handle_socket ((server_t *) self, self->pipe, s_server_api_message);
    engine_handle_socket ((server_t *) self, self->router, s_server_client_message);

    //  Run reactor until there's a termination signal
    zloop_start (self->loop);

    //  Reactor has ended
    zlog_notice (self->log, "terminating zpipes_server service");
    s_server_destroy (&self);
}
