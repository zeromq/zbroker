/*  =========================================================================
    zpipes_selftest - run self tests

    Copyright (c) tbd
    =========================================================================
*/

#include "zpipes_classes.h"

int main (int argc, char *argv [])
{
    bool verbose;
    if (argc == 2 && streq (argv [1], "-v"))
        verbose = true;
    else
        verbose = false;

    printf ("Running self tests...\n");
    zpipes_agent_test (verbose);
    zpipes_test (verbose);
    zpipes_client_test (verbose);
    printf ("Tests passed OK\n");
    return 0;
}
