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

#include "error.h"
#include "http.h"
#include "file.h"
#include "conf.h"
#include "sock.h"
#include "rest.h"

#define KEEPALIVE_TIMEOUT_MS 2000
#define MAX_REQUEST_SECONDS 5
#define MAX_REQUESTS 1000

static volatile int run = 1;
static sigjmp_buf watchdog;

static void handle_client( int fd )
{
    char line[512], buffer[2048];
    size_t len, count;
    http_request req;
    cfg_host* h;
    int ret;

    if( setjmp(watchdog)!=0 )
    {
        ret = ERR_TIMEOUT;
        goto fail;
    }

    for( count = 0; count < MAX_REQUESTS; ++count )
    {
        if( !wait_for_fd(fd,KEEPALIVE_TIMEOUT_MS) )
            return;
        ret = ERR_BAD_REQ;
        alarm( MAX_REQUEST_SECONDS );

        if( !read_line( fd, line, sizeof(line) ) )
            goto fail;
        if( !http_request_init( &req, line, buffer, sizeof(buffer) ) )
            goto fail;

        while( 1 )
        {
            if( !read_line( fd, line, sizeof(line) ) )
                goto fail;
            if( !line[0] )
                break;
            if( !http_parse_attribute( &req, line ) )
                goto fail;
        }

        if( !(h = config_find_host( req.host )) )
            goto fail;

        if( !req.path || !req.path[0] )
        {
            req.path = h->index;
            if( !req.path || !req.path[0] )
                goto fail;
        }

        if( h->restdir )
        {
            len = strlen(h->restdir);

            if( !strncmp(req.path, h->restdir, len) &&
                (req.path[len]=='/' || !req.path[len]) )
            {
                for( req.path+=len; req.path[0]=='/'; ++req.path ) { }
                ret = rest_handle_request( fd, &req );
                goto done;
            }
        }

        if( h->zip > 0 )
        {
            ret = send_zip( h->zip, req.method, fd, req.ifmod, req.path,
                            req.accept );
            if( ret != ERR_NOT_FOUND )
                goto done;
        }

        ret = ERR_NOT_FOUND;
        if( h->datadir > 0 )
            ret = http_send_file( h->datadir, req.method, fd,
                                  req.ifmod, req.path );
    done:
        if( ret )
            gen_error_page( fd, ret, req.accept );
        alarm( 0 );
    }
    return;
fail:
    gen_error_page( fd, ret, req.accept );
    alarm( 0 );
}

/****************************************************************************/
static void child_sighandler( int sig )
{
    if( sig == SIGTERM || sig == SIGINT )
        run = 0;
    if( sig == SIGCHLD )
        wait( NULL );
    if( sig == SIGALRM )
        longjmp(watchdog,-1);
    signal( sig, child_sighandler );
}

int main( int argc, char** argv )
{
    int i, fd, port = -1, ret = EXIT_FAILURE;
    struct pollfd pfd[3];
    size_t j, count = 0;

    if( argc < 2 )
        goto usage;

    for( i=1; i<argc; ++i )
    {
        if( !strcmp(argv[i], "--help") || !strcmp(argv[i], "-h") )
            goto usage;
    }

    signal( SIGTERM, child_sighandler );
    signal( SIGINT, child_sighandler );
    signal( SIGCHLD, child_sighandler );
    signal( SIGALRM, child_sighandler );
    signal( SIGPIPE, SIG_IGN );

    for( i=1; i<argc; ++i )
    {
        fd = -1;
        if( !strcmp(argv[i], "--ipv4") )
        {
            if( ++i > argc ) goto err_arg;
            if( port < 0   ) goto err_port;
            fd = create_socket(argv[i], port, PF_INET);
        }
        else if( !strcmp(argv[i], "--ipv6") )
        {
            if( ++i > argc ) goto err_arg;
            if( port < 0   ) goto err_port;
            fd = create_socket(argv[i], port, PF_INET6);
        }
        else if( !strcmp(argv[i], "--unix") )
        {
            if( ++i > argc ) goto err_arg;
            fd = create_socket(argv[i], port, AF_UNIX);
        }
        else if( !strcmp(argv[i], "--port") )
        {
            if( ++i > argc )
                goto err_arg;
            for( port=0, j=0; isdigit(argv[i][j]); ++j )
                port = port * 10 + (argv[i][j] - '0');
            if( argv[i][j] )
                goto err_num;
        }
        else if( !strcmp(argv[i], "--cfg") )
        {
            if( ++i > argc )
                goto err_arg;
            if( !config_read( argv[i] ) )
            {
                fprintf( stderr, "Error reading host configuration '%s'\n",
                         argv[i] );
                goto fail;
            }
        }
        else
        {
            fprintf( stderr, "Unknown option %s\n\n", argv[i] );
            goto fail;
        }

        if( fd > 0 )
        {
            pfd[count].events = POLLIN;
            pfd[count].fd = fd;
            ++count;
        }
    }

    if( !count )
    {
        fputs( "No open sockets!\n", stderr );
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
    config_cleanup( );
    for( j=0; j<count; ++j )
        close( pfd[j].fd );
    return ret;
err_num:
    fprintf(stderr, "Expected a numeric argument for option %s\n\n", argv[i]);
    goto fail;
err_arg:
    fprintf(stderr, "Missing argument for option %s\n\n", argv[i]);
    goto fail;
err_port:
    fprintf(stderr, "Port must be specified _before_ option %s\n\n", argv[i]);
    goto fail;
fail:
    fprintf(stderr, "Try '%s --help' for more information\n\n", argv[0]);
    goto out;
usage:
    puts( "Usage: server [--port <num>] [--ipv4 <bind>] [--ipv6 <bind>]\n"
          "              [--unix <bind>] --cfg <configfile>\n\n"
          "  --ipv4, --ipv6 Create an IPv4/IPv6 socket. Either bind to a\n"
          "                 specific address, or use ANY.\n"
          "  --unix         Create a unix socket.\n"
          "  --port         Specify port number to use for TCP/IP\n"
          "  --cfg          Configuration file with virtual hosts\n" );
    return EXIT_SUCCESS;
}

