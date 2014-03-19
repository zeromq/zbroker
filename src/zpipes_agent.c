/*  =========================================================================
    zpipes_agent - work with background zpipes agent

    -------------------------------------------------------------------------
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

#include "zpipes_classes.h"

//  -------------------------------------------------------------------------
//  Structure of our class, used in this source only

typedef struct {
    zctx_t *ctx;                //  CZMQ context
    void *pipe;                 //  Pipe back to application
    char *name;                 //  Broker name
    zpoller_t *poller;          //  Core broker poller
    void *router;               //  Command server socket
    bool terminated;            //  API shut us down
    zhash_t *pipes;             //  Collection of pipes
} broker_t;

typedef struct {
    broker_t *broker;           //  Parent broker
    char *name;                 //  Name of pipe
    zlist_t *queue;             //  Pipe chunk queue
    size_t links;               //  Number of callers that have opened pipe
    zframe_t *pending;          //  Routing ID of pending reader, if any
} pipe_t;

static void s_delete_pipe (void *argument);

static pipe_t *
pipe_new (broker_t *broker, char *name)
{
    pipe_t *self = (pipe_t *) zmalloc (sizeof (pipe_t));
    self->name = strdup (name);
    self->queue = zlist_new ();
    self->broker = broker;
    zhash_insert (broker->pipes, name, self);
    zhash_freefn (broker->pipes, name, s_delete_pipe);
    return self;
}

static void
pipe_destroy (pipe_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        pipe_t *self = *self_p;
        zframe_t *chunk = (zframe_t *) zlist_first (self->queue);
        while (chunk) {
            zframe_destroy (&chunk);
            chunk = (zframe_t *) zlist_next (self->queue);
        }
        zframe_destroy (&self->pending);
        zlist_destroy (&self->queue);
        free (self->name);
        free (self);
        *self_p = NULL;
    }
}

//  Callback when we remove group from container
static void
s_delete_pipe (void *argument)
{
    pipe_t *pipe = (pipe_t *) argument;
    pipe_destroy (&pipe);
}


//  -------------------------------------------------------------------------
//  Constructor

static broker_t *
broker_new (zctx_t *ctx, void *pipe)
{
    broker_t *self = (broker_t *) zmalloc (sizeof (broker_t));
    self->ctx = ctx;
    self->pipe = pipe;
    self->name = zstr_recv (self->pipe);
    self->pipes = zhash_new ();
    self->router = zsocket_new (self->ctx, ZMQ_ROUTER);
    self->poller = zpoller_new (self->pipe, self->router, NULL);

    //  Bind router socket to broker IPC command interface
    //  We're using abstract namespaces, assuming Linux for now
    int rc = zsocket_bind (self->router, "ipc://@/zpipes/%s", self->name);
    assert (rc == 0);

    return self;
}


//  -------------------------------------------------------------------------
//  Destructor

static void
broker_destroy (broker_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        broker_t *self = *self_p;
        free (self->name);
        zpoller_destroy (&self->poller);
        zhash_destroy (&self->pipes);
        free (self);
        *self_p = NULL;
    }
}


//  -------------------------------------------------------------------------
//  Here we handle the different control messages from the front-end

static int
broker_process_command (broker_t *self)
{
    zmsg_t *msg = zmsg_recv (self->router);
    if (!msg)
        return -1;              //  Interrupted

    //  Ignore invalid messages, log them for debugging
    if (zmsg_size (msg) < 3) {
        puts ("E: invalid command, discarded");
        zmsg_dump (msg);
        zmsg_destroy (&msg);
        return 0;
    }
    //  Now parse the command
    //  First frame is caller routing id
    zframe_t *caller_rid = zmsg_pop (msg);

    //  Second frame is always command
    char *command = zmsg_popstr (msg);

    //  If pipe doesn't exist, create it now
    char *pipename = zmsg_popstr (msg);
    pipe_t *pipe = (pipe_t *) zhash_lookup (self->pipes, pipename);
    char *reply = NULL;
    if (streq (command, "OPEN")) {
        if (!pipe)
            pipe = pipe_new (self, pipename);
        pipe->links++;          //  Count this link to the pipe
        reply = "OK";
    }
    else
    if (streq (command, "CLOSE") && pipe) {
        pipe->links--;
        if (pipe->links == 0)
            zhash_delete (self->pipes, pipename);
        reply = "OK";
    }
    else
    if (streq (command, "READ") && pipe) {
        //  If pipe has content, take next chunk and return it
        zframe_t *chunk = (zframe_t *) zlist_pop (pipe->queue);
        if (chunk) {
            zframe_send (&caller_rid, self->router, ZFRAME_MORE);
            zframe_send (&chunk, self->router, 0);
        }
        else
            //  Pipe is empty, mark pending delivery to this caller
            pipe->pending = caller_rid;
    }
    else
    if (streq (command, "WRITE") && pipe) {
        zframe_t *chunk = zmsg_pop (msg);
        if (pipe->pending) {
            zframe_send (&pipe->pending, self->router, ZFRAME_MORE);
            zframe_send (&chunk, self->router, 0);
        }
        else
            zlist_append (pipe->queue, chunk);
        reply = "OK";
    }
    else
        printf ("E: invalid command '%s'\n", command);

    //  Reply to caller if needed; else caller_rid must already be gone
    if (reply) {
        zframe_send (&caller_rid, self->router, ZFRAME_MORE);
        zstr_send (self->router, reply);
    }
    zstr_free (&command);
    zstr_free (&pipename);
    zmsg_destroy (&msg);
    return 0;
}


//  -------------------------------------------------------------------------
//  Here we handle control messages from the API

static int
broker_process_api (broker_t *self)
{
    //  Get the whole message off the pipe in one go
    zmsg_t *request = zmsg_recv (self->pipe);
    char *command = zmsg_popstr (request);
    if (!command)
        return -1;                  //  Interrupted

    if (streq (command, "TERMINATE")) {
        self->terminated = true;
        zstr_send (self->pipe, "OK");
    }
    free (command);
    zmsg_destroy (&request);
    return 0;
}


//  -------------------------------------------------------------------------
//  This is the agent thread. It starts up by creating an agent structure,
//  then polls the API pipe and other sockets for activity until it gets a
//  TERMINATE command from the API, at which point it exits.

void
zpipes_agent_main (void *args, zctx_t *ctx, void *pipe)
{
    broker_t *broker = broker_new (ctx, pipe);
    if (!broker)                //  Interrupted
        return;
    zstr_send (pipe, "OK");

    while (!zpoller_terminated (broker->poller)) {
        void *which = zpoller_wait (broker->poller, -1);
        if (which == broker->router)
            broker_process_command (broker);
        else
        if (which == broker->pipe)
            broker_process_api (broker);

        if (broker->terminated)
            break;
    }
    broker_destroy (&broker);
}


//  --------------------------------------------------------------------------
//  Self test of this class

void
zpipes_agent_test (bool verbose)
{
    printf (" * zpipes_agent: ");

    zctx_t *ctx = zctx_new ();
    assert (ctx);

    void *pipe = zthread_fork (ctx, zpipes_agent_main, NULL);
    assert (pipe);
    zstr_send (pipe, "local");
    char *status = zstr_recv (pipe);
    assert (streq (status, "OK"));
    zstr_free (&status);

    zstr_send (pipe, "TERMINATE");
    char *reply = zstr_recv (pipe);
    zstr_free (&reply);
    zctx_destroy (&ctx);

    printf ("OK\n");
}
