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
#define FIELD_IFMOD 4
#define FIELD_ACCEPT 5
#define FIELD_ENCODING 6

#define ERR_BAD_REQ 1
#define ERR_NOT_FOUND 2
#define ERR_METHOD 3
#define ERR_FORBIDDEN 4
#define ERR_TYPE 5
#define ERR_SIZE 6
#define ERR_INTERNAL 7
#define ERR_TIMEOUT 8

/* if set, allow caching and add last modified heder */
#define FLAG_STATIC 0x01

/* if set, do not allow caching */
#define FLAG_DYNAMIC 0x02

/* if set generate a 304 response instead of 200 */
#define FLAG_UNCHANGED 0x04

#define ENC_DEFLATE 0x01
#define ENC_GZIP 0x02

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
    unsigned long ifmod;    /* Only send the file if newer than this */
    int accept;     /* accepted encoding flags (ENC_*) */
    int encoding;   /* encoding used for content (ENC_* value) */
}
http_request;

typedef struct
{
    const char* type;           /* content type */
    unsigned long size;         /* content length */
    const char* encoding;       /* if set, content encoding */
    unsigned long last_mod;     /* when was it last modified? */
    int flags;                  /* misc. flags */
}
http_file_info;

/* Write an error page (and header). Returns number of bytes written. */
size_t gen_error_page( int fd, int error );

/*
    Write 200 Ok header with. Returns the number of bytes
    written, 0 on failure.

    If set cookies is not NULL, it is pasted into the set-cookie header.
 */
size_t http_ok( int fd, const http_file_info* info, const char* setcookies );

/* parse a HTTP request, returns non-zero on success, zero on failure */
int http_request_parse( char* buffer, http_request* request );

/* Get the value of a named argument after using http_split_args. */
const char* http_get_arg( const char* argstr, int args, const char* arg );

/* pre-process an argument string (e.g. cookies or POST data). */
int http_split_args( char* argstr );

#endif /* HTTP_H */

