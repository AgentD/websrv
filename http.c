#include "http.h"

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

static const char* const error_msgs[] =
{
    "400 Bad Request",
    "404 Not Found",
    "405 Not Allowed",
    "403 Forbidden",
    "406 Not Acceptable",
    "413 Payload Too Large",
    "500 Internal Server Error",
};

static const char* err_page_fmt = "<!DOCTYPE html>"
                                  "<html><head><title>%s</title></head>"
                                  "<body><h1>%s</h1></body></html>";

static const char* header_fmt = "HTTP/1.1 %s\r\n"
                                "Server: HTTP toaster\r\n"
                                "X-Powered-By: Electricity\r\n"
                                "Content-Type: %s\r\n"
                                "Content-Length: %lu\r\n"
                                "Connection: keep-alive\r\n%s";

static const struct { const char* field; int length; } hdrfields[] =
{
    { "Host: ",            6 },
    { "Content-Length: ", 16 },
    { "Content-Type: ",   14 },
    { "Cookie: ",          8 },
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

static int check_path( char* path )
{
    int level=0;
    char* ptr;

    while( path )
    {
        if( path[0]=='.' && path[1]=='.' && (path[2]=='/' || !path[2]) )
        {
            if( (--level) < 0 )
                return 0;
        }
        else if( path[0]!='.' || (path[1]!='/' && path[1]) )
        {
            ++level;
        }

        ptr = strchr( path, '/' );
        path = ptr ? (ptr + 1) : NULL;
    }

    return 1;
}

/****************************************************************************/

size_t gen_error_page( int fd, int errorid )
{
    const char* error = error_msgs[ errorid ];
    size_t count = strlen(err_page_fmt) - 4 + strlen(error)*2;

    count = dprintf(fd, header_fmt, error, "text/html",
                    (unsigned long)count, "\r\n");
    count += dprintf( fd, err_page_fmt, error, error );
    return count;
}

size_t http_ok( int fd, const char* type, unsigned long size,
                const char* setcookies )
{
    size_t count;

    if( setcookies )
    {
        count = dprintf(fd, header_fmt, "200 Ok", type, size, "Set-Cookie: ");
        count+= dprintf(fd, "%s\r\n\r\n", setcookies);
        return count;
    }
    else
    {
        return dprintf( fd, header_fmt, "200 Ok", type, size, "\r\n" );
    }
}

int http_request_parse( char* buffer, http_request* rq )
{
    char* out = buffer;
    size_t j;

    memset( rq, 0, sizeof(*rq) );

    /* parse method */
    for( rq->method=-1, j=0; j<sizeof(methods)/sizeof(methods[0]); ++j )
    {
        if( !strncmp(buffer, methods[j].str, methods[j].length) )
        {
            rq->method = j;
            buffer += methods[j].length;
            break;
        }
    }

    if( rq->method < 0 )
        return 0;

    /* isolate path */
    while( *buffer=='/' || *buffer=='\\' ) { ++buffer; }
    rq->path = out;

    while( !isspace(*buffer) && *buffer )
    {
        if( *buffer=='%' && isxdigit(buffer[1]) && isxdigit(buffer[2]) )
        {
            *(out++) = (hextoi(buffer[1])<<4) | hextoi(buffer[2]);
            buffer += 3;
        }
        else if( *buffer=='?' )
        {
            *(out++) = '\0';
            rq->getargs = out;
            rq->numargs = 1;
            ++buffer;
        }
        else if( *buffer=='&' && rq->numargs )
        {
            *(out++) = '\0';
            ++rq->numargs;
            ++buffer;
        }
        else
        {
            *(out++) = *buffer=='\\' ? '/' : *buffer;
            ++buffer;
        }
    }

    *(out++) = '\0';
    if( !check_path( rq->path ) )
        return 0;

    /* parse fields */
    while( *buffer )
    {
        while( *buffer && *buffer!='\n' ) { ++buffer; }
        ++buffer;

        for( j=0; j<sizeof(hdrfields)/sizeof(hdrfields[0]); ++j )
        {
            if( !strncmp( buffer, hdrfields[j].field, hdrfields[j].length ) )
            {
                buffer += hdrfields[j].length;
                break;
            }
        }

        switch( j )
        {
        case FIELD_HOST:
            rq->host = out;
            while( isgraph(*buffer)&&*buffer!=':' ) { *(out++)=*(buffer++); }
            *(out++) = '\0';
            break;
        case FIELD_LENGTH:
            rq->length = strtol( buffer, NULL, 10 );
            break;
        case FIELD_TYPE:
            rq->type = out;
            while( !isspace(*buffer) && *buffer ) { *(out++)=*(buffer++); }
            *(out++) = '\0';
            break;
        case FIELD_COOKIE:
            rq->cookies = out;
            rq->numcookies = 1;
            while( *buffer && *buffer!='\n' && *buffer!='\r' )
            {
                if( isspace(*buffer) )
                {
                    ++buffer;
                }
                else if( *buffer==';' )
                {
                    *(out++) = '\0';
                    ++rq->numcookies;
                    ++buffer;
                }
                else
                {
                    *(out++) = *(buffer++);
                }
            }
            *(out++) = '\0';
            break;
        }
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

