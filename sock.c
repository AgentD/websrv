#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>

#include <string.h>
#include <stdio.h>

#include "sock.h"

int create_socket( const char* bindaddr, int bindport, int netproto )
{
    char buffer[ 256 ];
    struct sockaddr_in6* sin6 = (struct sockaddr_in6*)buffer;
    struct sockaddr_in* sin = (struct sockaddr_in*)buffer;
    int val, fd, sinsize;

    if( !bindaddr || bindport<0 || bindport>0xFFFF )
        return -1;

    /* setup address structure */
    memset( buffer, 0, sizeof(buffer) );

    if( netproto==PF_INET )
    {
        sin->sin_family      = AF_INET;
        sin->sin_addr.s_addr = INADDR_ANY;
        sin->sin_port        = htons( bindport );
        sinsize = sizeof(*sin);

        if( strcmp(bindaddr, "*") )
            inet_pton( AF_INET, bindaddr, &(sin->sin_addr) );
    }
    else if( netproto==PF_INET6 )
    {
        sin6->sin6_family = AF_INET6;
        sin6->sin6_addr   = in6addr_any;
        sin6->sin6_port   = htons( bindport );
        sinsize = sizeof(*sin6);

        if( strcmp(bindaddr, "*") )
            inet_pton( AF_INET6, bindaddr, &(sin6->sin6_addr) );
    }
    else
    {
        return -1;
    }

    /* create socket */
    fd = socket( netproto, SOCK_STREAM, IPPROTO_TCP );

    if( fd<=0 )
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

int wait_for_fd( int fd, long timeoutms )
{
    struct pollfd pfd;

    pfd.fd = fd;
    pfd.events = POLLIN|POLLRDHUP;
    pfd.revents = 0;

    return poll( &pfd, 1, timeoutms )>0 && (pfd.revents & POLLIN);
}
