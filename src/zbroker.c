/*  =========================================================================
    zbroker - command-line broker daemon

    Copyright (c) the Contributors as noted in the AUTHORS file.
    This file is part of zbroker, the ZeroMQ broker project.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.
    =========================================================================
*/


#include "zbroker_classes.h"

#define PRODUCT         "zbroker service/0.0.1"
#define COPYRIGHT       "Copyright (c) 2014 the Contributors"
#define NOWARRANTY \
"This Software is provided under the MPLv2 License on an \"as is\" basis,\n" \
"without warranty of any kind, either expressed, implied, or statutory.\n"

int main (int argc, char *argv [])
{
    puts (PRODUCT);
    puts (COPYRIGHT);
    puts (NOWARRANTY);

    if (argc == 0) {
        puts ("Usage: zbroker [-b] [configfile]");
        puts ("  -b  run broker as background process");
        puts ("  Default configfile is 'zbroker.cfg'");
        return 0;
    }
    //  This is poor argument passing; should be improved later
    int argn = 1;

    //  Collect -b switch, if any
    bool as_daemon = false;
    if (argc > argn && streq (argv [argn], "-b")) {
        as_daemon = true;
        argn++;
    }
    //  Collect configuration file name
    const char *config_file = "zbroker.cfg";
    if (argc > argn) {
        config_file = argv [argn];
        argn++;
    }
    zclock_log ("I: starting zpipes server using config in '%s'", config_file);
    zpipes_server_t *zpipes_server = zpipes_server_new ();
    zpipes_server_configure (zpipes_server, config_file);

    if (as_daemon) {
        zclock_log ("I: broker switching to background process...");
        zsys_daemonize (NULL);
    }
    //  Wait until process is interrupted
    while (!zctx_interrupted)
        zclock_sleep (1000);
    puts ("interrupted");

    //  Shutdown all services
    zpipes_server_destroy (&zpipes_server);
    return 0;
}


