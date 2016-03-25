#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#include <string.h>
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

    if( netproto==PF_INET )
    {
        sin->sin_family      = AF_INET;
        sin->sin_addr.s_addr = INADDR_ANY;
        sin->sin_port        = htons( port );

        if( addr && strcmp(addr, "ANY") )
            inet_pton( AF_INET, addr, &(sin->sin_addr) );

        return sizeof(*sin);
    }

    if( netproto==PF_INET6 )
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

int read_line( int fd, char* buffer, size_t size )
{
    size_t i = 0;
    char c;

    while( 1 )
    {
        if( read(fd, &c, 1) != 1 || i == size )
            return 0;
        if( isspace(c) && c != '\n' )
            c = ' ';
        if( c == '\\' )
            c = '/';
        if( c == '/' && i && buffer[i-1] == '/' )
            continue;
        if( c == ' ' && (!i || (buffer[i-1] == ' ')) )
            continue;
        if( c == '\n' )
            break;
        buffer[i++] = c;
    }

    while( i > 0 && isspace(buffer[i - 1]) )
        --i;

    buffer[i] = '\0';
    return 1;
}

