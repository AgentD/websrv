#include <sys/stat.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <setjmp.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <poll.h>

#include <getopt.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

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

static const struct option options[] =
{
    { "ipv4", required_argument, NULL, '4' },
    { "ipv6", required_argument, NULL, '6' },
    { "unix", required_argument, NULL, 'u' },
    { "port", required_argument, NULL, 'p' },
    { "cfg", required_argument, NULL, 'c' },
    { "log", required_argument, NULL, 'f' },
    { "loglevel", required_argument, NULL, 'l' },
    { "chroot", required_argument, NULL, 'r' },
    { NULL, 0, NULL, 0 },
};

static sig_atomic_t run = 1;
static sigjmp_buf watchdog;
static const char* configfile;
static pid_t main_pid = -1;

static void sighandler( int sig )
{
    if( sig == SIGTERM || sig == SIGINT )
        run = 0;
    if( sig == SIGCHLD )
    {
        while( waitpid( -1, NULL, WNOHANG )!=-1 ) { }
    }
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
        config_set_user( );
    }
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
        #ifdef HAVE_REST
            if( h->restdir && ret == ERR_NOT_FOUND )
                ret = rest_handle_request( fd, h, &req );
        #endif
        #ifdef HAVE_PROXY
            if( h->proxydir && h->proxysock && ret == ERR_NOT_FOUND )
                ret = proxy_handle_request( fd, h, &req );
        #endif
        #ifdef HAVE_STATIC
            if( h->datadir > 0 && ret == ERR_NOT_FOUND )
                ret = http_send_file( h->datadir, fd, &req );
        #endif
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

static void usage( int status )
{
    fputs( "Usage: server [--port <num>] [--ipv4 <bind>] [--ipv6 <bind>]\n"
           "              [--unix <bind>] [--log <file>] [--loglevel <num>]\n"
           "              [--chroot <path>] --cfg <configfile>\n\n"
           "  -4, --ipv4     Create an IPv4/IPv6 socket. Either bind to a\n"
           "  -6, --ipv6     specific address, or use ANY.\n\n"
           "  -u, --unix     Create a unix socket.\n"
           "  -p, --port     Specify port number to use for TCP/IP\n"
           "  -c, --cfg      Configuration file with virtual hosts\n"
           "  -f, --log      Append log output to a specific file\n"
           "  -l, --loglevel Higher value means more verbose\n"
           "  -r, --chroot   Set the root directory to this\n",
           status==EXIT_FAILURE ? stderr : stdout );
    exit( status );
}

int main( int argc, char** argv )
{
    int i, fd, port = -1, ret = EXIT_FAILURE, loglevel = LEVEL_WARNING;
    const char *logfile = NULL, *rootdir = NULL;
    size_t j, count = 0, max = 0;
    struct pollfd* pfd = NULL;
    struct sigaction act;
    void* new;

    memset( &act, 0, sizeof(act) );
    act.sa_handler = sighandler;
    sigaction( SIGTERM, &act, NULL );
    sigaction( SIGINT, &act, NULL );
    sigaction( SIGCHLD, &act, NULL );
    sigaction( SIGUSR1, &act, NULL );

    for( i=1; i<argc; ++i )
    {
        if( !strcmp(argv[i], "--help") || !strcmp(argv[i], "-h") )
            usage(EXIT_SUCCESS);
    }

    while( (i=getopt_long(argc,argv,"4:6:u:p:c:f:l:r:",options,NULL)) != -1 )
    {
        fd = -1;
        if( (i=='4' || i=='6') && (port < 0 || port > 0xFFFF) )
            goto err_port;

        switch( i )
        {
        case '4': fd = create_socket(optarg, port, PF_INET); break;
        case '6': fd = create_socket(optarg, port, PF_INET6); break;
        case 'u':
            fd = create_socket(optarg, port, AF_UNIX);
            chmod(optarg, 0777);
            break;
        case 'p':
            for( port=0, j=0; isdigit(optarg[j]); ++j )
                port = port * 10 + (optarg[j] - '0');
            if( optarg[j] )
                goto err_num;
            break;
        case 'l':
            for( loglevel=0, j=0; isdigit(optarg[j]); ++j )
                loglevel = loglevel * 10 + (optarg[j] - '0');
            if( optarg[j] )
                goto err_num;
            break;
        case 'c': configfile = optarg; break;
        case 'f': logfile    = optarg; break;
        case 'r': rootdir    = optarg; break;
        default:
            usage(EXIT_FAILURE);
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

    if( optind < argc )
    {
        WARN( "unknown extra arguments" );
        usage(EXIT_FAILURE);
    }

    if( rootdir )
    {
        if( chdir( rootdir ) != 0 )
        {
            CRITICAL( "chdir: %s", strerror(errno) );
            goto out;
        }
        if( chroot( rootdir ) != 0 )
        {
            CRITICAL( "chroot: %s", strerror(errno) );
            goto out;
        }
    }

    if( !count )
    {
        CRITICAL( "No open sockets!" );
        goto fail;
    }

    if( !configfile )
    {
        CRITICAL( "No config file specified!" );
        goto fail;
    }

    if( !config_read( configfile ) )
    {
        CRITICAL( "Error reading host configuration '%s'\n", configfile );
        goto fail;
    }

    if( !config_set_user( ) )
        goto fail;

    if( !log_init( logfile, loglevel ) )
        goto out;

    if( clearenv( ) != 0 )
    {
        CRITICAL( "clearenv: %s", strerror(errno) );
        environ = NULL;
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
                sigaction( SIGALRM, &act, NULL );
                sigaction( SIGSEGV, &act, NULL );
                sigaction( SIGPIPE, &act, NULL );
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
err_num:
    fprintf(stderr, "Expected numeric argument, found '%s'\n", optarg);
    goto fail;
err_port:
    fputs("A vaild port number must be specified before -4 or -6\n", stderr);
    goto fail;
fail:
    fprintf(stderr, "Try '%s --help' for more information\n\n", argv[0]);
    goto out;
err_alloc:
    fputs("Out of memory\n\n", stderr);
    goto out;
}

