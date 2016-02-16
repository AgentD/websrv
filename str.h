#ifndef DYN_STRING_H
#define DYN_STRING_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    size_t used;    /* number of chars used (excluding null-terminator) */
    size_t avail;   /* number of chars available in the array */
    char* data;     /* dynamically resized, null-terminated string */
}
string;

int string_init( string* str );

#define string_cleanup( str ) free((str)->data)

int string_append_len( string* str, const char* cstr, size_t len );

#define string_append( str, cstr ) string_append_len(str,cstr,strlen(cstr))

/*
    Deflate compress a string. Optionally wrap the compressed data in gzip
    format.

    Returns: non-zero on success, zero on failure.
 */
int string_compress( string* str, int gziphdr );

/*
    Uncompress a deflate compressed string (optionally wrapped in gzip
    format).

    Returns: non-zero on success, zero on failure.
 */
int string_extract( string* str, int isgzip );

#endif /* DYN_STRING_H */

