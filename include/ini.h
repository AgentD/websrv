#ifndef INI_H
#define INI_H

#include <stddef.h>

/*
    Parse/compile an *.ini file in memory and preprocess it for easy data
    retrieval.

    Returns non-zeron on success, zero on failure.
 */
int ini_compile( char* buffer, size_t size );

/*
    Finds next section and returns its name,
    or NULL if there are no more sections
 */
char* ini_next_section( void );

/*
    Finds the next key-value pair in the current section.
    Returns non-zero on success, zero if there are no more
    key-value pairs in the current section.
 */
int ini_next_key( char** key, char** value );

#endif /* INI_H */

