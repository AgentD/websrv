#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

#define HTTP_GET 1
#define HTTP_HEAD 2
#define HTTP_POST 3
#define HTTP_PUT 4
#define HTTP_DELETE 5

#define FIELD_HOST 1
#define FIELD_LENGTH 2
#define FIELD_TYPE 3

#define ERR_BAD_REQ 0
#define ERR_NOT_FOUND 1
#define ERR_METHOD 2
#define ERR_FORBIDDEN 3
#define ERR_INTERNAL 4

typedef struct
{
    int method;     /* request method */
    char* path;     /* requested path */
    char* host;     /* hostname field */
    char* type;     /* content-type */
    size_t length;  /* content-length */
    int flags;
}
http_request;

/* Write an error page (and header). Returns number of bytes written. */
size_t gen_error_page( int fd, int error );

/*
    Write 200 Ok header with content length and content type.
    Returns the number of bytes written, 0 on failure.
 */
size_t http_ok( int fd, const char* type, unsigned long size );

/* parse a HTTP request, returns non-zero on success, zero on failure */
int http_request_parse( char* buffer, http_request* request );

#endif /* HTTP_H */

