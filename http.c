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

static int check_path( char* path )
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

/****************************************************************************/

size_t http_response_header( int fd, const http_file_info* info,
                             const char* setcookies, const char* status )
{
    const char* cache;
    char temp[ 256 ];
    size_t len = 0;
    struct tm stm;
    string str;
    time_t t;

    if( !string_init( &str ) )
        return 0;

    len = sprintf( temp, header_fmt, status );
    if( !string_append_len( &str, temp, len ) )
        goto fail;

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

size_t http_ok( int fd, const http_file_info* info, const char* setcookies )
{
    size_t ret;
    if( info->flags & FLAG_UNCHANGED )
        ret = http_response_header(fd, info, setcookies, "304 Not Modified");
    else
        ret = http_response_header(fd, info, setcookies, "200 Ok");
    return ret;
}

int http_request_parse( char* buffer, http_request* rq )
{
    char c, *out = buffer;
    struct tm stm;
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
        c = *(buffer++);
        if( c=='\\' )
            c = '/';
        while( c=='/' && (*buffer=='/' || *buffer=='\\') )
            ++buffer;
        if( c=='/' && (!(*buffer) || isspace(*buffer)) )
            break;

        if( c=='%' && isxdigit(buffer[0]) && isxdigit(buffer[1]) )
        {
            c = (hextoi(buffer[0])<<4) | hextoi(buffer[1]);
            buffer += 2;
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
    if( !check_path( rq->path ) )
        return 0;

    /* parse fields */
    while( (buffer = strchr( buffer, '\n' )) )
    {
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
                c = *(buffer++);
                if( !isspace(c) )
                    continue;
                if( c==';' )
                {
                    c = '\0';
                    ++rq->numcookies;
                }
                *(out++) = c;
            }
            *(out++) = '\0';
            break;
        case FIELD_IFMOD:
            memset( &stm, 0, sizeof(stm) );
            strptime(buffer, http_date_fmt, &stm);
            rq->ifmod = mktime(&stm);
            break;
        case FIELD_ACCEPT:
            rq->accept = 0;
            while( *buffer && *buffer!='\n' && *buffer!='\r' )
            {
                if( isspace(*buffer) || *buffer==',' )
                {
                    ++buffer;
                    continue;
                }
                if( !strncmp( buffer, "gzip", 4 ) && !isalnum(buffer[4]) )
                {
                    rq->accept |= ENC_GZIP;
                    buffer += 4;
                    continue;
                }
                if( !strncmp( buffer, "deflate", 7 ) && !isalnum(buffer[7]) )
                {
                    rq->accept |= ENC_DEFLATE;
                    buffer += 7;
                    continue;
                }
                while( !isspace(*buffer) )
                    ++buffer;
            }
            break;
        case FIELD_ENCODING:
            if( !strncmp( buffer, "gzip", 4 ) && !isalnum(buffer[4]) )
                rq->encoding = ENC_GZIP;
            else if( !strncmp( buffer, "deflate", 7 ) && !isalnum(buffer[7]) )
                rq->encoding = ENC_DEFLATE;
            else
                rq->encoding = -1;
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

