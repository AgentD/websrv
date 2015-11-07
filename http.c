#include "http.h"

#include <unistd.h>
#include <string.h>
#include <alloca.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

static const char* err403 = "403 Forbidden";
static const char* err404 = "404 Not Found";
static const char* err405 = "405 Not Allowed";
static const char* err500 = "500 Internal Server Error";

static const char* err_page_fmt = "<html><head><title>%s</title></head>"
                                  "<body><h1>%s</h1>";

static const char* header_fmt = "HTTP/1.1 %s\r\n"
                                "Server: HTTP toaster\r\n"
                                "X-Powered-By: Electricity\r\n"
                                "Content-Type: %s\r\n"
                                "Content-Length: %lu\r\n"
                                "Connection: keep-alive\r\n\r\n";

static size_t gen_header( int fd, const char* retmsg,
                          const char* type, unsigned long size )
{
    char* buffer;
    size_t count;

    buffer = alloca(strlen(header_fmt)+strlen(retmsg)+strlen(type)+30);
    sprintf( buffer, header_fmt, retmsg, type, size );

    count = strlen(buffer);
    write( fd, buffer, count );
    return count;
}

static size_t gen_error_page( int fd, const char* error )
{
    size_t count, hdrsize;
    char* buffer;

    buffer = alloca( strlen(err_page_fmt) + strlen(error)*2 + 1 );
    sprintf( buffer, err_page_fmt, error, error );
    count = strlen(buffer);

    hdrsize = gen_header( fd, error, "text/html", count );

    if( !hdrsize )
        return 0;

    write( fd, buffer, count );
    return hdrsize + count;
}

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

/****************************************************************************/

size_t http_not_found( int fd )
{
    return gen_error_page( fd, err404 );
}

size_t http_not_allowed( int fd )
{
    return gen_error_page( fd, err405 );
}

size_t http_forbidden( int fd )
{
    return gen_error_page( fd, err403 );
}

size_t http_internal_error( int fd )
{
    return gen_error_page( fd, err500 );
}

size_t http_ok( int fd, const char* type, unsigned long size )
{
    return gen_header( fd, "200 Ok", type, size );
}

int http_request_parse( char* buffer, http_request* rq )
{
    char* out = buffer;
    size_t j;

    memset( rq, 0, sizeof(*rq) );
    rq->method = -1;

    /* parse method */
         if(!strncmp(buffer,"GET",   3)) {rq->method=HTTP_GET;    buffer+=3;}
    else if(!strncmp(buffer,"HEAD",  4)) {rq->method=HTTP_HEAD;   buffer+=4;}
    else if(!strncmp(buffer,"POST",  4)) {rq->method=HTTP_POST;   buffer+=4;}
    else if(!strncmp(buffer,"PUT",   3)) {rq->method=HTTP_PUT;    buffer+=3;}
    else if(!strncmp(buffer,"DELETE",6)) {rq->method=HTTP_DELETE; buffer+=6;}

    if( rq->method<0 || !isspace(*buffer) )
        return 0;

    while( isspace(*buffer) ) { ++buffer; }

    /* isolate path */
    while( *buffer=='/' || *buffer=='\\' ) { ++buffer; }
    rq->path = out;

    for( j=0; !isspace(*buffer) && *buffer; ++j )
    {
        if( *buffer=='%' && isxdigit(buffer[1]) && isxdigit(buffer[2]) )
        {
            *(out++) = (hextoi(buffer[1])<<4) | hextoi(buffer[2]);
            buffer += 3;
        }
        else if( *buffer=='/' || *buffer=='\\' )
        {
            while( *buffer=='/' || *buffer=='\\' )
                ++buffer;
            if( isspace(*buffer) )
                break;
            *(out++) = '/';
        }
        else
        {
            *(out++) = *(buffer++);
        }
    }

    ++buffer;
    *(out++) = '\0';
    fix_path( rq->path, j );

    while( isspace(*buffer) && *buffer!='\n' && *buffer!='\r' ) { ++buffer; }
    if( !strncmp(buffer,"http/1.1",8) || !strncmp(buffer,"HTTP/1.1",8) )
        rq->flags |= FLAG_KEEPALIVE;

    /* parse fields */
    while( *buffer )
    {
        /* skip current line */
        while( *buffer && *buffer!='\n' && *buffer!='\r' ) { ++buffer; }
        while( isspace(*buffer) ) { ++buffer; }

        if( !strncmp( buffer, "Host:", 5 ) && isspace(buffer[5]) )
        {
            for( buffer+=5; *buffer==' ' || *buffer=='\t'; ++buffer ) { }
            rq->host = buffer;
            for( j=0; isalpha(rq->host[j]); ++j ) { }
            rq->host[j] = '\0';
            buffer += j + 1;
        }
        else if( !strncmp( buffer, "Content-Length:", 15 ) )
        {
            for( buffer+=15; *buffer==' ' || *buffer=='\t'; ++buffer ) { }
            rq->length = strtol( buffer, NULL, 10 );
        }
        else if( !strncmp( buffer, "Content-Type:", 13 ) )
        {
            for( buffer+=13; *buffer==' ' || *buffer=='\t'; ++buffer ) { }
            rq->type = buffer;
        }
        else if( !strncmp( buffer, "Connection:", 11 ) )
        {
            for( buffer+=11; *buffer==' ' || *buffer=='\t'; ++buffer ) { }
            if( !strncmp(buffer, "keep-alive", 10) )
                rq->flags |= FLAG_KEEPALIVE;
            else
                rq->flags &= ~FLAG_KEEPALIVE;
        }
    }
    return 1;
}

