/*  =========================================================================
    zpipes - start/stop zpipes broker service

    Copyright (c) tbd
    =========================================================================
*/

/*
@header
    To be written.
@discuss
@end
*/

#include "zpipes_classes.h"

//  ---------------------------------------------------------------------
//  Structure of our class

struct _zpipes_t {
    void *pipe;                 //  Pipe through to agent
    zctx_t *ctx;                //  Our global ZMQ context
};


//  ---------------------------------------------------------------------
//  Constructor, creates a new zpipes broker

zpipes_t *
zpipes_new (const char *name)
{
    zpipes_t *self = (zpipes_t *) zmalloc (sizeof (zpipes_t));
    assert (self);

    //  Start zpipes agent and wait for it to be ready
    self->ctx = zctx_new ();
    self->pipe = zthread_fork (self->ctx, zpipes_agent_main, NULL);
    if (self->pipe) {
        //  Pass name to zpipes agent as startup argument
        zstr_send (self->pipe, name);
        //  Wait for handshake from agent telling us it's ready
        char *status = zstr_recv (self->pipe);
        if (strneq (status, "OK"))
            zpipes_destroy (&self);
        zstr_free (&status);
    }
    else
        zpipes_destroy (&self);
    return self;
}


//  ---------------------------------------------------------------------
//  Destructor, ends and destroys a zpipes broker

void
zpipes_destroy (zpipes_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        zpipes_t *self = *self_p;
        if (self->pipe) {
            zstr_send (self->pipe, "TERMINATE");
            char *reply = zstr_recv (self->pipe);
            zstr_free (&reply);
        }
        zctx_destroy (&self->ctx);
        free (self);
        *self_p = NULL;
    }
}


//  --------------------------------------------------------------------------
//  Self test of this class

void
zpipes_test (bool verbose)
{
    printf (" * zpipes: ");

    //  @selftest
    zpipes_t *broker = zpipes_new ("local");
    //  ...
    zpipes_destroy (&broker);
    //  @end
    printf ("OK\n");

}
