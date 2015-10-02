#include "http.h"

#include <unistd.h>
#include <string.h>
#include <alloca.h>
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
                                "Connection: close\r\n\r\n";

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

