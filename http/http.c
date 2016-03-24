#include "http.h"
#include "str.h"

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <time.h>

static const char* header_fmt = "HTTP/1.1 %s\r\n"
                                "Server: HTTP toaster\r\n"
                                "X-Powered-By: Electricity\r\n"
                                "Accept-Encoding: gzip, deflate\r\n"
                                "Connection: keep-alive\r\n";

static const char* http_date_fmt = "%a, %d %b %Y %H:%M:%S %Z";

static const char* const status_msgs[] =
{
    "200 Ok",
    "400 Bad Request",
    "404 Not Found",
    "405 Not Allowed",
    "403 Forbidden",
    "406 Not Acceptable",
    "413 Payload Too Large",
    "500 Internal Server Error",
    "408 Request Time-out",
    "307 Temporary Redirect",
    "303 See Other",
    "304 Not Modified",
};

static const struct { const char* field; int length; } hdrfields[] =
{
    { "Host: ",               6 },
    { "Content-Length: ",    16 },
    { "Content-Type: ",      14 },
    { "Cookie: ",             8 },
    { "If-Modified-Since: ", 19 },
    { "Accept-Encoding: ",   17 },
    { "Content-Encoding: ",  18 },
};

static const struct { const char* str; int length; } methods[] =
{
    { "GET ",    4 },
    { "HEAD ",   5 },
    { "POST ",   5 },
    { "PUT ",    4 },
    { "DELETE ", 7 },
};

static int hextoi( int c )
{
    return isdigit(c) ? (c-'0') : (isupper(c) ? (c-'A'+10) : (c-'a'+10));
}

static int check_path( const char* path )
{
    unsigned int len;
    char* ptr;

    while( *path && (ptr = strchrnul(path, '/')) )
    {
        len = ptr - path;
        if( path[0]=='.' && (len==1 || (len==2 && path[1]=='.')) )
            return 0;
        path = *ptr ? ptr+1 : ptr;
    }

    return 1;
}

static char* store_string( http_request* rq, const char* string,
                           size_t length )
{
    char* ptr;

    if( (rq->used + length + 1) > rq->size )
        return NULL;

    ptr = rq->buffer + rq->used;
    rq->used += length + 1;

    memcpy( ptr, string, length );
    ptr[length] = 0;

    return ptr;
}

/****************************************************************************/

const char* http_method_to_string( unsigned int method )
{
    if( method >= sizeof(methods)/sizeof(methods[0]) )
        return NULL;

    return methods[method].str;
}

size_t http_response_header( int fd, const http_file_info* info,
                             const char* setcookies, int statuscode )
{
    const char* status;
    const char* cache;
    char temp[ 256 ];
    size_t len = 0;
    struct tm stm;
    string str;
    time_t t;

    if( statuscode < 0 )
        statuscode = 0;
    if( statuscode > (int)(sizeof(status_msgs)/sizeof(status_msgs[0])) )
        statuscode = 0;

    status = status_msgs[ statuscode ];

    if( !string_init( &str ) )
        return 0;

    len = sprintf( temp, header_fmt, status );
    if( !string_append_len( &str, temp, len ) )
        goto fail;

    if( info->redirect )
    {
        if( !string_append( &str, "Location: "   ) ) goto fail;
        if( !string_append( &str, info->redirect ) ) goto fail;
        if( !string_append( &str, "\r\n"         ) ) goto fail;
    }

    if( info->type )
    {
        len = sprintf(temp, "Content-Type: %s\r\n", info->type);
        if( !string_append_len( &str, temp, len ) )
            goto fail;
    }

    if( info->size )
    {
        len = sprintf(temp, "Content-Length: %lu\r\n",
                      (unsigned long)info->size);
        if( !string_append_len( &str, temp, len ) )
            goto fail;
    }

    if( info->encoding )
    {
        len = sprintf(temp, "Content-Encoding: %s\r\n", info->encoding);
        if( !string_append_len( &str, temp, len ) )
            goto fail;
    }

    if( setcookies )
    {
        if( !string_append( &str, "Set-Cookie: " ) ) goto fail;
        if( !string_append( &str, setcookies     ) ) goto fail;
        if( !string_append( &str, "\r\n"         ) ) goto fail;
    }

    t = info->last_mod;
    localtime_r( &t, &stm );
    len = strftime( temp, sizeof(temp), http_date_fmt, &stm );

    if( !string_append( &str, "Last-Modified: " ) ) goto fail;
    if( !string_append_len( &str, temp, len     ) ) goto fail;
    if( !string_append( &str, "\r\n"            ) ) goto fail;

    if( info->flags & FLAG_STATIC )
        cache = "Cache-Control: max-age=3600\r\n";
    else if( info->flags & FLAG_DYNAMIC )
        cache = "Cache-Control: no-store, must-revalidate\r\n";
    else
        cache = NULL;

    if( cache && !string_append( &str, cache ) )
        goto fail;
    if( !string_append( &str, "\r\n" ) )
        goto fail;

    len = str.used;
    write( fd, str.data, str.used );
    string_cleanup( &str );
    return len;
fail:
    string_cleanup( &str );
    return 0;
}

