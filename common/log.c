#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>

#include "log.h"

static int logfile = -1;
static int max_level = LEVEL_WARNING;

int log_init( const char* file, int level )
{
    if( file )
        logfile = open(file,O_WRONLY|O_APPEND|O_CREAT|O_SYNC|O_CLOEXEC, 0600);
    else
        logfile = dup(STDERR_FILENO);

    max_level = level;
    return 1;
}

void log_cleanup( void )
{
    close( logfile );
}

void log_printf( int level, const char* fmt, ... )
{
    char *p = NULL, *lvstr;
    char temp[128];
    struct tm stm;
    va_list ap;
    time_t t;

    if( level > max_level )
        return;

    switch( level )
    {
    case LEVEL_INFO:     lvstr = "[INFO]"; break;
    case LEVEL_WARNING:  lvstr = "[WARN]"; break;
    case LEVEL_CRITICAL: lvstr = "[ERR]"; break;
    default:             lvstr = "[DBG]"; break;
    }

    t = time(NULL);
    localtime_r( &t, &stm );
    strftime( temp, sizeof(temp), "[%Y-%m-%d %H:%M:%S %Z]", &stm );

    va_start( ap, fmt );
    if( vasprintf( &p, fmt, ap ) < 0 )
        p = NULL;
    va_end( ap );

    dprintf(logfile<0 ? STDERR_FILENO : logfile, "%s%s %s\n", temp, lvstr, p);
    free( p );
}

