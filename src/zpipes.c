//  zpipes broker version 0.1
//
//  This broker handles named pipes within a single host
//  It imposes no limits on pipe size - writing always succeeds.
//  Readers are blocked until data arrives.
//
//  IPC command protocol:
//      C:OPEN pipename            S:OK
//      C:READ pipename            S:chunk
//      C:WRITE pipename chunk     S:OK
//      C:CLOSE pipename           S:OK

#include "czmq.h"

//  -------------------------------------------------------------------------
//  Named pipe class implementation

typedef struct {
    char *name;                 //  Broker name
    zctx_t *ctx;                //  CZMQ context
    zpoller_t *poller;          //  Core broker poller
    void *router;               //  Command server socket
    bool terminated;            //  API shut us down
    zhash_t *pipes;             //  Pending requests
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
//  Broker class implementation

static broker_t *
broker_new (char *name)
{
    broker_t *self = (broker_t *) zmalloc (sizeof (broker_t));
    self->name = strdup (name);
    self->ctx = zctx_new ();
    self->pipes = zhash_new ();
    self->router = zsocket_new (self->ctx, ZMQ_ROUTER);
    self->poller = zpoller_new (self->router, NULL);

    //  Bind router socket to broker IPC command interface
    //  We're using abstract namespaces, assuming Linux for now
    int rc = zsocket_bind (self->router, "ipc://@/zpipes/%s", self->name);
    assert (rc == 0);

    return self;
}

static void
broker_destroy (broker_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        broker_t *self = *self_p;
        free (self->name);
        zctx_destroy (&self->ctx);
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
//  Run broker until interrupted

static void
broker_execute (broker_t *self)
{
    printf ("I: starting zpipes broker '%s'\n", self->name);
    while (!zpoller_terminated (self->poller)) {
        void *which = zpoller_wait (self->poller, -1);
        if (which == self->router)
            broker_process_command (self);
        if (self->terminated)
            break;
    }
    printf (" - interrupted\n");
    printf ("I: ending zpipes broker '%s'\n", self->name);
}


//  -------------------------------------------------------------------------
//  Main code starts one broker instance and runs it in current thread

int main (int argc, char *argv [])
{
    char *name = argc < 2? "local": argv [1];
    broker_t *broker = broker_new (name);
    broker_execute (broker);
    broker_destroy (&broker);
    return 0;
}
