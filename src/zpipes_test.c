//  Test case for zpipes
//
//  Simulates a set of applications that share pipes across
//  multiple servers. We use 3 applications, sitting on two
//  servers (zpipes services).

#include <czmq.h>

//  Simple API class to manage access to pipes for us

typedef struct {
    char *name;                 //  Name of pipe
    void *dealer;               //  Dealer socket to zpipes broker
} pipe_t;

//  Pipe constructor

static pipe_t *
pipe_new (zctx_t *ctx, char *broker_id, char *pipe_name)
{
    //  Create new pipe API instance
    pipe_t *self = (pipe_t *) zmalloc (sizeof (pipe_t));
    assert (self);
    self->name = strdup (pipe_name);

    //  Create dealer socket and connect to broker IPC port
    self->dealer = zsocket_new (ctx, ZMQ_DEALER);
    if (self->dealer) {
        int rc = zsocket_connect (self->dealer, "ipc://@/zpipes/%s", broker_id);
        assert (rc == 0);

        //  Open the pipe and wait for broker confirmation
        zstr_sendm (self->dealer, "OPEN");
        zstr_send (self->dealer, self->name);
        char *ack = zstr_recv (self->dealer);
        free (ack);
    }
    return self;
}

//  Pipe destructor

static void
pipe_destroy (pipe_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        pipe_t *self = *self_p;
        if (self->dealer) {
            //  Close the pipe and wait for broker confirmation
            zstr_sendm (self->dealer, "CLOSE");
            zstr_send (self->dealer, self->name);
            char *ack = zstr_recv (self->dealer);
            free (ack);
        }
        //  Destroy this pipe API instance
        free (self->name);
        free (self);
        *self_p = NULL;
    }
}

//  Write chunk of data to to pipe

static void
pipe_write (pipe_t *self, void *data, size_t size)
{
    assert (self);
    if (self->dealer) {
        //  Send chunk to pipe and wait for broker confirmation
        zstr_sendm (self->dealer, "WRITE");
        zstr_sendm (self->dealer, self->name);
        zmq_send (self->dealer, data, size, 0);
        char *ack = zstr_recv (self->dealer);
        free (ack);
    }
}

//  Read chunk of data from pipe, blocks until data arrives
//  Returns size of chunk read; if less than max_size, truncates

static size_t
pipe_read (pipe_t *self, void *data, size_t max_size)
{
    assert (self);
    if (self->dealer) {
        zstr_sendm (self->dealer, "READ");
        zstr_send (self->dealer, self->name);
        int rc = zmq_recv (self->dealer, data, max_size, 0);
        return (size_t) rc;
    }
    else
        return 0;
}


//  Test client tasks

static void
client_a (void *args, zctx_t *ctx, void *caller_pipe)
{
    //  Synchronize with main line task
    char *start = zstr_recv (caller_pipe);
    free (start);

    //  Write CHUNK1 to pipe B at "local" broker
    pipe_t *pipeB = pipe_new (ctx, "local", "B");
    pipe_write (pipeB, "CHUNK1", 6);
    pipe_destroy (&pipeB);
}

static void
client_b (void *args, zctx_t *ctx, void *caller_pipe)
{
    //  Read CHUNK1 from pipe B at "local" broker
    pipe_t *pipeB = pipe_new (ctx, "local", "B");
    byte buffer [6];
    size_t bytes = pipe_read (pipeB, buffer, 6);
    if (bytes == -1)
        return;                 //  Interrupted

    assert (bytes == 6);
    pipe_destroy (&pipeB);

    //  Write CHUNK2 to pipe C at "local" broker
    pipe_t *pipeC = pipe_new (ctx, "local", "C");
    pipe_write (pipeC, "CHUNK2", 6);
    pipe_destroy (&pipeC);
}

static void
client_c (void *args, zctx_t *ctx, void *caller_pipe)
{
    //  Read CHUNK2 from pipe C at "local" broker
    pipe_t *pipeC = pipe_new (ctx, "local", "C");
    byte buffer [6];
    size_t bytes = pipe_read (pipeC, buffer, 6);
    if (bytes == -1)
        return;                 //  Interrupted

    assert (bytes == 6);
    pipe_destroy (&pipeC);

    //  Synchronize with main line task
    zstr_send (caller_pipe, "DONE");
}


int main (void)
{
    //  Well test a chain of flow from main to A, then B, then C, then main
    zctx_t *ctx = zctx_new ();
    void *thread_pipe_a = zthread_fork (ctx, client_a, NULL);
    void *thread_pipe_c = zthread_fork (ctx, client_c, NULL);
    zthread_fork (ctx, client_b, NULL);

    printf ("Starting zpipes test...");
    zstr_send (thread_pipe_a, "START");
    char *done = zstr_recv (thread_pipe_c);
    free (done);
    printf (" OK\n");

    zctx_destroy (&ctx);
    return 0;
}