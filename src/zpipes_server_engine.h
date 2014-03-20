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
//  Set one configuration key value

void
zpipes_server_setoption (zpipes_server_t *self, const char *path, const char *value)
{
    zstr_sendm (self->pipe, "SET_OPTION");
    zstr_sendm (self->pipe, path);
    zstr_send  (self->pipe, value);
}


//  --------------------------------------------------------------------------
//  Binds the server to a specified endpoint

long
zpipes_server_bind (zpipes_server_t *self, const char *endpoint)
{
    assert (self);
    assert (endpoint);
    zstr_sendm (self->pipe, "BIND");
    zstr_sendf (self->pipe, endpoint);
    char *reply = zstr_recv (self->pipe);
    long reply_value = atol (reply);
    free (reply);
    return reply_value;
}


//  ---------------------------------------------------------------------
//  State machine constants

typedef enum {
    start_state = 1,
    reading_state = 2,
    writing_state = 3
} state_t;

typedef enum {
    terminate_event = -1,
    input_event = 1,
    output_event = 2,
    failed_event = 3,
    expired_event = 4,
    heartbeat_event = 5,
    fetch_event = 6,
    close_event = 7,
    empty_event = 8,
    timeout_event = 9,
    store_event = 10
} event_t;

//  Names for animation
static char *
s_state_name [] = {
    "",
    "Start",
    "Reading",
    "Writing"
};

static char *
s_event_name [] = {
    "",
    "input",
    "output",
    "failed",
    "expired",
    "heartbeat",
    "fetch",
    "close",
    "empty",
    "timeout",
    "store"
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
    size_t heartbeat;           //  Default client heartbeat interval
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
    char *hashkey;              //  Key into clients hash
    zframe_t *routing_id;       //  Routing_id back to client
    state_t state;              //  Current state
    event_t event;              //  Current event
    event_t next_event;         //  The next event
    event_t exception;          //  Exception event, if any
    size_t heartbeat;           //  Actual heartbeat interval
    int64_t heartbeat_at;       //  Next heartbeat at this time
    int64_t expires_at;         //  Expires at this time
} s_client_t;

static void
    s_server_client_execute (s_server_t *server, s_client_t *client, int event);
static void
    open_pipe_for_input (client_t *self);
static void
    open_pipe_for_output (client_t *self);
static void
    fetch_chunk_from_pipe (client_t *self);
static void
    close_pipe (client_t *self);
static void
    store_chunk_to_pipe (client_t *self);

//  ---------------------------------------------------------------------
//  These methods are an internal API for actions

//  Set the next event, needed in at least one action in an internal
//  state; otherwise the state machine will wait for a message on the
//  router socket and treat that as the event.

#define set_next_event(self,event) { \
    ((s_client_t *) (self))->next_event = event; \
}

#define raise_exception(self,event) { \
    ((s_client_t *) (self))->exception = event; \
}

#define set_heartbeat(self,heartbeat) { \
    ((s_client_t *) (self))->heartbeat = heartbeat; \
}


//  ---------------------------------------------------------------------
//  Client methods

static s_client_t *
s_client_new (zframe_t *routing_id)
{
    s_client_t *self = (s_client_t *) zmalloc (sizeof (s_client_t));
    assert (self);
    assert ((s_client_t *) &self->client == self);
    
    self->state = start_state;
    self->hashkey = zframe_strhex (routing_id);
    self->routing_id = zframe_dup (routing_id);
    
    self->client.reply = zpipes_msg_new (0);
    zpipes_msg_set_routing_id (self->client.reply, self->routing_id);
    client_alloc (&self->client);
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
        client_free (&self->client);
        zframe_destroy (&self->routing_id);
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
    if (*tickless > self->heartbeat_at)
        *tickless = self->heartbeat_at;
    return 0;
}

//  Client hash function that checks if client is alive
static int
s_client_ping (const char *key, void *client, void *argument)
{
    s_client_t *self = (s_client_t *) client;
    //  Expire client if it's not answered us in a while
    if (zclock_time () >= self->expires_at && self->expires_at) {
        //  In case dialog doesn't handle expired_event by destroying
        //  client, set expires_at to zero to prevent busy looping
        self->expires_at = 0;
        s_server_client_execute ((s_server_t *) argument, self, expired_event);
    }
    else
    //  Check whether to send heartbeat to client
    if (zclock_time () >= self->heartbeat_at) {
        s_server_client_execute ((s_server_t *) argument, self, heartbeat_event);
        self->heartbeat_at = zclock_time () + self->heartbeat;
    }
    return 0;
}

