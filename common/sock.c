#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

#include "sock.h"
#include "log.h"

static int gen_address( int netproto, void* buffer, const char* addr,
                        int port, int* subproto )
{
    struct sockaddr_un* sun = buffer;
    struct sockaddr_in* sin = buffer;
    struct sockaddr_in6* sin6 = buffer;
    *subproto = 0;

    if( netproto==AF_UNIX )
    {
        if( !addr )
            return -1;

        sun->sun_family = AF_UNIX;
        strcpy( sun->sun_path, addr );
        return sizeof(*sun);
    }

    if( port<0 || port>0xFFFF )
        return -1;

    *subproto = IPPROTO_TCP;

    if( netproto==AF_INET )
    {
        sin->sin_family      = AF_INET;
        sin->sin_addr.s_addr = INADDR_ANY;
        sin->sin_port        = htons( port );

        if( addr && strcmp(addr, "ANY") )
            inet_pton( AF_INET, addr, &(sin->sin_addr) );

        return sizeof(*sin);
    }

    if( netproto==AF_INET6 )
    {
        sin6->sin6_family = AF_INET6;
        sin6->sin6_addr   = in6addr_any;
        sin6->sin6_port   = htons( port );

        if( addr && strcmp(addr, "ANY") )
            inet_pton( AF_INET6, addr, &(sin6->sin6_addr) );

        return sizeof(*sin6);
    }

    return -1;
}

int create_socket( const char* bindaddr, int bindport, int netproto )
{
    int val, fd, sinsize, subproto;
    char buffer[ 256 ];

    memset( buffer, 0, sizeof(buffer) );

    sinsize = gen_address( netproto, buffer, bindaddr, bindport, &subproto );

    if( sinsize<0 )
        return -1;

    if( netproto==AF_UNIX )
        unlink(bindaddr);

    fd = socket( netproto, SOCK_STREAM, subproto );

    if( fd<0 )
        goto fail;

    val=1; setsockopt( fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val) );
    val=1; setsockopt( fd, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val) );

    if( netproto == AF_INET6 )
    {
        val=1;
        setsockopt( fd, IPPROTO_IPV6, IPV6_V6ONLY, &val, sizeof(val) );
    }

    if( bind( fd, (void*)buffer, sinsize )!=0 )
        goto fail;

    if( listen( fd, 10 )!=0 )
        goto fail;

    return fd;
fail:
    CRITICAL( "create_socket: %s", strerror(errno) );
    if( fd >= 0 ) close( fd );
    return -1;
}

int connect_to( const char* addr, int port, int netproto )
{
    int fd, sinsize, subproto;
    char buffer[ 256 ];

    memset( buffer, 0, sizeof(buffer) );

    sinsize = gen_address( netproto, buffer, addr, port, &subproto );

    if( sinsize <  0)
        return -1;

    fd = socket( netproto, SOCK_STREAM, subproto );

    if( fd<=0 )
        goto fail;

    if( connect( fd, (void*)buffer, sinsize )!=0 )
        goto fail;

    return fd;
fail:
    CRITICAL( "connect_to: %s", strerror(errno) );
    if( fd >= 0 ) close( fd );
    return -1;
}

int wait_for_fd( int fd, long timeoutms )
{
    struct pollfd pfd;

    pfd.fd = fd;
    pfd.events = POLLIN|POLLRDHUP;
    pfd.revents = 0;

    if( poll( &pfd, 1, timeoutms )!=1 || !(pfd.revents & POLLIN) )
        return 0;

    if( pfd.revents & (POLLRDHUP|POLLERR|POLLHUP) )
        return 0;

    return 1;
}

void splice_to_sock( int* pfd, int filefd, int sockfd,
                     size_t filesize, size_t pipedata )
{
    ssize_t count;

    while( filesize || pipedata )
    {
        if( filesize )
        {
            count = splice(filefd, 0, pfd[1], 0, filesize, SPLICE_F_MOVE);
            if( count<0 )
                break;
            if( count==0 )
                filesize = 0;
            pipedata += count;
            filesize -= count;
        }
        if( pipedata )
        {
            count = splice(pfd[0], 0, sockfd, 0, pipedata, SPLICE_F_MOVE);
            if( count<=0 )
                break;
            pipedata -= count;
        }
    }
}

sock_t* create_wrapper( int fd )
{
    sock_t* sock = calloc( 1, sizeof(*sock) );

    if( sock )
        sock->fd = fd;

    return sock;
}

void destroy_wrapper( sock_t* sock )
{
    close( sock->fd );
    free( sock );
}

int sock_wait( sock_t* sock, long timeoutms )
{
    int diff;

    if( sock->size && sock->offset < sock->size )
        return 1;

    if( timeoutms && !wait_for_fd( sock->fd, timeoutms ) )
    {
        errno = EWOULDBLOCK;
        return -1;
    }

    diff = read( sock->fd, sock->buffer, sizeof(sock->buffer) );
    if( diff == 0 )
        return 0;
    if( diff < 0 )
        return -1;

    sock->offset = 0;
    sock->size = diff;
    return 1;
}

ssize_t sock_read( sock_t* sock, void* buffer, size_t size, long timeoutms )
{
    size_t have = sock->size - sock->offset;
    ssize_t diff;

    if( have >= size )
    {
        memcpy( buffer, sock->buffer + sock->offset, size );
        sock->offset += size;
        return size;
    }

    if( have )
    {
        memcpy( buffer, sock->buffer + sock->offset, have );
        buffer = (char*)buffer + have;
    }

    sock->offset = sock->size = 0;

    if( timeoutms && !wait_for_fd( sock->fd, timeoutms ) )
    {
        errno = EWOULDBLOCK;
        return -1;
    }

    diff = read( sock->fd, buffer, size - have );
    return diff <= 0 ? diff : ((ssize_t)have + diff);
}

int read_line( sock_t* sock, char* buffer, size_t size, long timeout )
{
    size_t i = 0;
    char c;
    int ret;

    while( 1 )
    {
        if( sock->offset >= sock->size )
        {
            ret = sock_wait( sock, timeout );
            if( ret <= 0 )
                return ret;
        }

        c = sock->buffer[sock->offset++];
        if( c == '\t' ) c = ' ';
        if( c == '\\' ) c = '/';
        if( c == '\n' ) break;
        if( isspace(c) && c != ' ' )
            continue;
        if( c == '/' && i && buffer[i-1] == '/' )
            continue;
        if( c == ' ' && (!i || (buffer[i-1] == ' ')) )
            continue;
        if( i == size )
            return 0;
        buffer[i++] = c;
    }

    while( i > 0 && isspace(buffer[i - 1]) )
        --i;

    buffer[i] = '\0';
    return 1;
}

