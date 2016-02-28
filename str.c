#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <zlib.h>

#include "str.h"

int string_init( string* str )
{
    str->avail = 512;
    str->used = 0;
    str->data = malloc( str->avail );

    if( str->data == NULL )
        return 0;

    str->data[0] = 0;
    return 1;
}

int string_append_len( string* str, const char* cstr, size_t len )
{
    size_t newsize;
    char* new;

    if( (str->used + len + 1) > str->avail )
    {
        newsize = str->used + len + 512;

        new = realloc( str->data, newsize );

        if( !new )
            return 0;

        str->data = new;
        str->avail = newsize;
    }

    memcpy( str->data + str->used, cstr, len );
    str->used += len;

    str->data[ str->used ] = 0;
    return 1;
}

int string_compress( string* str, int gziphdr )
{
    unsigned char* buffer;
    z_stream strm;
    size_t bound;
    int ret;

    memset( &strm, 0, sizeof(strm) );

    if( gziphdr )
    {
        ret = deflateInit2( &strm, Z_BEST_COMPRESSION, Z_DEFLATED,
                            MAX_WBITS + 16, MAX_MEM_LEVEL,
                            Z_DEFAULT_STRATEGY );
    }
    else
    {
        ret = deflateInit(&strm, Z_BEST_COMPRESSION);
    }

    if( ret != Z_OK )
        return 0;

    bound = deflateBound( &strm, str->used );

    if( !(buffer = malloc( bound )) )
    {
        deflateEnd( &strm );
        return 0;
    }

    strm.avail_in = str->used;
    strm.avail_out = bound;
    strm.next_in = (unsigned char*)str->data;
    strm.next_out = buffer;

    if( deflate(&strm, Z_FINISH) != Z_STREAM_END )
    {
        deflateEnd( &strm );
        free( buffer );
        return 0;
    }

    free( str->data );
    str->avail = bound;
    str->used = strm.next_out - buffer;
    str->data = (char*)buffer;
    deflateEnd( &strm );
    return 1;
}

int string_extract( string* str, int isgzip )
{
    char out[1024];
    z_stream strm;
    string temp;
    int ret;

    if( !string_init( &temp ) )
        return 0;

    memset( &strm, 0, sizeof(strm) );

    ret = isgzip ? inflateInit2(&strm, MAX_WBITS + 16) : inflateInit(&strm);

    if( ret!=Z_OK )
    {
        string_cleanup( &temp );
        return 0;
    }

    strm.avail_in = str->used;
    strm.next_in = (unsigned char*)str->data;

    while( strm.avail_in )
    {
        do
        {
            strm.avail_out = sizeof(out);
            strm.next_out = (unsigned char*)out;

            ret = inflate( &strm, Z_NO_FLUSH );

            if( ret!=Z_OK && ret!=Z_STREAM_END )
                goto fail;

            if( !string_append_len( &temp, out, sizeof(out)-strm.avail_out ) )
                goto fail;
        }
        while( strm.avail_out == 0 );
    }

    /* cleanup */
    inflateEnd( &strm );
    free( str->data );
    *str = temp;
    return 1;
fail:
    string_cleanup( &temp );
    inflateEnd( &strm );
    return 0;
}

int string_process_template( string* str, int fd, const template_map* map,
                             unsigned int map_size )
{
    char buffer[ 1025 ], *ptr;
    unsigned int k, len;
    ssize_t i, diff;

    while( (diff = read(fd, buffer, sizeof(buffer)-1)) > 0 )
    {
        buffer[ diff ] = 0;

        ptr = strchr( buffer, '$' );

        if( ptr )
        {
            i = ptr - buffer;
            lseek( fd, i - diff, SEEK_CUR );

            if( i && !string_append_len( str, buffer, i ) )
                return -1;

            for( k=0; k<map_size; ++k )
            {
                len = strlen( map[k].name );
                if( !strncmp(ptr, map[k].name, len) && !isalnum(ptr[len]) )
                {
                    lseek( fd, len, SEEK_CUR );
                    return map[k].id;
                }
            }
        }
        else if( !string_append_len( str, buffer, diff ) )
        {
            return -1;
        }
    }
    return 0;
}

