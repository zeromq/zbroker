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

#define PRODUCT         "zbroker service/0.0.2"
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
    zclock_log ("I: starting zpipes broker using config in '%s'", config_file);
    zconfig_t *config = zconfig_load (config_file);
    if (config) {
        //  Do we want to run broker in the background?
        int as_daemon = atoi (zconfig_resolve (config, "server/background", "0"));
        const char *workdir = zconfig_resolve (config, "server/workdir", ".");
        if (as_daemon) {
            zclock_log ("I: broker switching to background process...");
            if (zsys_daemonize (workdir))
                return -1;
        }
        //  Switch to user/group to run process under, if any
        if (zsys_run_as (
            zconfig_resolve (config, "server/lockfile", NULL),
            zconfig_resolve (config, "server/group", NULL),
            zconfig_resolve (config, "server/user", NULL)))
            return -1;

        zconfig_destroy (&config);
    }
    else {
        zclock_log ("E: cannot load config file '%s'", config_file);
        return 1;
    }
    zactor_t *server = zactor_new (zpipes_server, NULL);
    zstr_sendx (server, "CONFIGURE", config_file, NULL);
    zstr_sendx (server, "JOIN CLUSTER", NULL);
    
    char *reply = zstr_recv (server);
    if (reply && strneq (reply, "OK"))
        zclock_log ("W: no UDP discovery, cannot join cluster");
    free (reply);
    
    //  Accept and print any message back from server
    while (true) {
        char *message = zstr_recv (server);
        if (message) {
            puts (message);
            free (message);
        }
        else {
            puts ("interrupted");
            break;
        }
    }
    //  Shutdown all services
    zactor_destroy (&server);
    return 0;
}
