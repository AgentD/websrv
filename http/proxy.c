#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>

#include "config.h"
#include "proxy.h"
#include "sock.h"
#include "log.h"

#ifdef HAVE_PROXY
static const char* mustencode = "!#$&'()*+,/:;=?@[]";

static void write_url_encoded( int fd, const unsigned char* str, int ispath )
{
    int i;

    for( i=0; str[i]!=0; ++i )
    {
        if( str[i]=='/' && ispath )
            continue;

        if( (str[i] & 0x80) || strchr( mustencode, str[i] ) )
        {
            if( i )
                write( fd, str, i );

            dprintf( fd, "%%%02X", (int)str[i] );

            str = str + i + 1;
            i = -1;
        }
    }

    if( i )
        write( fd, str, i );
}

static void write_encoded_list( int fd, const char* field, const char* list,
                                int count, char sep )
{
    const char* ptr;
    int i;

    write( fd, field, strlen(field) );
    ptr = list;

    for( i=0; i<count; ++i )
    {
        write_url_encoded( fd, (const unsigned char*)ptr, 0 );
        ptr += strlen(ptr);
        write( fd, "=", 1 );
        write_url_encoded( fd, (const unsigned char*)ptr, 0 );
        ptr += strlen(ptr);

        if( (i+1) < count )
            write( fd, &sep, 1 );
    }
}

static int forward_request( int fd, const char* addr, http_request* req )
{
    int fwd, pfd[2], ret = 0;
    size_t i, size, pipedata;
    const char* ptr;
    char temp[512];
    struct tm stm;
    time_t t;

    if( pipe(pfd) != 0 )
    {
        CRITICAL("pipe: %s", strerror(errno));
        return 0;
    }

    fwd = connect_to( addr, 0, AF_UNIX );

    if( fwd < 0 )
        goto out_pipe;

    /* generate request */
    dprintf( fwd, "%s/", http_method_to_string(req->method) );
    write_url_encoded( fwd, (const unsigned char*)req->path, 1 );

    if( req->getargs && req->numargs )
        write_encoded_list( fwd, "?", req->getargs, req->numargs, '&' );

    dprintf( fwd, " HTTP/1.0\r\n" );

    if( req->host   ) dprintf( fwd, "Host: %s\r\n",            req->host   );
    if( req->type   ) dprintf( fwd, "Content-Type: %s\r\n",    req->type   );
    if( req->length ) dprintf( fwd, "Content-Length: %lu\r\n", req->length );

    if( req->ifmod )
    {
        t = req->ifmod;
        localtime_r( &t, &stm );
        strftime( temp, sizeof(temp), "%a, %d %b %Y %H:%M:%S %Z", &stm );
        dprintf( fwd, "If-Modified-Since: %s\r\n", temp );
    }

    if( req->accept & (ENC_DEFLATE|ENC_GZIP) )
    {
        i = req->accept;
        temp[0] = 0;

        if( i & ENC_DEFLATE ) { strcat(temp, "deflate"); i &= ~ENC_DEFLATE; }
        if( i               )   strcat( temp, ", " );
        if( i & ENC_GZIP    ) { strcat(temp, "gzip");    i &= ~ENC_GZIP;    }

        dprintf( fwd, "Accept-Encoding: %s\r\n", temp );
    }

    switch( req->encoding )
    {
    case ENC_DEFLATE: ptr = "deflate"; break;
    case ENC_GZIP:    ptr = "gzip";    break;
    default:          ptr = NULL;      break;
    }

    if( ptr )
        dprintf( fwd, "Content-Encoding: %s\r\n", ptr );

    if( req->cookies && req->numcookies )
    {
        write_encoded_list( fwd, "Cookie: ", req->cookies,
                            req->numcookies, ';' );
    }

    dprintf( fwd, "Connection: Close\r\n\r\n" );

    /* forward request content */
    splice_to_sock( pfd, fd, fwd, req->length, 0 );

    /* load HTTP response into pipe and substitute some header fields */
    pipedata = 0;
    size = 0;

    do
    {
        if( !read_line( fwd, temp, sizeof(temp) ) )
            goto out;

        if( !strncmp( temp, "Connection:", 11 ) )
        {
            sprintf( temp, "Connection: %s",
                     req->flags & REQ_CLOSE ? "Close" : "keep-alive" );
        }

        if( !strncmp( temp, "Content-Length:", 15 ) )
            size = strtol( temp+15, NULL, 10 );

        pipedata += dprintf( pfd[1], "%s\r\n", temp );
    }
    while( strlen(temp) );

    /* make sure remote fails if it tries to force keep-alive */
    shutdown( fwd, SHUT_WR );

    /* forward response content */
    splice_to_sock( pfd, fwd, fd, size, pipedata );
    ret = 1;
out:
    close( fwd );
out_pipe:
    close( pfd[0] );
    close( pfd[1] );
    return ret;
}

int proxy_handle_request( int fd, const cfg_host* h, http_request* req )
{
    size_t len = strlen(h->proxydir);
    const char* ptr;

    if( strncmp(req->path, h->proxydir, len) )
        return ERR_NOT_FOUND;
    if( req->path[len] && req->path[len]!='/' )
        return ERR_NOT_FOUND;

    for( ptr=req->path+len; *ptr=='/'; ++ptr ) { }

    INFO("Forwarding /%s to %s:/%s", req->path, h->proxysock, ptr);

    req->path = ptr;
    if( !forward_request( fd, h->proxysock, req ) )
        return ERR_INTERNAL;

    return 0;
}
#endif /* HAVE_PROXY */

