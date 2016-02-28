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

typedef struct
{
    const char* name;
    int id;
}
template_map;

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

/*
    Read a template from a file descriptor (fd) and append it to a string.

    If the '$' sign is encoutered in the template, it is not inserted into the
    page. Instead, the template map is used to resolve the string starting at
    the '$' sign to a numeric ID that this function returns.

    If end of file is reached, the function returns 0. On error, -1 is
    returned.
 */
int string_process_template( string* str, int fd, const template_map* map,
                             unsigned int map_size );

#endif /* DYN_STRING_H */

