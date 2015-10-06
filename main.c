#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <unistd.h>
#include <poll.h>

#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

#include "http.h"
#include "file.h"
#include "conf.h"

static volatile int run = 1;

static int hextoi( int c )
{
    return isdigit(c) ? (c-'0') : (isupper(c) ? (c-'A'+10) : (c-'a'+10));
}

static int fix_path( char* path, size_t len )
{
    size_t i = 0, j;

    while( i < len )
    {
        for( j=i; path[j]!='/' && path[j]; ++j ) { }

        if( !strncmp( path+i, ".", j-i ) || !strncmp( path+i, "..", j-i ) )
        {
            if( path[i]=='.' && path[i+1]=='.' )
            {
                if( i<2 )
                    return 0;
                for( i-=2; i>0 && path[i]!='/'; --i ) { }
            }

            if( !path[j] )
            {
                path[i] = '\0';
                break;
            }

            memmove( path+i, path+j+1, len-j );
        }
        else
        {
            i = j + 1;
        }
    }
    return 1;
}

static void handle_client( int fd )
{
    char buffer[ 512 ], *path = NULL, *host = NULL, *data = NULL;
    ssize_t count, i, j;
    size_t length = 0;
    int method = -1;
    cfg_host* h;

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

    /* parse method */
         if( !strncmp(buffer,"GET",   3) ) { method = HTTP_GET;    i=3; }
    else if( !strncmp(buffer,"HEAD",  4) ) { method = HTTP_HEAD;   i=4; }
    else if( !strncmp(buffer,"POST",  4) ) { method = HTTP_POST;   i=4; }
    else if( !strncmp(buffer,"PUT",   3) ) { method = HTTP_PUT;    i=3; }
    else if( !strncmp(buffer,"DELETE",6) ) { method = HTTP_DELETE; i=6; }

    if( method<0 || !isspace(buffer[i]) )
        return;

    while( buffer[i] && isspace(buffer[i]) ) { ++i; }

    /* isolate path */
    while( buffer[i]=='/' || buffer[i]=='\\' ) { ++i; }
    path = buffer + i;

    for( j=0; !isspace(buffer[i]) && buffer[i]; ++j )
    {
        if( buffer[i]=='%' && isxdigit(buffer[i+1]) && isxdigit(buffer[i+2]) )
        {
            path[j] = (hextoi(buffer[i+1])<<4) | hextoi(buffer[i+2]);
            i += 3;
        }
        else
        {
            path[j] = buffer[i++];

            if( path[j]=='/' || path[j]=='\\' )
            {
                path[j] = '/';
                while( buffer[i]=='/' || buffer[i]=='\\' )
                    ++i;
            }
        }
    }

    ++i;
    path[j] = '\0';
    fix_path( path, j );

    /* parse fields */
    while( buffer[i] )
    {
        /* skip current line */
        while( buffer[i] && buffer[i]!='\n' && buffer[i]!='\r' ) { ++i; }
        while( buffer[i] && isspace(buffer[i]) ) { ++i; }

        /* */
        if( (!strncmp( buffer+i, "Host:", 5 ) ||
             !strncmp( buffer+i, "host:", 5 )) && isspace(buffer[i+5]) )
        {
            for( i+=5; buffer[i]==' ' || buffer[i]=='\t'; ++i ) { }
            host = buffer + i;
            for( j=0; isalpha(host[j]); ++j ) { }
            host[j] = '\0';
            i += j + 1;
        }
        else if( !strncmp( buffer+i, "Content-Length:", 15 ) )
        {
            for( i+=15; buffer[i]==' ' || buffer[i]=='\t'; ++i ) { }
            length = strtol( buffer+i, NULL, 10 );
        }
    }

    /* send file */
    h = config_find_host( host );

    if( h )
    {
        if( path && strlen(path) )
            http_send_file( method, fd, path, h->datadir );
        else if( h->indexfile )
            http_send_file( method, fd, h->indexfile, h->datadir );
    }
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
    cfg_server *s, *slist;
    struct sockaddr_in6 sin6;
    struct sockaddr_in sin;
    struct pollfd* pfd;
    size_t i, count;
    int fd;

    if( argc<2 )
    {
        puts( "Usage: server <configfile>" );
        return EXIT_SUCCESS;
    }

    if( !config_read( argv[1] ) )
        return EXIT_FAILURE;

    /* create sockets and pollfds for all server configs */
    slist = config_get_servers( );

    for( count=0, s=slist; s!=NULL; s=s->next )
    {
        count += (s->flags & SERVER_IPV4) ? 1 : 0;
        count += (s->flags & SERVER_IPV6) ? 1 : 0;
    }

    pfd = calloc( count, sizeof(struct pollfd) );

    for( i=0, s=slist; s!=NULL; s=s->next )
    {
        if( s->flags & SERVER_IPV4 )
        {
            memset( &sin, 0, sizeof(sin) );
            sin.sin_family      = AF_INET;
            sin.sin_port        = s->port;
            sin.sin_addr.s_addr = s->bindv4;
            setup_socket( pfd + (i++), PF_INET, s->port, &sin, sizeof(sin) );
        }
        if( s->flags & SERVER_IPV6 )
        {
            memset( &sin6, 0, sizeof(sin6) );
            sin6.sin6_family = AF_INET6;
            sin6.sin6_port   = s->port;
            sin6.sin6_addr   = s->bindv6;
            setup_socket( pfd+(i++), PF_INET6, s->port, &sin6, sizeof(sin6) );
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
            if( pfd[i].revents & POLLIN )
            {
                fd = accept( pfd[i].fd, NULL, NULL );

                if( fork( ) == 0 )
                {
                    handle_client( fd );
                    close( fd );
                    goto out;
                }

                close( fd );
            }
        }
    }

out:
    /* cleanup */
    for( i=0; i<count; ++i )
        close( pfd[i].fd );

    free( pfd );
    config_cleanup( );
    return EXIT_SUCCESS;
}

