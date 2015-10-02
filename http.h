#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

#define HTTP_GET 0
#define HTTP_HEAD 1
#define HTTP_POST 2
#define HTTP_PUT 3
#define HTTP_DELETE 4

/* Write 404 page+header. Returns number of bytes written, 0 on failure. */
size_t http_not_found( int fd );

/* Write 405 page+header. Returns number of bytes written, 0 on failure. */
size_t http_not_allowed( int fd );

/* Write 403 page+header. Returns number of bytes written, 0 on failure. */
size_t http_forbidden( int fd );

/* Write 500 page+header. Returns number of bytes written, 0 on failure. */
size_t http_internal_error( int fd );

/*
    Write 200 Ok header with content length and content type.
    Returns the number of bytes written, 0 on failure.
 */
size_t http_ok( int fd, const char* type, unsigned long size );

#endif /* HTTP_H */

