// $Id: $

#include <cassert>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include <libgen.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "server.h"
#include "logger.h"

#ifndef DAEMONIZE
#define DAEMONIZE 1
#endif

// globals
const char* g_program = NULL;
//char g_machine[100];
//pid_t g_pid = 0; 
unsigned int g_verbose = 0;

//
int
main (int argc, char** argv)
{
    g_program = basename (argv[0]);
    const char* config_file = "default.json";

    for (int i = 1; i < argc; i++)
    {
        // tts_service (fallback)
        if (!strncmp (argv[i], "--conf", 6))
            config_file = argv[++i];

        else if (!strcmp (argv[i], "-v"))
            g_verbose = 1;
        else if (!strncmp (argv[i], "--verbose=", 10))
            g_verbose = atoi (argv[i] + 10);

        else if (!strcmp (argv[i], "-h") || !strcmp (argv[i], "--help"))
        {
            const char* prog = basename (argv[0]);
            printf ("%s -- networked text-to-speech server\n",
                    prog);
            printf ("usage: %s [--conf <conf_file>]"
                    "\n",
                    prog);
            return (0);
        }
        else if (*argv[i] == '-')
        {
            fprintf (stderr, "invalid option: \"%s\"\n", argv[i]);
            exit (1);
        }
        else
        {
            fprintf (stderr, "invalid argument: \"%s\"\n", argv[i]);
            exit (1);
        }
    }

    // traps
    signal (SIGTERM, tts_server::quit);
    signal (SIGHUP, tts_server::quit);
    signal (SIGINT, tts_server::quit);

#if DAEMONIZE
    // daemonization (http://www.netzmafia.de/skripten/unix/linux-daemon-howto.html)
    pid_t child_pid = fork ();
    if (child_pid < 0) { fprintf (stderr, "fork failed\n"); exit (1); }
    if (child_pid > 0) { fprintf (stderr, "forked: %d\n", child_pid); exit (0); }
    umask (0);
    pid_t sid = setsid ();  // run the process in a new session
    if (sid < 0) exit(1);
    chdir ("/");
    close (STDIN_FILENO);
    close (STDOUT_FILENO);
    close (STDERR_FILENO);
#endif

    //syslog
    openlog (g_program, LOG_PID, LOG_USER);
    //LOG_EMERG=0, LOG_ALERT=1, LOG_CRIT=2, LOG_ERR=3, LOG_WARNING=4, LOG_NOTICE=5, LOG_INFO=6, and LOG_DEBUG=7
    //if (g_verbose <= 0) setlogmask (0b00111111);
    //if (g_verbose == 1) setlogmask (0b01111111);
    //if (g_verbose >= 2) setlogmask (0b11111111);

    //gethostname (g_machine, 100);
    //g_pid = getpid ();

    // load config
    tts_server::setup (config_file);

    // service
    tts_server::run ();  // blocking

    syslog (LOG_ERR, "terminated unexpectedly");
    tts_server::quit (SIGTERM);

    return (0);
}

// wrapper of the common syslog function
void
_syslog (int prio, const char* fmt, ...)
{
    //prio: LOG_EMERG=0, LOG_ALERT=1, LOG_CRIT=2, LOG_ERR=3, LOG_WARNING=4, LOG_NOTICE=5, LOG_INFO=6, and LOG_DEBUG=7
    if (g_verbose == 0 && prio >= LOG_NOTICE) return;
    if (g_verbose == 1 && prio >= LOG_INFO) return;
    if (g_verbose == 2 && prio >= LOG_DEBUG) return;
        
    va_list ap;
    va_start (ap, fmt);

#if DAEMONIZE
    vsyslog (prio, fmt, ap);
#else
    vfprintf (stderr, fmt, ap);
    fprintf (stderr, "\n");
#endif

    va_end (ap);
}
