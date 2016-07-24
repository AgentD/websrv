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
#include "http.h"
#include "file.h"
#include "conf.h"
#include "sock.h"
#include "rest.h"
#include "log.h"

#define ERR_ALARM -1
#define ERR_SEGFAULT -2

static const struct option options[] =
{
    { "cfg", required_argument, NULL, 'c' },
    { "log", required_argument, NULL, 'f' },
    { "loglevel", required_argument, NULL, 'l' },
    { "chroot", required_argument, NULL, 'r' },
    { "help", no_argument, NULL, 'h' },
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
    if( getpid( ) == main_pid )
    {
        if( sig == SIGHUP )
        {
            INFO("re-reading config file %s", configfile);
            config_cleanup( );
            config_read( configfile );
            config_set_user( );
        }
    }
    else
    {
        if( sig == SIGALRM )
            longjmp( watchdog, ERR_ALARM );
        if( sig == SIGSEGV )
        {
            print_stacktrace( );
            longjmp( watchdog, ERR_SEGFAULT );
        }
    }
}

static int read_header( sock_t* sock, http_request* req,
                        char* buffer, size_t size )
{
    char line[512];
    int ret;

    ret = read_line( sock, line, sizeof(line), KEEPALIVE_TIMEOUT_MS );
    if( ret == 0 ) return ERR_BAD_REQ;
    if( ret <  0 ) return ERR_TIMEOUT;

    if( !http_request_init( req, line, buffer, size ) )
    {
        DBG( "Error parsing line '%s'", line );
        return ERR_BAD_REQ;
    }

    INFO( "Request: %s", line );

    while( 1 )
    {
        ret = read_line( sock, line, sizeof(line), KEEPALIVE_TIMEOUT_MS );
        if( ret == 0 ) return ERR_BAD_REQ;
        if( ret <  0 ) return ERR_TIMEOUT;
        if( !line[0] )
            break;
        http_parse_attribute( req, line );
    }
    return 0;
}

static void handle_client( sock_t* sock )
{
    http_file_info info;
    char buffer[2048];
    http_request req;
    size_t count;
    cfg_host* h;
    string page;
    int ret;

    memset( &req, 0, sizeof(req) );
    req.method = -1;

    if( (ret = setjmp(watchdog))!=0 )
    {
        alarm(0);
        goto fail_sig;
    }

    for( count = 0; count < MAX_REQUESTS; ++count )
    {
        if( !sock_wait( sock, KEEPALIVE_TIMEOUT_MS ) )
            break;

        alarm( MAX_REQUEST_SECONDS );

        ret = read_header( sock, &req, buffer, sizeof(buffer) );
        if( ret != 0 )
            goto fail;

        ret = ERR_BAD_REQ;
        if( !(h = config_find_host( req.host )) )
            goto fail;

        ret = ERR_NOT_FOUND;
        if( !req.path || !req.path[0] )
            req.path = h->rootfile;

        if( req.path && req.path[0] )
        {
        #ifdef HAVE_REST
            if( h->restdir && ret == ERR_NOT_FOUND )
                ret = rest_handle_request( sock, h, &req );
        #endif
        #ifdef HAVE_STATIC
            if( h->datadir > 0 && ret == ERR_NOT_FOUND )
            {
                alarm( MAX_FILEXFER_TIMEOUT );
                ret = http_send_file( h->datadir, sock->fd, &req );
            }
        #endif
        }

        if( ret && gen_default_page( &page, &info, ret, req.accept, NULL ) )
        {
            http_response_header( sock->fd, &info );
            write( sock->fd, page.data, page.used );
            string_cleanup( &page );
        }

        alarm( 0 );

        if( req.flags & REQ_CLOSE )
            break;
    }
    return;
fail_sig:
    if( ret == ERR_SEGFAULT )
    {
        CRITICAL( "SEGFAULT!! Host: '%s', Request: %s/%s",
                  req.host, http_method_to_string(req.method), req.path );
        ret = ERR_INTERNAL;
    }
    else
    {
        WARN( "Watchdog timeout! Host: '%s', Request: %s/%s",
              req.host, http_method_to_string(req.method), req.path );
        ret = ERR_SRV_TIMEOUT;
    }
fail:
    if( gen_default_page( &page, &info, ret, req.accept, NULL ) )
    {
        http_response_header( sock->fd, &info );
        write( sock->fd, page.data, page.used );
        string_cleanup( &page );
    }
    alarm( 0 );
}

