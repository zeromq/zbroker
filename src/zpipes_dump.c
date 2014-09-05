/*  =========================================================================
    zpipes_dump - dump state of all zpipes brokers on network
    Currently, dumps Zyre map for each broker.

    Copyright (c) the Contributors as noted in the AUTHORS file.
    This file is part of zbroker, the ZeroMQ broker project.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.
    =========================================================================
*/

#include "zbroker_classes.h"

int main (int argc, char *argv [])
{
    //  Set up Zyre cluster from command-line arguments
    zyre_t *node = zyre_new ("zpipes_dump");
    if (argc == 2 && streq (argv [1], "beacon"))
        printf ("zpipes_dump: using default UDP beaconing to find brokers\n");
    else
    if (argc == 4 && streq (argv [1], "gossip")) {
        printf ("zpipes_dump: using gossip discovery to find brokers\n");
        printf (" - Zyre node endpoint=%s", argv [2]);
        zyre_set_endpoint (node, "%s", argv [2]);
        char *gossip_endpoint = argv [3];
        if (*gossip_endpoint == '@') {
            gossip_endpoint++;
            zsys_info (" - gossip service bind to %s", gossip_endpoint);
            zyre_gossip_bind (node, "%s", gossip_endpoint);
        }
        else {
            zsys_info (" - gossip service connect to %s", gossip_endpoint);
            zyre_gossip_connect (node, "%s", gossip_endpoint);
        }
    }
    else {
        puts ("Usage: zpipes_dump [ beacon | gossip zyre-endpoint [@]gossip-endpoint ] ");
        puts ("  Where @ means bind, else connect to gossip endpoint");
        zyre_destroy (&node);
        return 0;
    }
    zyre_start (node);
    zyre_join (node, "ZPIPES");
    zclock_sleep (100);
    zyre_shouts (node, "ZPIPES", "DUMP");
    zclock_sleep (100);
    zyre_destroy (&node);

    return 0;
}
