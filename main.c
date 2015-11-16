#include <sys/wait.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
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

static volatile int run = 1;

static void handle_client( cfg_server* server, int fd )
{
    char buffer[ 512 ], c, *ptr;
    http_request req;
    size_t i, len;
    cfg_host* h;

    while( wait_for_fd( fd, KEEPALIVE_TIMEOUT_MS ) )
    {
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
            goto fail400;

        if( !(h = config_find_host( server, req.host )) )
            goto fail400;

        if( h->restdir )
        {
            for( ptr=h->restdir; *ptr=='/'; ++ptr ) { }

            len = strlen(ptr);
            while( len && ptr[len-1]=='/' )
                --len;

            if( !strncmp( req.path, ptr, len ) )
            {
                for( req.path+=len; req.path[0]=='/'; ++req.path ) { }

                if( !rest_handle_request( fd, &req ) && req.length )
                    return;

                continue;
            }
        }

        if( req.path && strlen(req.path) )
            http_send_file( req.method, fd, req.path, h->datadir );
        else if( h->index )
            http_send_file( req.method, fd, h->index, h->datadir );
    }
    return;
fail400:
    gen_error_page( fd, ERR_BAD_REQ );
}

/****************************************************************************/
static void sighandler( int sig )
{
    if( sig == SIGTERM || sig == SIGINT )
        run = 0;
    if( sig == SIGCHLD )
        wait( NULL );
    signal( sig, sighandler );
}

int main( int argc, char** argv )
{
    size_t i, j, count, raw_count;
    struct pollfd* pfd;
    cfg_server* slist;
    int fd;

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

    /* create sockets and pollfds for all server configs */
    slist = config_get_servers( &raw_count );

    for( count=0, i=0; i<raw_count; ++i )
    {
        count += slist[i].ipv4 ? 1 : 0;
        count += slist[i].ipv6 ? 1 : 0;
    }

    if( !(pfd = calloc( count, sizeof(struct pollfd) )) )
    {
        perror("Allocating pollfd for sockets");
        return EXIT_FAILURE;
    }

    for( i=0; i<raw_count; ++i )
    {
        slist[i].fd4 = create_socket(slist[i].ipv4, slist[i].port, PF_INET );
        slist[i].fd6 = create_socket(slist[i].ipv6, slist[i].port, PF_INET6);
    }

    for( i=0; i<count; ++i )
        pfd[i].events = POLLIN;

    for( i=0, count=0; i<raw_count; ++i )
    {
        if( slist[i].fd4 > 0 ) pfd[count++].fd = slist[i].fd4;
        if( slist[i].fd6 > 0 ) pfd[count++].fd = slist[i].fd6;
    }

    /* hook signals to catch */
    signal( SIGTERM, sighandler );
    signal( SIGINT, sighandler );
    signal( SIGCHLD, sighandler );
    signal( SIGPIPE, SIG_IGN );

    /* accept and handle connections */
    while( run )
    {
        if( poll( pfd, count, -1 )<=0 )
            continue;

        for( i=0; i<count; ++i )
        {
            if( !(pfd[i].revents & POLLIN) )
                continue;

            fd = accept( pfd[i].fd, NULL, NULL );

            for( j=0; j<raw_count; ++j )
            {
                if( slist[j].fd4!=pfd[i].fd && slist[j].fd6!=pfd[i].fd )
                    continue;
                if( fork( ) != 0 )
                    break;
                handle_client( slist + j, fd );
                return EXIT_SUCCESS;
            }

            close( fd );
        }
    }

    /* cleanup */
    for( i=0; i<count; ++i )
        close( pfd[i].fd );

    free( pfd );
    config_cleanup( );
    return EXIT_SUCCESS;
}

