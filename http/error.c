#include "error.h"
#include "http.h"
#include "str.h"

#include <unistd.h>
#include <time.h>

static const char* const error_msgs[] =
{
    "400 Bad Request",
    "404 Not Found",
    "405 Not Allowed",
    "403 Forbidden",
    "406 Not Acceptable",
    "413 Payload Too Large",
    "500 Internal Server Error",
    "408 Request Time-out",
};

size_t gen_error_page( int fd, int errorid, int accept )
{
    const char* error = error_msgs[ errorid-1 ];
    http_file_info info;
    size_t length = 0;
    string str;

    if( !string_init( &str ) )
        return 0;
    if( !string_append( &str, "<!DOCTYPE html><html><head><title>" ) )
        goto fail;
    if( !string_append( &str, error ) )
        goto fail;
    if( !string_append( &str, "</title></head><body><h1>" ) )
        goto fail;
    if( !string_append( &str, error ) )
        goto fail;
    if( !string_append( &str, "</h1></body></html>" ) )
        goto fail;

    memset( &info, 0, sizeof(info) );

    if( accept & ENC_DEFLATE )
    {
        if( string_compress( &str, 0 ) )
            info.encoding = "deflate";
    }
    else if( accept & ENC_GZIP )
    {
        if( string_compress( &str, 1 ) )
            info.encoding = "gzip";
    }

    info.type = "text/html";
    info.last_mod = time(0);
    info.size = str.used;

    length = http_response_header( fd, &info, NULL, error );
    write( fd, str.data, str.used );
    length += str.used;
fail:
    string_cleanup( &str );
    return length;
}

