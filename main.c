#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>

#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "http.h"
#include "file.h"
#include "conf.h"

#define KEEPALIVE_TIMEOUT_MS 2000

static volatile int run = 1;

static int wait_for_fd( int fd, long timeoutms )
{
    struct pollfd pfd;

    pfd.fd = fd;
    pfd.events = POLLIN|POLLRDHUP;
    pfd.revents = 0;

    return poll( &pfd, 1, timeoutms )>0 && (pfd.revents & POLLIN);
}

static void handle_client( cfg_server* server, int fd )
{
    char buffer[ 512 ], *data = NULL;
    ssize_t count, i;
    http_request req;
    cfg_host* h;
next:
    /* receive header */
    i = 0;
    do
    {
        count = read( fd, buffer+i, sizeof(buffer)-i-1 );
        if( count<=0 )
            return;

        i += count;
        if( (size_t)i >= (sizeof(buffer)-1) )
            return;

        buffer[i] = '\0';
        data = strstr( buffer, "\r\n\r\n" );
        data = data ? data : strstr( buffer, "\n\n" );
    }
    while( !data );

    data += data[0]=='\r' ? 4 : 2;
    data[-1] = '\0';

    /* parse header */
    if( !http_request_parse( buffer, &req ) )
        return;

    /* send file */
    h = config_find_host( server, req.host );

    if( !h )
        return;

    if( req.path && strlen(req.path) )
        http_send_file( req.method, fd, req.path, h->datadir );
    else if( h->index )
        http_send_file( req.method, fd, h->index, h->datadir );

    /* keep-alive */
    if( wait_for_fd( fd, KEEPALIVE_TIMEOUT_MS ) )
        goto next;
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

static void setup_socket( struct pollfd* pfd, int proto, unsigned int port,
                          void* sin, size_t sinsize )
{
    int val;

    pfd->fd = socket( proto, SOCK_STREAM, IPPROTO_TCP );
    pfd->events = POLLIN;

    if( pfd->fd<=0 )
    {
        fprintf( stderr, "Cannot create socket: %s\n", strerror(errno) );
        return;
    }

    val=1; setsockopt( pfd->fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val) );
    val=1; setsockopt( pfd->fd, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val) );

    if( bind( pfd->fd, sin, sinsize )!=0 )
    {
        fprintf( stderr, "Cannot bind socket to port %d: %s\n",
                 ntohs(port), strerror(errno) );
        return;
    }
    if( listen( pfd->fd, 10 )!=0 )
    {
        fprintf( stderr, "Cannot listen on port %d: %s\n",
                 ntohs(port), strerror(errno) );
        return;
    }
}

int main( int argc, char** argv )
{
    size_t i, j, count, raw_count;
    struct sockaddr_in6 sin6;
    struct sockaddr_in sin;
    struct pollfd* pfd;
    cfg_server* slist;
    int fd;

    if( argc<2 )
    {
        puts( "Usage: server <configfile>" );
        return EXIT_SUCCESS;
    }

    if( !config_read( argv[1] ) )
        return EXIT_FAILURE;

    /* create sockets and pollfds for all server configs */
    slist = config_get_servers( &raw_count );

    for( count=0, i=0; i<raw_count; ++i )
    {
        count += slist[i].ipv4 ? 1 : 0;
        count += slist[i].ipv6 ? 1 : 0;
    }

    pfd = calloc( count, sizeof(struct pollfd) );

    for( i=0, j=0; i<raw_count; ++i )
    {
        if( slist[i].ipv4 )
        {
            memset( &sin, 0, sizeof(sin) );
            sin.sin_family = AF_INET;
            sin.sin_port   = htons( slist[i].port );

            if( strcmp(slist[i].ipv4,"*") )
                inet_pton( AF_INET, slist[i].ipv4, &sin.sin_addr );
            else
                sin.sin_addr.s_addr = INADDR_ANY;

            setup_socket( pfd+j, PF_INET, slist[i].port, &sin, sizeof(sin) );
            slist[i].fd4 = pfd[j].fd;
            ++j;
        }
        if( slist[i].ipv6 )
        {
            memset( &sin6, 0, sizeof(sin6) );
            sin6.sin6_family = AF_INET6;
            sin6.sin6_port   = htons( slist[i].port );

            if( strcmp(slist[i].ipv6,"*") )
                inet_pton( AF_INET6, slist[i].ipv6, &sin6.sin6_addr );
            else
                sin6.sin6_addr = in6addr_any;

            setup_socket(pfd+j, PF_INET6, slist[i].port, &sin6, sizeof(sin6));
            slist[i].fd6 = pfd[j].fd;
            ++j;
        }
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

