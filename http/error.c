#include "error.h"
#include "http.h"
#include "str.h"

#include <unistd.h>
#include <time.h>

static const char* const text[] =
{
    "200 Ok",
    "Error 400. Your request contains an error.",
    "Error 404. File not found.",
    "Error 405. Not Allowed",
    "Error 403. Forbidden",
    "Error 406. The encoding of your request cannot be processed",
    "Error 413. The data sent to the server with your request is too large",
    "Error 500. Internal server error",
    "Error 408. A timeout occoured while handling your request",
    "Redirecting....",
    "Temporarily moved. Redirecting...."
};

size_t gen_error_page( int fd, int errorid, int accept, const char* redirect )
{
    const char* error;
    http_file_info info;
    size_t length = 0;
    string str;

    if( (errorid < 0) || (errorid > (int)(sizeof(text)/sizeof(text[0]))) )
        errorid = ERR_INTERNAL;

    error = text[ errorid ];

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
    if( !string_append( &str, "</h1>" ) )

    if( redirect )
    {
        string_append( &str, "Your browser is being redirected to a different"
                             "location.<br><br>"
                             "If it does not work, click <a href=\"" );
        string_append( &str, redirect );
        string_append( &str, "\">here</a>" );
    }

    if( !string_append( &str, "</body></html>" ) )
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

    info.status = errorid;
    info.type = "text/html";
    info.last_mod = time(0);
    info.size = str.used;
    info.redirect = redirect;

    length = http_response_header( fd, &info );
    write( fd, str.data, str.used );
    length += str.used;
fail:
    string_cleanup( &str );
    return length;
}

