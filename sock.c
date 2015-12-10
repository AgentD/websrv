#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>

#include <string.h>
#include <stdio.h>

#include "sock.h"

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

        if( addr && strcmp(addr, "*") )
            inet_pton( AF_INET, addr, &(sin->sin_addr) );

        return sizeof(*sin);
    }

    if( netproto==PF_INET6 )
    {
        sin6->sin6_family = AF_INET6;
        sin6->sin6_addr   = in6addr_any;
        sin6->sin6_port   = htons( bind );

        if( addr && strcmp(addr, "*") )
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
    perror( "create_socket" );
    close( fd );
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
    perror( "connect_to" );
    close( fd );
    return -1;
}

int wait_for_fd( int fd, long timeoutms )
{
    struct pollfd pfd;

    pfd.fd = fd;
    pfd.events = POLLIN|POLLRDHUP;
    pfd.revents = 0;

    return poll( &pfd, 1, timeoutms )>0 && (pfd.revents & POLLIN);
}

