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

static int write_url_encoded( string* out, const unsigned char* str,
                              int ispath )
{
    char hexbuf[16];
    int i, ret = 1;

    for( i=0; str[i]!=0; ++i )
    {
        if( str[i]=='/' && ispath )
            continue;

        if( (str[i] & 0x80) || strchr( mustencode, str[i] ) )
        {
            if( i )
                ret = ret && string_append_len( out, (const char*)str, i );

            sprintf( hexbuf, "%%%02X", (int)str[i] );
            ret = ret && string_append_len( out, hexbuf, 4 );

            str = str + i + 1;
            i = -1;
        }
    }

    if( i )
        ret = ret && string_append_len( out, (const char*)str, i );
    return ret;
}

static int write_encoded_list( string* out, const char* field,
                               const char* list, int count, char sep )
{
    const char* ptr;
    int i, ret = 1;

    ret = ret && string_append( out, field );
    ptr = list;

    for( i=0; i<count; ++i )
    {
        ret = ret && write_url_encoded( out, (const unsigned char*)ptr, 0 );
        ptr += strlen(ptr);
        ret = ret && string_append_len( out, "=", 1 );
        ret = ret && write_url_encoded( out, (const unsigned char*)ptr, 0 );
        ptr += strlen(ptr);

        if( (i+1) < count )
            ret = ret && string_append_len( out, &sep, 1 );
    }
    return ret;
}

static int append_field( string* out, const char* field, const char* value )
{
    int ret = 1;
    ret = ret && string_append( out, field );
    ret = ret && string_append( out, value );
    ret = ret && string_append( out, "\r\n" );
    return ret;
}

static int request_to_string( string* str, const http_request* req,
                              const char* path )
{
    const char* ptr;
    int i, ret = 1;
    char temp[64];
    struct tm stm;
    time_t t;

    ret = ret && string_append( str, http_method_to_string(req->method) );
    ret = ret && string_append_len( str, "/", 1 );
    ret = ret && write_url_encoded( str, (const unsigned char*)path, 1 );

    if( req->getargs && req->numargs )
    {
        ret = ret && write_encoded_list( str, "?", req->getargs,
                                         req->numargs, '&' );
    }

    ret = ret && string_append( str, " HTTP/1.0\r\n" );

    if( req->host   ) ret = ret&&append_field(str,"Host: ",req->host);
    if( req->type   ) ret = ret&&append_field(str,"Content-Type: ",req->type);
    if( req->length )
    {
        sprintf( temp, "%lu", req->length );
        ret = ret && append_field( str, "Content-Length: ", temp );
    }

    if( req->ifmod )
    {
        t = req->ifmod;
        localtime_r( &t, &stm );
        strftime( temp, sizeof(temp), "%a, %d %b %Y %H:%M:%S %Z", &stm );
        ret = ret && append_field( str, "If-Modified-Since: ", temp );
    }

    if( req->accept & (ENC_DEFLATE|ENC_GZIP) )
    {
        i = req->accept & (ENC_DEFLATE|ENC_GZIP);
        temp[0] = 0;

        if( i & ENC_DEFLATE )
        {
            strcat( temp, "deflate" );
            i &= ~ENC_DEFLATE;
            if( i )
                strcat( temp, ", " );
        }

        if( i & ENC_GZIP )
            strcat( temp, "gzip" );

        ret = ret && append_field( str, "Accept-Encoding: ", temp );
    }

    switch( req->encoding )
    {
    case ENC_DEFLATE: ptr = "deflate"; break;
    case ENC_GZIP:    ptr = "gzip";    break;
    default:          ptr = NULL;      break;
    }

    if( ptr )
        ret = ret && append_field( str, "Content-Encoding: ", ptr );

    if( req->cookies && req->numcookies )
    {
        ret = ret && write_encoded_list( str, "Cookie: ", req->cookies,
                                         req->numcookies, ';' );
    }

    ret = ret && string_append( str, "Connection: close\r\n\r\n" );
    return ret;
}

static int sock_flush_to_pipe( int pfd, sock_t* sock,
                               size_t* size, size_t* pipedata )
{
    size_t diff;

    if( *size && (sock->offset < sock->size) )
    {
        diff = sock->size - sock->offset;
        if( diff > *size )
            diff = *size;

        if( write( pfd, sock->buffer + sock->offset, diff ) < (ssize_t)diff )
            return 0;

        sock->offset = 0;
        sock->size = 0;
        *pipedata += diff;
        *size -= diff;
    }
    return 1;
}

static int forward_request( sock_t* sock, const char* addr,
                            const http_request* req, const char* path )
{
    int fwd, pfd[2], ret = 0;
    size_t size, pipedata;
    sock_t* fwdwrap;
    char temp[512];
    string str;

    /* generate request */
    if( !string_init( &str ) )
        return 0;
    if( !request_to_string( &str, req, path ) )
        goto out_str;

    /* forward request */
    if( pipe(pfd) != 0 )
    {
        CRITICAL("pipe: %s", strerror(errno));
        goto out_str;
    }

    if( write( pfd[1], str.data, str.used ) < (ssize_t)str.used )
        goto out_pipe;

    pipedata = str.used;
    size = req->length;

    if( !sock_flush_to_pipe( pfd[1], sock, &size, &pipedata ) )
        goto out_pipe;

    fwd = connect_to( addr, 0, AF_UNIX );
    if( fwd < 0 )
        goto out_pipe;
    fwdwrap = create_wrapper( fwd );
    if( !fwdwrap )
    {
        close( fwd );
        goto out_pipe;
    }

    splice_to_sock( pfd, sock->fd, fwd, size, pipedata );
    pipedata = size = 0;

    /* load HTTP response into pipe and substitute some header fields */
    do
    {
        ret = read_line( fwdwrap, temp, sizeof(temp), 0 );
        if( ret <= 0 )
        {
            ret = 0;
            goto out;
        }

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

    /* forward response content */
    ret = 0;
    if( !sock_flush_to_pipe( pfd[1], fwdwrap, &size, &pipedata ) )
        goto out;

    splice_to_sock( pfd, fwd, sock->fd, size, pipedata );
    ret = 1;
out:
    destroy_wrapper( fwdwrap );
out_pipe:
    close( pfd[0] );
    close( pfd[1] );
out_str:
    string_cleanup( &str );
    return ret;
}

int proxy_handle_request( sock_t* sock, const cfg_host* h,
                          const http_request* req )
{
    size_t len = strlen(h->proxydir);
    const char* ptr;

    if( strncmp(req->path, h->proxydir, len) )
        return ERR_NOT_FOUND;
    if( req->path[len] && req->path[len]!='/' )
        return ERR_NOT_FOUND;

    for( ptr=req->path+len; *ptr=='/'; ++ptr ) { }

    INFO("Forwarding /%s to %s:/%s", req->path, h->proxysock, ptr);

    return forward_request( sock, h->proxysock, req, ptr ) ? 0 : ERR_INTERNAL;
}
#endif /* HAVE_PROXY */

