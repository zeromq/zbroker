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

    if (argc == 2 && streq (argv [1], "-h")) {
        puts ("Usage: zbroker [-h | config-file]");
        puts ("  Default config-file is 'zbroker.cfg'");
        return 0;
    }
    //  Collect configuration file name
    const char *config_file = "zbroker.cfg";
    if (argc > 1)
        config_file = argv [1];
    
    //  Load config file for our own use here
    zclock_log ("I: starting zpipes server using config in '%s'", config_file);
    zconfig_t *config = zconfig_load (config_file);
    if (config) {
        //  Do we want to run server in the background?
        int as_daemon = atoi (zconfig_resolve (config, "server/background", "0"));
        const char *workdir = zconfig_resolve (config, "server/workdir", ".");
        if (as_daemon) {
            zclock_log ("I: broker switching to background process...");
            zsys_daemonize (workdir);
        }
        zconfig_destroy (&config);
    }
    else {
        zclock_log ("E: cannot load config file '%s'\\n", config_file);
        return 1;
    }
    zpipes_server_t *zpipes_server = zpipes_server_new ();
    zpipes_server_configure (zpipes_server, config_file);
    
    //  Wait until process is interrupted
    while (!zctx_interrupted)
        zclock_sleep (1000);
    puts ("interrupted");

    //  Shutdown all services
    zpipes_server_destroy (&zpipes_server);
    return 0;
}