int http_request_init( http_request* rq, const char* request,
                       char* stringbuffer, size_t size )
{
    char c, *in, *out;
    size_t i;

    memset( rq, 0, sizeof(*rq) );
    rq->method = -1;
    rq->buffer = stringbuffer;
    rq->size = size;

    for( i=0; i<sizeof(methods)/sizeof(methods[0]); ++i )
    {
        if( !strncmp(request, methods[i].str, methods[i].length) )
        {
            rq->method = i;
            request += methods[i].length;
            break;
        }
    }

    if( rq->method < 0 )
        return 0;

    while( *request=='/' )
        ++request;

    for( i=0; request[i] && !isspace(request[i]); ++i ) { }

    if( !i )
        return 1;

    out = in = store_string( rq, request, i );
    if( !in )
        return 0;

    rq->path = in;
    while( !isspace(*in) && *in )
    {
        c = *(in++);

        if( c=='%' && isxdigit(in[0]) && isxdigit(in[1]) )
        {
            c = (hextoi(in[0])<<4) | hextoi(in[1]);
            in += 2;
        }
        else if( (c=='?' && !rq->numargs) || (c=='&' && rq->numargs) )
        {
            if( c=='?' )
                rq->getargs = out + 1;
            c = '\0';
            ++rq->numargs;
        }
        *(out++) = c;
    }

    *(out++) = '\0';
    return check_path( rq->path );
}

int http_parse_attribute( http_request* rq, char* line )
{
    struct tm stm;
    char *ptr;
    size_t i;

    for( i=0; i<sizeof(hdrfields)/sizeof(hdrfields[0]); ++i )
    {
        if( !strncmp( line, hdrfields[i].field, hdrfields[i].length ) )
        {
            line += hdrfields[i].length;
            while( isspace(*line) )
                ++line;
            break;
        }
    }

    switch( i )
    {
    case FIELD_HOST:
        ptr = strchrnul( line, ':' );
        rq->host = store_string( rq, line, ptr-line );
        if( !rq->host )
            return 0;
        break;
    case FIELD_LENGTH:
        rq->length = strtol( line, NULL, 10 );
        break;
    case FIELD_TYPE:
        rq->type = store_string( rq, line, strlen(line) );
        if( !rq->type )
            return 0;
        break;
    case FIELD_COOKIE:
        ptr = store_string( rq, line, strlen(line) );
        if( !ptr )
            return 0;
        rq->numcookies = 1;
        rq->cookies = ptr;
        while( ptr )
        {
            ptr = strchr(ptr, ';');
            if( ptr )
            {
                *(ptr++) = '\0';
                ++rq->numcookies;
            }
        }
        break;
    case FIELD_IFMOD:
        memset( &stm, 0, sizeof(stm) );
        strptime(line, http_date_fmt, &stm);
        rq->ifmod = mktime(&stm);
        break;
    case FIELD_ACCEPT:
        while( (ptr = strsep(&line, ", ")) )
        {
            if( !strcmp( ptr, "gzip" ) )
                rq->accept |= ENC_GZIP;
            else if( !strcmp( ptr, "deflate" ) )
                rq->accept |= ENC_DEFLATE;
        }
        break;
    case FIELD_ENCODING:
        if( !strcmp( line, "gzip"    ) ) { rq->encoding=ENC_GZIP;    break; }
        if( !strcmp( line, "deflate" ) ) { rq->encoding=ENC_DEFLATE; break; }
        return 0;
    }
    return 1;
}

const char* http_get_arg( const char* argstr, int args, const char* arg )
{
    const char* ptr = argstr;
    int i, len = strlen(arg);

    if( !ptr )
        return NULL;

    for( i=0; i<args; ++i )
    {
        if( !strncmp( ptr, arg, len ) && ptr[len]=='=' )
            return ptr + len + 1;

        ptr += strlen(ptr) + 1;
    }

    return NULL;
}

int http_split_args( char* argstr )
{
    int count;

    for( count=1; *argstr; ++argstr )
    {
        if( *argstr=='+' )
        {
            *argstr = ' ';
        }
        else if( *argstr=='%' && isxdigit(argstr[1]) && isxdigit(argstr[2]) )
        {
            *argstr = (hextoi(argstr[1])<<4) | hextoi(argstr[2]);
            memmove( argstr+1, argstr+3, strlen(argstr+3)+1 );
        }
        else if( *argstr=='&' )
        {
            *argstr = '\0';
            ++count;
        }
    }

    return count;
}

