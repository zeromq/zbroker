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

    if (argc < 2) {
        puts ("Usage: zbroker [broker-name]");
        return 0;
    }
    //  Install all services
    const char *zpipes_instance = argc == 2? argv [1]: "local";
    zclock_log ("I: starting zpipes server '%s'", zpipes_instance);
    zpipes_server_t *zpipes_server = zpipes_server_new ();
    zpipes_server_bind (zpipes_server, "ipc://@/zpipes/%s", zpipes_instance);

    //  Wait until process is interrupted
    while (!zctx_interrupted)
        zclock_sleep (1000);
    puts ("interrupted");

    //  Shutdown all services
    zpipes_server_destroy (&zpipes_server);
    return 0;
}
