#include <execinfo.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>

#include "log.h"

static int max_level = LEVEL_WARNING;

int log_init( const char* file, int level )
{
    int fd;

    if( file )
    {
        fd = open(file,O_WRONLY|O_APPEND|O_CREAT|O_SYNC|O_CLOEXEC, 0600);

        if( fd < 0 || dup2( fd, STDERR_FILENO ) < 0 )
            perror( file );

        close( fd );
    }

    max_level = level;
    return 1;
}

void log_printf( int level, const char* fmt, ... )
{
    const char* lvstr;
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
    fprintf(stderr, "%s%s ", temp, lvstr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end( ap );
}

void print_stacktrace( void )
{
    void *buffer[100];
    char **strings;
    int j, nptrs;

    nptrs = backtrace( buffer, sizeof(buffer)/sizeof(buffer[0]) );
    strings = backtrace_symbols( buffer, nptrs );

    if( strings )
    {
        for( j = 0; j < nptrs; ++j )
            fprintf( stderr, "%s\n", strings[j] );

        free( strings );
    }
    else
    {
        perror( "backtrace_symbols" );
    }
}