//  Server methods

static void
s_server_config_self (s_server_t *self)
{
    //  Get standard server configuration
    self->monitor = atoi (
        zconfig_resolve (self->config, "server/monitor", "1")) * 1000;
    self->heartbeat = atoi (
        zconfig_resolve (self->config, "server/heartbeat", "1")) * 1000;
    self->monitor_at = zclock_time () + self->monitor;
}

static s_server_t *
s_server_new (zctx_t *ctx, void *pipe)
{
    s_server_t *self = (s_server_t *) zmalloc (sizeof (s_server_t));
    assert (self);
    assert ((s_server_t *) &self->server == self);
    server_alloc (&self->server);
    
    self->ctx = ctx;
    self->pipe = pipe;
    self->router = zsocket_new (self->ctx, ZMQ_ROUTER);
    self->clients = zhash_new ();
    self->config = zconfig_new ("root", NULL);
    s_server_config_self (self);
    return self;
}

static void
s_server_destroy (s_server_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        s_server_t *self = *self_p;
        server_free (&self->server);
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
    zconfig_t *section = zconfig_child (self->config);
    while (section) {
        zconfig_t *entry = zconfig_child (section);
        while (entry) {
            if (streq (zconfig_name (entry), "echo"))
                zclock_log (zconfig_value (entry));
            entry = zconfig_next (entry);
        }
        if (streq (zconfig_name (section), "bind")) {
            char *endpoint = zconfig_resolve (section, "endpoint", "?");
            self->port = zsocket_bind (self->router, endpoint);
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
        self->port = zsocket_bind (self->router, endpoint);
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
            printf ("E: cannot load config file '%s'\n", config_file);
            self->config = zconfig_new ("root", NULL);
        }
        free (config_file);
    }
    else
    if (streq (method, "SET_OPTION")) {
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


//  Execute state machine as long as we have next events
static void
s_server_client_execute (s_server_t *self, s_client_t *client, int event)
{
    client->next_event = event;
    while (client->next_event) {
        client->event = client->next_event;
        client->next_event = (event_t) 0;
        client->exception = (event_t) 0;
        zclock_log ("S: %s:", s_state_name [client->state]);
        zclock_log ("S:     %s", s_event_name [client->event]);
        switch (client->state) {
            case start_state:
                if (client->event == input_event) {
                    if (!client->exception) {
                        //  open pipe for input
                        zclock_log ("S:         $ open pipe for input");
                        open_pipe_for_input (&client->client);
                    }
                    if (!client->exception) {
                        //  send ready
                        zclock_log ("S:         $ send ready");
                        zpipes_msg_set_id (client->client.reply, ZPIPES_MSG_READY);
                        zpipes_msg_send (&(client->client.reply), self->router);
                        client->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (client->client.reply, client->routing_id);
                    }
                    if (!client->exception)
                        client->state = reading_state;
                }
                else
                if (client->event == output_event) {
                    if (!client->exception) {
                        //  open pipe for output
                        zclock_log ("S:         $ open pipe for output");
                        open_pipe_for_output (&client->client);
                    }
                    if (!client->exception) {
                        //  send ready
                        zclock_log ("S:         $ send ready");
                        zpipes_msg_set_id (client->client.reply, ZPIPES_MSG_READY);
                        zpipes_msg_send (&(client->client.reply), self->router);
                        client->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (client->client.reply, client->routing_id);
                    }
                    if (!client->exception)
                        client->state = writing_state;
                }
                else
                if (client->event == failed_event) {
                    if (!client->exception) {
                        //  send failed
                        zclock_log ("S:         $ send failed");
                        zpipes_msg_set_id (client->client.reply, ZPIPES_MSG_FAILED);
                        zpipes_msg_send (&(client->client.reply), self->router);
                        client->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (client->client.reply, client->routing_id);
                    }
                    if (!client->exception) {
                        //  terminate
                        zclock_log ("S:         $ terminate");
                        client->next_event = terminate_event;
                    }
                }
                else
                if (client->event == expired_event) {
                }
                else
                if (client->event == heartbeat_event) {
                }
                break;

            case reading_state:
                if (client->event == fetch_event) {
                    if (!client->exception) {
                        //  fetch chunk from pipe
                        zclock_log ("S:         $ fetch chunk from pipe");
                        fetch_chunk_from_pipe (&client->client);
                    }
                    if (!client->exception) {
                        //  send fetched
                        zclock_log ("S:         $ send fetched");
                        zpipes_msg_set_id (client->client.reply, ZPIPES_MSG_FETCHED);
                        zpipes_msg_send (&(client->client.reply), self->router);
                        client->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (client->client.reply, client->routing_id);
                    }
                    if (!client->exception)
                        client->state = reading_state;
                }
                else
                if (client->event == close_event) {
                    if (!client->exception) {
                        //  close pipe
                        zclock_log ("S:         $ close pipe");
                        close_pipe (&client->client);
                    }
                    if (!client->exception) {
                        //  send closed
                        zclock_log ("S:         $ send closed");
                        zpipes_msg_set_id (client->client.reply, ZPIPES_MSG_CLOSED);
                        zpipes_msg_send (&(client->client.reply), self->router);
                        client->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (client->client.reply, client->routing_id);
                    }
                    if (!client->exception) {
                        //  terminate
                        zclock_log ("S:         $ terminate");
                        client->next_event = terminate_event;
                    }
                }
                else
                if (client->event == empty_event) {
                    if (!client->exception) {
                        //  send empty
                        zclock_log ("S:         $ send empty");
                        zpipes_msg_set_id (client->client.reply, ZPIPES_MSG_EMPTY);
                        zpipes_msg_send (&(client->client.reply), self->router);
                        client->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (client->client.reply, client->routing_id);
                    }
                }
                else
                if (client->event == timeout_event) {
                    if (!client->exception) {
                        //  send timeout
                        zclock_log ("S:         $ send timeout");
                        zpipes_msg_set_id (client->client.reply, ZPIPES_MSG_TIMEOUT);
                        zpipes_msg_send (&(client->client.reply), self->router);
                        client->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (client->client.reply, client->routing_id);
                    }
                }
                else
                if (client->event == failed_event) {
                    if (!client->exception) {
                        //  send failed
                        zclock_log ("S:         $ send failed");
                        zpipes_msg_set_id (client->client.reply, ZPIPES_MSG_FAILED);
                        zpipes_msg_send (&(client->client.reply), self->router);
                        client->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (client->client.reply, client->routing_id);
                    }
                    if (!client->exception) {
                        //  terminate
                        zclock_log ("S:         $ terminate");
                        client->next_event = terminate_event;
                    }
                }
                else
                if (client->event == expired_event) {
                }
                else
                if (client->event == heartbeat_event) {
                }
                break;

            case writing_state:
                if (client->event == store_event) {
                    if (!client->exception) {
                        //  store chunk to pipe
                        zclock_log ("S:         $ store chunk to pipe");
                        store_chunk_to_pipe (&client->client);
                    }
                    if (!client->exception) {
                        //  send stored
                        zclock_log ("S:         $ send stored");
                        zpipes_msg_set_id (client->client.reply, ZPIPES_MSG_STORED);
                        zpipes_msg_send (&(client->client.reply), self->router);
                        client->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (client->client.reply, client->routing_id);
                    }
                    if (!client->exception)
                        client->state = writing_state;
                }
                else
                if (client->event == close_event) {
                    if (!client->exception) {
                        //  close pipe
                        zclock_log ("S:         $ close pipe");
                        close_pipe (&client->client);
                    }
                    if (!client->exception) {
                        //  send closed
                        zclock_log ("S:         $ send closed");
                        zpipes_msg_set_id (client->client.reply, ZPIPES_MSG_CLOSED);
                        zpipes_msg_send (&(client->client.reply), self->router);
                        client->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (client->client.reply, client->routing_id);
                    }
                    if (!client->exception) {
                        //  terminate
                        zclock_log ("S:         $ terminate");
                        client->next_event = terminate_event;
                    }
                }
                else
                if (client->event == failed_event) {
                    if (!client->exception) {
                        //  send failed
                        zclock_log ("S:         $ send failed");
                        zpipes_msg_set_id (client->client.reply, ZPIPES_MSG_FAILED);
                        zpipes_msg_send (&(client->client.reply), self->router);
                        client->client.reply = zpipes_msg_new (0);
                        zpipes_msg_set_routing_id (client->client.reply, client->routing_id);
                    }
                    if (!client->exception) {
                        //  terminate
                        zclock_log ("S:         $ terminate");
                        client->next_event = terminate_event;
                    }
                }
                else
                if (client->event == expired_event) {
                }
                else
                if (client->event == heartbeat_event) {
                }
                break;

        }
        if (client->exception) {
            zclock_log ("S:         ! %s", s_event_name [client->exception]);
            client->next_event = client->exception;
        }
        else {
            zclock_log ("S:         > %s", s_state_name [client->state]);
        }
        if (client->next_event == terminate_event) {
            //  Automatically calls s_client_destroy
            zhash_delete (self->clients, client->hashkey);
            break;
        }
    }
}

static void
s_server_client_message (s_server_t *self)
{
    zpipes_msg_t *request = zpipes_msg_recv (self->router);
    if (!request)
        return;         //  Interrupted; do nothing

    char *hashkey = zframe_strhex (zpipes_msg_routing_id (request));
    s_client_t *client = (s_client_t *) zhash_lookup (self->clients, hashkey);
    if (client == NULL) {
        client = s_client_new (zpipes_msg_routing_id (request));
        //  Set default client heartbeat
        client->heartbeat = self->heartbeat;
        client->client.server = &self->server;
        zhash_insert (self->clients, hashkey, client);
        zhash_freefn (self->clients, hashkey, s_client_free);
    }
    free (hashkey);
    if (client->client.request)
        zpipes_msg_destroy (&client->client.request);
    client->client.request = request;

    //  Any input from client counts as heartbeat
    client->heartbeat_at = zclock_time () + client->heartbeat;
    //  Any input from client counts as activity
    client->expires_at = zclock_time () + client->heartbeat * 3;

    //  Process messages that may come from client
    switch (zpipes_msg_id (request)) {
        case ZPIPES_MSG_INPUT:
            s_server_client_execute (self, client, input_event);
            break;
        case ZPIPES_MSG_OUTPUT:
            s_server_client_execute (self, client, output_event);
            break;
        case ZPIPES_MSG_FAILED:
            s_server_client_execute (self, client, failed_event);
            break;
        case ZPIPES_MSG_FETCH:
            s_server_client_execute (self, client, fetch_event);
            break;
        case ZPIPES_MSG_EMPTY:
            s_server_client_execute (self, client, empty_event);
            break;
        case ZPIPES_MSG_TIMEOUT:
            s_server_client_execute (self, client, timeout_event);
            break;
        case ZPIPES_MSG_STORE:
            s_server_client_execute (self, client, store_event);
            break;
        case ZPIPES_MSG_CLOSE:
            s_server_client_execute (self, client, close_event);
            break;
    }
}

//  Finally here's the server thread itself, which polls its two
//  sockets and processes incoming messages
static void
s_server_task (void *args, zctx_t *ctx, void *pipe)
{
    s_server_t *self = s_server_new (ctx, pipe);
    assert (self);
    zstr_send (self->pipe, "OK");

    zmq_pollitem_t items [] = {
        { self->pipe, 0, ZMQ_POLLIN, 0 },
        { self->router, 0, ZMQ_POLLIN, 0 }
    };
    self->monitor_at = zclock_time () + self->monitor;
    while (!self->terminated && !zctx_interrupted) {
        //  Calculate tickless timer, up to interval seconds
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

        //  Send heartbeats to idle clients as needed
        zhash_foreach (self->clients, s_client_ping, self);
        
        //  If clock went past timeout, then monitor server
        if (zclock_time () >= self->monitor_at) {
            self->monitor_at = zclock_time () + self->monitor;
        }
    }
    s_server_destroy (&self);
}
