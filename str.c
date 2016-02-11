#include <stdlib.h>
#include <string.h>

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

