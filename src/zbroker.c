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

//  Helpers that should move into CZMQ/zsys
static int
s_zsys_run_as (const char *lockfile, const char *group, const char *user);

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
        if (s_zsys_run_as (
            zconfig_resolve (config, "server/lockfile", NULL),
            zconfig_resolve (config, "server/group", NULL),
            zconfig_resolve (config, "server/user", NULL)))
            return -1;

        zconfig_destroy (&config);
    }
    else {
        zclock_log ("E: cannot load config file '%s'\n", config_file);
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


//  --------------------------------------------------------------------------
//  Set-up the process to run as a specified group and user.
//  First, if lockfile is specified, tries to grab a write lock on that
//  file, and if successful, dumps the process ID into the file. This
//  prevents two instances of the process from starting up by accident.
//  Second, if group is specified, switches to using that group. Third,
//  if user is specified, switches to using that group. If user is null,
//  switches to the real user ID after dropping the PID into the lockfile.
//  Returns 0 on success, -1 on any error.

static int
s_zsys_run_as (const char *lockfile, const char *group, const char *user)
{
    //  Switch to effective user ID (who owns executable); for
    //  system services this should be root, so that we can write
    //  the PID file into e.g. /var/run/
    if (seteuid (geteuid ())) {
        zclock_log ("E: cannot set effective user id: %s\n",
                    strerror (errno));
        return -1;
    }
    if (lockfile) {
        //  We enforce a lock on the lockfile, if specified, so that
        //  only one copy of the process can run at once.
        int handle = open (lockfile, O_RDWR | O_CREAT, 0640);
        if (handle < 0) {
            zclock_log ("E: cannot open lockfile '%s': %s\n",
                        lockfile, strerror (errno));
            return -1;          
        }
        else {
            struct flock filelock;
            filelock.l_type   = F_WRLCK;    //  F_RDLCK, F_WRLCK, F_UNLCK
            filelock.l_whence = SEEK_SET;   //  SEEK_SET, SEEK_CUR, SEEK_END
            filelock.l_start  = 0;          //  Offset from l_whence
            filelock.l_len    = 0;          //  length, 0 = to EOF
            filelock.l_pid    = getpid ();
            if (fcntl (handle, F_SETLK, &filelock)) {
                zclock_log ("E: cannot get lock: %s\n", strerror (errno));
                return -1;              
            }
        }
        //   We record the broker's process id in the lock file
        char pid_buffer [10];
        snprintf (pid_buffer, sizeof (pid_buffer), "%6d\n", getpid ());
        if (write (handle, pid_buffer, strlen (pid_buffer)) != strlen (pid_buffer)) {
            zclock_log ("E: cannot write to lockfile: %s\n",
                        strerror (errno));
            return -1;
        }
        close (handle);
    }
    if (group) {
        zclock_log ("I: broker running under group '%s'", group);
        struct group *grpbuf = NULL;
        grpbuf = getgrnam (group);
        if (grpbuf == NULL || setgid (grpbuf->gr_gid)) {
            zclock_log ("E: could not switch group: %s", strerror (errno));
            return -1;
        }
    }
    if (user) {
        zclock_log ("I: broker running under user '%s'", user);
        struct passwd *pwdbuf = NULL;
        pwdbuf = getpwnam (user);
        if (pwdbuf == NULL || setuid (pwdbuf->pw_uid)) {
            zclock_log ("E: could not switch user: %s", strerror (errno));
            return -1;
        }
    }
    else {
        //  Switch back to real user ID (who started process)
        if (setuid (getuid ())) {
            zclock_log ("E: cannot set real user id: %s\n",
                        strerror (errno));
            return -1;
        }
    }
    return 0;
}
