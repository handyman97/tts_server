// $Id: logger.h,v 1.1 2020/03/14 08:36:04 hito Exp hito $

#ifndef _LOGGER_H
#define _LOGGER_H

#include <syslog.h>

#ifdef __cplusplus
extern "C" {
#endif

#define syslog(...) _syslog (__VA_ARGS__)

// wrapper of the common syslog function
void _syslog (int, const char*, ...);

#ifdef __cplusplus
}
#endif

#endif
