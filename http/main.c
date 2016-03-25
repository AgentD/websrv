#include <sys/wait.h>
#include <arpa/inet.h>
#include <setjmp.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <poll.h>

#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "config.h"
#include "error.h"
#include "proxy.h"
#include "http.h"
#include "file.h"
#include "conf.h"
#include "sock.h"
#include "rest.h"
#include "log.h"

#define KEEPALIVE_TIMEOUT_MS 2000
#define MAX_REQUEST_SECONDS 5
#define MAX_REQUESTS 1000

#define ERR_ALARM -1
#define ERR_SEGFAULT -2
#define ERR_PIPE -3

static sig_atomic_t run = 1;
static sigjmp_buf watchdog;
static const char* configfile;
static pid_t main_pid = -1;

static void sighandler( int sig )
{
    if( sig == SIGTERM || sig == SIGINT )
        run = 0;
    if( sig == SIGCHLD )
        wait( NULL );
    if( sig == SIGALRM )
        longjmp(watchdog,ERR_ALARM);
    if( sig == SIGSEGV )
        longjmp(watchdog,ERR_SEGFAULT);
    if( sig == SIGPIPE )
        longjmp(watchdog,ERR_PIPE);
    if( sig == SIGUSR1 && getpid( ) == main_pid )
    {
        INFO("re-reading config file %s", configfile);
        config_cleanup( );
        config_read( configfile );
    }
    signal( sig, sighandler );
}

static int read_header( int fd, http_request* req, char* buffer, size_t size )
{
    char line[512];

    if( !read_line( fd, line, sizeof(line) ) )
        return 0;
    if( !http_request_init( req, line, buffer, size ) )
        goto out;

    INFO( "Request: %s", line );

    while( read_line( fd, line, sizeof(line) ) )
    {
        if( !line[0] )
            return 1;
        if( !http_parse_attribute( req, line ) )
            break;
    }
out:
    DBG( "Error parsing line '%s'", line );
    return 0;
}

static void handle_client( int fd )
{
    http_file_info info;
    char buffer[2048];
    http_request req;
    size_t count;
    cfg_host* h;
    string page;
    int ret;

    if( (ret = setjmp(watchdog))!=0 )
    {
        if( ret == ERR_SEGFAULT )
        {
            alarm(0);
            CRITICAL( "SEGFAULT!! Host: '%s', Request: %s/%s",
                      req.host, http_method_to_string(req.method), req.path );
            ret = ERR_INTERNAL;
        }
        else if( ret == ERR_PIPE )
        {
            alarm(0);
            WARN("SIGPIPE! Host: '%s', Request: %s/%s",
                  req.host, http_method_to_string(req.method), req.path );
            return;
        }
        else
        {
            WARN( "Watchdog timeout! Host: '%s', Request: %s/%s",
                  req.host, http_method_to_string(req.method), req.path );
            ret = ERR_TIMEOUT;
        }
        goto fail;
    }

    for( count = 0; count < MAX_REQUESTS; ++count )
    {
        if( !wait_for_fd(fd,KEEPALIVE_TIMEOUT_MS) )
            return;

        ret = ERR_BAD_REQ;
        alarm( MAX_REQUEST_SECONDS );
        if( !read_header( fd, &req, buffer, sizeof(buffer) ) )
            goto fail;

        if( !(h = config_find_host( req.host )) )
            goto fail;

        ret = ERR_NOT_FOUND;
        if( !req.path || !req.path[0] )
            req.path = h->index;

        if( req.path && req.path[0] )
        {
            if( h->restdir )
                ret = rest_handle_request( fd, h, &req );
        #ifdef HAVE_PROXY
            if( h->proxydir && h->proxysock && ret == ERR_NOT_FOUND )
                ret = proxy_handle_request( fd, h, &req );
        #endif
            if( h->datadir > 0 && ret == ERR_NOT_FOUND )
                ret = http_send_file( h->datadir, fd, &req );
        }

        if( ret && gen_default_page( &page, &info, ret, req.accept, NULL ) )
        {
            http_response_header( fd, &info );
            write( fd, page.data, page.used );
            string_cleanup( &page );
        }
        alarm( 0 );
    }
    return;
fail:
    if( gen_default_page( &page, &info, ret, req.accept, NULL ) )
    {
        http_response_header( fd, &info );
        write( fd, page.data, page.used );
        string_cleanup( &page );
    }
    alarm( 0 );
}

static void usage( void )
{
    puts( "Usage: server [--port <num>] [--ipv4 <bind>] [--ipv6 <bind>]\n"
          "              [--unix <bind>] [--log <file>] [--loglevel <num>]\n"
          "              --cfg <configfile>\n\n"
          "  --ipv4, --ipv6 Create an IPv4/IPv6 socket. Either bind to a\n"
          "                 specific address, or use ANY.\n"
          "  --unix         Create a unix socket.\n"
          "  --port         Specify port number to use for TCP/IP\n"
          "  --cfg          Configuration file with virtual hosts\n"
          "  --log          Append log output to a specific file\n"
          "  --loglevel     Higher value means more verbose\n" );
}

