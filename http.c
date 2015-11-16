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
                                "Connection: keep-alive\r\n\r\n";

static struct { const char* field; int length; int id; } hdrfields[] =
{
    { "Host: ",            6, FIELD_HOST   },
    { "Content-Length: ", 16, FIELD_LENGTH },
    { "Content-Type: ",   14, FIELD_TYPE   },
};

static struct { const char* str; int length; int id; } methods[] =
{
    { "GET ",    4, HTTP_GET    },
    { "HEAD ",   5, HTTP_HEAD   },
    { "POST ",   5, HTTP_POST   },
    { "PUT ",    4, HTTP_PUT    },
    { "DELETE ", 7, HTTP_DELETE },
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

    count = dprintf(fd, header_fmt, error, "text/html", (unsigned long)count);
    count += dprintf( fd, err_page_fmt, error, error );
    return count;
}

size_t http_ok( int fd, const char* type, unsigned long size )
{
    return dprintf( fd, header_fmt, "200 Ok", type, size );
}

int http_request_parse( char* buffer, http_request* rq )
{
    char* out = buffer;
    int field;
    size_t j;

    memset( rq, 0, sizeof(*rq) );

    /* parse method */
    for( j=0; j<sizeof(methods)/sizeof(methods[0]); ++j )
    {
        if( !strncmp(buffer, methods[j].str, methods[j].length) )
        {
            rq->method = methods[j].id;
            buffer += methods[j].length;
            break;
        }
    }

    if( !rq->method )
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

        for( field=0, j=0; j<sizeof(hdrfields)/sizeof(hdrfields[0]); ++j )
        {
            if( !strncmp( buffer, hdrfields[j].field, hdrfields[j].length ) )
            {
                field = hdrfields[j].id;
                buffer += hdrfields[j].length;
                break;
            }
        }

        switch( field )
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
        }
    }
    return 1;
}

