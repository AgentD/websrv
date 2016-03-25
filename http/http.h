#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

#include "str.h"

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
#define ERR_REDIRECT 9
#define ERR_REDIRECT_GET 10
#define ERR_UNCHANGED 11

/* static page (allow caching for up to 1 day) */
#define FLAG_STATIC 0x01

/* static image/archive/etc... (allow caching for up to 1 week) */
#define FLAG_STATIC_RESOURCE 0x02

/* if set, do not allow caching */
#define FLAG_DYNAMIC 0x04

#define ENC_DEFLATE 0x01
#define ENC_GZIP 0x02

typedef struct
{
    char* buffer;   /* static buffer to allocate strings from */
    size_t size;    /* size of buffer */
    size_t used;    /* number of buffer bytes used so far */

    int method;           /* request method */
    int numargs;          /* number of get get-arguments */
    int numcookies;       /* number of cookies */
    int accept;           /* accepted encoding flags (ENC_*) */
    int encoding;         /* encoding used for content (ENC_* value) */
    long ifmod;           /* Only send the file if newer than this */
    unsigned long length; /* content-length */
    const char* path;     /* requested path */
    const char* host;     /* hostname field */
    const char* type;     /* content-type */
    const char* getargs;  /* arguments pasted to path string */
    const char* cookies;  /* pointer to cookie args */
}
http_request;

typedef struct
{
    int status;                 /* an ERR_* status code */
    int flags;                  /* misc. flags */
    long last_mod;              /* unix time stamp of last modification */
    unsigned long size;         /* content length */
    const char* type;           /* content type */
    const char* encoding;       /* if set, content encoding */
    const char* redirect;       /* if set, redirect client there */
    const char* setcookies;     /* if set, added via set-cookie field */
}
http_file_info;

/* Get a string describing a HTTP_* method ID */
const char* http_method_to_string( unsigned int method );

size_t http_response_header( int fd, const http_file_info* info );

/* Parse "METHOD <path> <version>" line and initialize an http request */
int http_request_init( http_request* rq, const char* request,
                       char* stringbuffer, size_t size );

/* Parse a "key: value" attribute line */
int http_parse_attribute( http_request* rq, char* line );

/*
    Write a default page to an _uninitialized_ string and fill the file info
    structure. If the client accepts compressed data, the string is
    compressed.

    Returns non-zero on sucess.
 */
int gen_default_page(string* page, http_file_info* info,
                     int status, int accept, const char* redirect);

/* Get the value of a named argument after using http_split_args. */
const char* http_get_arg( const char* argstr, int args, const char* arg );

/* pre-process an argument string (e.g. cookies or POST data). */
int http_split_args( char* argstr );

#endif /* HTTP_H */

