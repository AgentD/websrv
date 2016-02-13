#include <stdlib.h>
#include <string.h>
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