static void usage( int status )
{
    fputs( "Usage: server [--log <file>] [--loglevel <num>]\n"
           "              [--chroot <path>] --cfg <configfile>\n\n"
           "  -c, --cfg      Configuration file with virtual hosts\n"
           "  -f, --log      Append log output to a specific file\n"
           "  -l, --loglevel Higher value means more verbose\n"
           "  -r, --chroot   Set the root directory to this\n",
           status==EXIT_FAILURE ? stderr : stdout );
    exit( status );
}

int main( int argc, char** argv )
{
    int i, fd, ret = EXIT_FAILURE, loglevel = LEVEL_WARNING;
    const char *logfile = NULL, *rootdir = NULL;
    size_t j, count = 0, max = 0;
    struct pollfd* pfd = NULL;
    cfg_socket *s, *sockets;
    struct sigaction act;
    sock_t* wrapper;
    void* new;

    main_pid = getpid( );
    memset( &act, 0, sizeof(act) );
    act.sa_handler = sighandler;
    sigaction( SIGTERM, &act, NULL );
    sigaction( SIGINT, &act, NULL );
    sigaction( SIGCHLD, &act, NULL );
    sigaction( SIGHUP, &act, NULL );
    sigaction( SIGALRM, &act, NULL );
    sigaction( SIGSEGV, &act, NULL );
    act.sa_handler = SIG_IGN;
    sigaction( SIGPIPE, &act, NULL );

    while( (i=getopt_long(argc,argv,"c:f:l:r:h",options,NULL)) != -1 )
    {
        switch( i )
        {
        case 'l':
            for( loglevel=0, j=0; isdigit(optarg[j]); ++j )
                loglevel = loglevel * 10 + (optarg[j] - '0');
            if( optarg[j] )
                goto err_num;
            break;
        case 'c': configfile = optarg; break;
        case 'f': logfile    = optarg; break;
        case 'r': rootdir    = optarg; break;
        case 'h': usage(EXIT_SUCCESS);
        default:  usage(EXIT_FAILURE);
        }
    }

    if( optind < argc )
    {
        WARN( "unknown extra arguments" );
        usage(EXIT_FAILURE);
    }

    if( !configfile )
    {
        CRITICAL( "No config file specified!" );
        goto fail;
    }

    if( !log_init( logfile, loglevel ) )
        goto out;

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

    if( !config_read( configfile ) )
    {
        CRITICAL( "Error reading host configuration '%s'\n", configfile );
        goto out;
    }

    sockets = config_get_sockets( );
    for( s = sockets; s != NULL; s = s->next )
    {
        fd = create_socket( s->bind, s->port, s->type );
        if( fd < 0 )
            goto out;

        if( s->type == AF_UNIX && chmod( s->bind, 0777 ) != 0 )
        {
            CRITICAL( "chmod %s: ", s->bind, strerror(errno) );
            close( fd );
            goto out;
        }

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

    if( !count )
    {
        CRITICAL( "No open sockets!" );
        goto fail;
    }

    if( !config_set_user( ) )
        goto fail;

    if( clearenv( ) != 0 )
    {
        CRITICAL( "clearenv: %s", strerror(errno) );
        environ = NULL;
        goto fail;
    }

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
                wrapper = create_wrapper( fd );
                if( !wrapper )
                    exit( EXIT_FAILURE );
                handle_client( wrapper );
                destroy_wrapper( wrapper );
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
    return ret;
err_num:
    fprintf(stderr, "Expected numeric argument, found '%s'\n", optarg);
    goto fail;
fail:
    fprintf(stderr, "Try '%s --help' for more information\n\n", argv[0]);
    goto out;
err_alloc:
    fputs("Out of memory\n\n", stderr);
    goto out;
}

