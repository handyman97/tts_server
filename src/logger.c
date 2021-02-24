// $Id: logger.c,v 1.1 2020/03/14 08:36:04 hito Exp hito $

// define your own version of _syslog elsewhere
// unless you need to resort to the following fallback version.

#if 0
//#if 1

#include "logger.h"
#include <stdarg.h>
#include <syslog.h>

// fallback
void
_syslog (int prio, const char* fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    vsyslog (prio, fmt, ap);
    va_end (ap);
}

#endif
