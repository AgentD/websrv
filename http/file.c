#include "error.h"
#include "file.h"
#include "http.h"
#include "sock.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>

static struct { const char* ending; const char* mime; } mimemap[] =
{
    { "js",   "application/javascript; charset=utf-8" },
    { "json", "application/json; charset=utf-8"       },
    { "xml",  "application/xml; charset=utf-8"        },
    { "html", "text/html; charset=utf-8"              },
    { "htm",  "text/html; charset=utf-8"              },
    { "css",  "text/css; charset=utf-8"               },
    { "csv",  "text/csv; charset=utf-8"               },
    { "pdf",  "application/pdf"                       },
    { "zip",  "application/zip"                       },
    { "gz",   "application/gzip"                      },
    { "png",  "image/png"                             },
    { "bmp",  "image/bmp"                             },
    { "jpg",  "image/jpeg"                            },
    { "jpeg", "image/jpeg"                            },
    { "tiff", "image/tiff"                            },
    { "txt",  "text/plain; charset=utf-8"             },
    { "ico",  "image/x-icon"                          },
};

static void guess_type( const char* name, http_file_info* info )
{
    size_t i, count;

    info->type = "application/octet-stream";

    if( !(name = strrchr( name, '.' )) )
        return;

    ++name;
    count = sizeof(mimemap) / sizeof(mimemap[0]);

    for( i=0; i<count; ++i )
    {
        if( !strcmp( name, mimemap[i].ending ) )
            info->type = mimemap[i].mime;
    }
}

static int send_file( int fd, const http_request* req, int filefd,
                      int isgzip, struct stat* sb )
{
    int pfd[2], hdrsize, ret = ERR_INTERNAL, status = 0;
    http_file_info info;

    guess_type( req->path, &info );
    info.encoding = isgzip ? "gzip" : NULL;
    info.size = sb->st_size;
    info.last_mod = sb->st_mtim.tv_sec;
    info.flags = FLAG_STATIC;

    if( req->ifmod >= info.last_mod )
    {
        info.size = 0;
        info.encoding = NULL;
        status = ERR_UNCHANGED;
        goto outhdr;
    }

    if( req->method==HTTP_HEAD ) goto outhdr;
    if( req->method!=HTTP_GET  ) return ERR_METHOD;
    if( pipe( pfd )!=0         ) return ERR_INTERNAL;

    if( (hdrsize = http_response_header( pfd[1], &info, NULL, 0 )) )
    {
        splice_to_sock( pfd, filefd, fd, info.size, hdrsize, 0 );
        ret = 0;
    }

    close( pfd[0] );
    close( pfd[1] );
    return ret;
outhdr:
    http_response_header( fd, &info, 0, status );
    return 0;
}

int http_send_file( int dirfd, int fd, const http_request* req )
{
    int filefd = -1, ret, isgzip = 0;
    const char *path, *ptr;
    char temp[PATH_MAX];
    struct stat sb;

    path = req->path;

    if( req->accept & ENC_GZIP )
    {
        ptr = strrchr( req->path, '.' );
        if( ptr && !strcmp(ptr, ".gz") )
            goto skip;
        if( (strlen(req->path) + 4) > sizeof(temp) )
            goto skip;
        strcpy( temp, req->path );
        strcat( temp, ".gz" );
        if( faccessat(dirfd, temp, R_OK, 0)!=0 )
            goto skip;
        path = temp;
        isgzip = 1;
    }
skip:
    if( faccessat(dirfd, path, F_OK, 0)!=0 ) return ERR_NOT_FOUND;
    if( faccessat(dirfd, path, R_OK, 0)!=0 ) return ERR_FORBIDDEN;
    if( fstatat(dirfd, path, &sb, 0)!=0    ) return ERR_INTERNAL;
    if( !S_ISREG(sb.st_mode)               ) return ERR_FORBIDDEN;

    filefd = openat( dirfd, path, O_RDONLY );
    if( filefd < 0 )
        return ERR_INTERNAL;

    ret = send_file( fd, req, filefd, isgzip, &sb );
    close( filefd );
    return ret;
}

