#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

#define HTTP_GET 0
#define HTTP_HEAD 1
#define HTTP_POST 2
#define HTTP_PUT 3
#define HTTP_DELETE 4

#define FIELD_HOST 0
#define FIELD_LENGTH 1
#define FIELD_TYPE 2
#define FIELD_COOKIE 3

#define ERR_BAD_REQ 0
#define ERR_NOT_FOUND 1
#define ERR_METHOD 2
#define ERR_FORBIDDEN 3
#define ERR_TYPE 4
#define ERR_SIZE 5
#define ERR_INTERNAL 6

typedef struct
{
    int method;     /* request method */
    char* path;     /* requested path */
    char* host;     /* hostname field */
    char* type;     /* content-type */
    char* getargs;  /* arguments pasted to path string */
    int numargs;    /* number of get get-arguments */
    char* cookies;  /* pointer to cookie args */
    int numcookies; /* number of cookies */
    size_t length;  /* content-length */
}
http_request;

/* Write an error page (and header). Returns number of bytes written. */
size_t gen_error_page( int fd, int error );

/*
    Write 200 Ok header with content length and content type.
    Returns the number of bytes written, 0 on failure.

    If set cookies is not NULL, it is pasted into the set-cookie header.
 */
size_t http_ok( int fd, const char* type, unsigned long size,
                const char* setcookies );

/* parse a HTTP request, returns non-zero on success, zero on failure */
int http_request_parse( char* buffer, http_request* request );

const char* http_get_arg( const char* argstr, int args, const char* arg );

int http_split_args( char* argstr );

#endif /* HTTP_H */

