#include <sys/eventfd.h>
#include <execinfo.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include "log.h"

static int max_level = LEVEL_WARNING;
static int lockfd = -1;

int log_init( const char* file, int level )
{
    int fd;

    lockfd = eventfd(1, EFD_CLOEXEC|EFD_SEMAPHORE);
    if( lockfd < 0 )
        return 0;

    if( file )
    {
        fd = open(file,O_WRONLY|O_APPEND|O_CREAT|O_SYNC|O_CLOEXEC, 0600);
        if( fd < 0 )
            goto fail;

        if( fd < 0 || dup2( fd, STDERR_FILENO ) < 0 )
        {
            perror( file );
            goto fail;
        }

        close( fd );
    }

    max_level = level;
    return 1;
fail:
    close( lockfd );
    return 0;
}

void log_printf( int level, const char* fmt, ... )
{
    const char* lvstr;
    char temp[128];
    struct tm stm;
    uint64_t val;
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

    /* lock */
    read(lockfd, &val, 8);

    /* generate log line */
    fprintf(stderr, "%s[%d]%s ", temp, getpid(), lvstr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);

    /* unlock */
    val = 1;
    write(lockfd, &val, 8);

    va_end( ap );
}

void print_stacktrace( void )
{
    void *buffer[100];
    char **strings;
    int j, nptrs;
    uint64_t val;
    pid_t pid;

    nptrs = backtrace( buffer, sizeof(buffer)/sizeof(buffer[0]) );
    strings = backtrace_symbols( buffer, nptrs );
    pid = getpid();

    /* lock */
    read(lockfd, &val, 8);

    /* print stack trace */
    if( strings )
    {
        for( j = 0; j < nptrs; ++j )
            fprintf( stderr, "[%d] %s\n", pid, strings[j] );
    }
    else
    {
        fprintf(stderr, "[%d] backtrace_symbols: %s\n", pid, strerror(errno));
    }

    /* unlock */
    val = 1;
    write(lockfd, &val, 8);

    free( strings );
}