int main( int argc, char** argv )
{
    int i, fd, port = -1, ret = EXIT_FAILURE, loglevel = LEVEL_WARNING;
    const char *errstr = NULL, *logfile = NULL;
    size_t j, count = 0, max = 0;
    struct pollfd* pfd = NULL;
    void* new;

    if( argc < 2 )
        goto usage;

    for( i=1; i<argc; ++i )
    {
        if( !strcmp(argv[i], "--help") || !strcmp(argv[i], "-h") )
            goto usage;
    }

    signal( SIGTERM, sighandler );
    signal( SIGINT, sighandler );
    signal( SIGCHLD, sighandler );
    signal( SIGALRM, sighandler );
    signal( SIGSEGV, sighandler );
    signal( SIGUSR1, sighandler );
    signal( SIGPIPE, sighandler );

    for( i=1; i<argc; ++i )
    {
        fd = -1;
        if( !strcmp(argv[i], "--ipv4") )
        {
            if( (i+1) > argc ) goto err_arg;
            if( port < 0     ) goto err_port;
            fd = create_socket(argv[++i], port, PF_INET);
        }
        else if( !strcmp(argv[i], "--ipv6") )
        {
            if( (i+1) > argc ) goto err_arg;
            if( port < 0     ) goto err_port;
            fd = create_socket(argv[++i], port, PF_INET6);
        }
        else if( !strcmp(argv[i], "--unix") )
        {
            if( (i+1) > argc ) goto err_arg;
            fd = create_socket(argv[++i], port, AF_UNIX);
        }
        else if( !strcmp(argv[i], "--port") )
        {
            if( (i+1) > argc )
                goto err_arg;
            for( port=0, j=0; isdigit(argv[i+1][j]); ++j )
                port = port * 10 + (argv[i+1][j] - '0');
            if( argv[i+1][j] )
                goto err_num;
            ++i;
        }
        else if( !strcmp(argv[i], "--cfg") )
        {
            if( (i+1) > argc )
                goto err_arg;
            configfile = argv[++i];
            if( !config_read( configfile ) )
            {
                fprintf( stderr, "Error reading host configuration '%s'\n",
                         argv[i] );
                goto fail;
            }
        }
        else if( !strcmp(argv[i], "--log") )
        {
            if( (i+1) >= argc )
                goto err_arg;
            logfile = argv[++i];
        }
        else if( !strcmp(argv[i], "--loglevel") )
        {
            if( (i+1) > argc )
                goto err_arg;
            for( loglevel=0, j=0; isdigit(argv[i+1][j]); ++j )
                loglevel = loglevel * 10 + (argv[i+1][j] - '0');
            if( argv[i+1][j] )
                goto err_num;
            ++i;
        }
        else
        {
            fprintf( stderr, "Unknown option %s\n\n", argv[i] );
            goto fail;
        }

        if( fd > 0 )
        {
            if( count == max )
            {
                max += 10;
                new = realloc( pfd, sizeof(pfd[0]) * max );
                if( !new )
                    goto err_alloc;
                pfd = new;
            }

            pfd[count].events = POLLIN;
            pfd[count].fd = fd;
            ++count;
        }
    }

    if( !log_init( logfile, loglevel ) )
        goto out;

    if( !count )
    {
        CRITICAL( "No open sockets!" );
        goto fail;
    }

    main_pid = getpid( );

    while( run )
    {
        if( poll( pfd, count, -1 )<=0 )
            continue;

        for( j=0; j<count; ++j )
        {
            if( !(pfd[j].revents & POLLIN) )
                continue;

            fd = accept( pfd[j].fd, NULL, NULL );

            if( fd >= 0 && fork( ) == 0 )
            {
                handle_client( fd );
                exit( EXIT_SUCCESS );
            }

            if( fd >= 0 )
                close( fd );
        }
    }

    signal( SIGCHLD, SIG_IGN );
    while( wait(NULL)!=-1 ) { }

    /* HUGE ASS diagnostics and cleanup */
    ret = EXIT_SUCCESS;
out:
    INFO("shutting down");
    config_cleanup( );
    for( j=0; j<count; ++j )
        close( pfd[j].fd );
    free( pfd );
    log_cleanup( );
    return ret;
err_num:   errstr = "Expected a numeric argument for"; goto err_print;
err_arg:   errstr = "Missing argument for";            goto err_print;
err_port:  errstr = "Port must be specified _before_"; goto err_print;
err_print: fprintf(stderr, "%s option %s\n\n", errstr, argv[i]); goto fail;
fail:
    fprintf(stderr, "Try '%s --help' for more information\n\n", argv[0]);
    goto out;
err_alloc: fputs("Out of memory\n\n", stderr);         goto out;
usage:
    usage( );
    return EXIT_SUCCESS;
}

