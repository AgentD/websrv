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

/* if set, allow caching and add last modified heder */
#define FLAG_STATIC 0x01

/* if set, do not allow caching */
#define FLAG_DYNAMIC 0x02

/* if set generate a 304 response instead of 200 */
#define FLAG_UNCHANGED 0x04

/* client has to make GET request when following redirection */
#define FLAG_REDIR_FORCE_GET 0x08

#define ENC_DEFLATE 0x01
#define ENC_GZIP 0x02

typedef struct
{
    char* buffer;   /* static buffer to allocate strings from */
    size_t size;    /* size of buffer */
    size_t used;    /* number of buffer bytes used so far */

    int method;           /* request method */
    const char* path;     /* requested path */
    const char* host;     /* hostname field */
    const char* type;     /* content-type */
    const char* getargs;  /* arguments pasted to path string */
    int numargs;          /* number of get get-arguments */
    const char* cookies;  /* pointer to cookie args */
    int numcookies; /* number of cookies */
    size_t length;  /* content-length */
    long ifmod;     /* Only send the file if newer than this */
    int accept;     /* accepted encoding flags (ENC_*) */
    int encoding;   /* encoding used for content (ENC_* value) */
}
http_request;

typedef struct
{
    const char* type;           /* content type */
    unsigned long size;         /* content length */
    const char* encoding;       /* if set, content encoding */
    const char* redirect;       /* if set, redirect client there */
    long last_mod;              /* unix time stamp of last modification */
    int flags;                  /* misc. flags */
}
http_file_info;

/* Get a string describing a HTTP_* method ID */
const char* http_method_to_string( unsigned int method );

size_t http_response_header( int fd, const http_file_info* info,
                             const char* setcookies, const char* status );

/*
    Write 200 Ok header with. Returns the number of bytes
    written, 0 on failure.

    If set cookies is not NULL, it is pasted into the set-cookie header.
 */
size_t http_ok( int fd, const http_file_info* info, const char* setcookies );

/* Parse "METHOD <path> <version>" line and initialize an http request */
int http_request_init( http_request* rq, const char* request,
                       char* stringbuffer, size_t size );

/* Parse a "key: value" attribute line */
int http_parse_attribute( http_request* rq, char* line );

/* Get the value of a named argument after using http_split_args. */
const char* http_get_arg( const char* argstr, int args, const char* arg );

/* pre-process an argument string (e.g. cookies or POST data). */
int http_split_args( char* argstr );

#endif /* HTTP_H */

