/*  =========================================================================
    zpipes_agent - work with background zpipes agent

    Copyright (c) tbd
    =========================================================================
*/

#ifndef __ZPIPES_AGENT_H_INCLUDED__
#define __ZPIPES_AGENT_H_INCLUDED__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _zpipes_agent_t zpipes_agent_t;

//  Background engine
void
    zpipes_agent_main (void *args, zctx_t *ctx, void *pipe);

//  Self test of this class
void
    zpipes_agent_test (bool verbose);

#ifdef __cplusplus
}
#endif

#endif
