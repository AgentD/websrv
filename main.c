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

static void handle_client( cfg_server* server, int fd )
{
    char buffer[ 512 ], c, *ptr, *path;
    size_t i, len, count;
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

        for( i=0; i<sizeof(buffer); )
        {
            if( read( fd, &c, 1 )!=1 )
                return;
            if( isspace( c ) && c!='\n' )
                c = ' ';
            if( c==' ' && (!i || buffer[i-1]==' ' || buffer[i-1]=='\n') )
                continue;
            if( c=='\n' && i && buffer[i-1]=='\n' )
            {
                buffer[i] = '\0';
                break;
            }
            buffer[ i++ ] = c;
        }

        if( i>=sizeof(buffer) || !http_request_parse( buffer, &req ) )
            goto fail;

        if( !(h = config_find_host( server, req.host )) )
            goto fail;

        if( chdir( h->datadir )!=0 )
        {
            ret = ERR_INTERNAL;
            goto fail;
        }

        path = (req.path && strlen(req.path)) ? req.path : h->index;
        if( !path )
            goto fail;

        if( h->restdir )
        {
            for( ptr=h->restdir; *ptr=='/'; ++ptr ) { }

            len = strlen(ptr);
            while( len && ptr[len-1]=='/' )
                --len;

            if( !strncmp( path, ptr, len ) && (path[len]=='/' || !path[len]) )
            {
                for( path+=len; *path=='/'; ++path ) { }
                req.path = path;
                ret = rest_handle_request( fd, &req );
                goto done;
            }
        }

        if( h->zipfd > 0 )
        {
            ret = send_zip( req.method, fd, req.ifmod, path, h->zipfd );
            if( ret != ERR_NOT_FOUND )
                goto done;
        }
        ret = http_send_file( req.method, fd, req.ifmod, path );
    done:
        if( ret )
            gen_error_page( fd, ret );
        alarm( 0 );
    }
    return;
fail:
    gen_error_page( fd, ret );
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

static void server_main( cfg_server* srv )
{
    struct pollfd pfd[3];
    size_t i, count = 0;
    int fd;

    signal( SIGTERM, child_sighandler );
    signal( SIGINT, child_sighandler );
    signal( SIGCHLD, child_sighandler );
    signal( SIGALRM, child_sighandler );
    signal( SIGPIPE, SIG_IGN );

    if( srv->ipv4 )
        pfd[count++].fd = create_socket(srv->ipv4, srv->port, PF_INET);
    if( srv->ipv6 )
        pfd[count++].fd = create_socket(srv->ipv6, srv->port, PF_INET6);
    if( srv->unix )
        pfd[count++].fd = create_socket(srv->unix, srv->port, AF_UNIX);

    for( i=0; i<count; ++i )
        pfd[i].events = POLLIN;

    while( run )
    {
        if( poll( pfd, count, -1 )<=0 )
            continue;

        for( i=0; i<count; ++i )
        {
            if( !(pfd[i].revents & POLLIN) )
                continue;

            fd = accept( pfd[i].fd, NULL, NULL );

            if( fork( ) == 0 )
            {
                handle_client( srv, fd );
                exit( EXIT_SUCCESS );
            }

            close( fd );
        }
    }

    for( i=0; i<count; ++i )
        close( pfd[i].fd );

    signal( SIGCHLD, SIG_IGN );
    while( wait(NULL)!=-1 ) { }

    if( srv->unix )
        unlink( srv->unix );
}

int main( int argc, char** argv )
{
    cfg_server* srv;

    if( argc<2 )
    {
        puts( "Usage: server <configfile>" );
        return EXIT_SUCCESS;
    }

    if( !config_read( argv[1] ) )
    {
        fputs( "Malformed configuration file!\n", stderr );
        return EXIT_FAILURE;
    }

    srv = config_fork_servers( );

    if( srv )
    {
        server_main( srv );
    }
    else
    {
        signal( SIGTERM, config_kill_all_servers );
        signal( SIGINT, config_kill_all_servers );

        while( wait(NULL)!=-1 ) { }
    }

    config_cleanup( );
    return EXIT_SUCCESS;
}

