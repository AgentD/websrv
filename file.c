#include "error.h"
#include "file.h"
#include "http.h"
#include "sock.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>

#define ZIP_MAGIC 0x04034b50
#define ZIP_ENCRYPTED 0x41
#define ZIP_NO_SIZE 0x08

#define LE32( ptr ) ((ptr)[0]|((ptr)[1]<<8)|((ptr)[2]<<16)|((ptr)[3]<<24))
#define LE16( ptr ) ((ptr)[0]|((ptr)[1]<<8))

typedef struct
{
    int algo;
    size_t size;
    off_t pos;
}
zip_header;

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

static int zip_find_header( int fd, zip_header* hdr, const char* path )
{
    unsigned int name_len, flags;
    unsigned char data[512];
    off_t namepos;

    for( hdr->pos=0; pread(fd, data, 30, hdr->pos)==30; hdr->pos+=hdr->size )
    {
        flags     = LE16( data+6  );
        hdr->algo = LE16( data+8  );
        hdr->size = LE32( data+18 );
        name_len  = LE16( data+26 );

        if( (LE32(data) != ZIP_MAGIC) || (flags & ZIP_NO_SIZE) )
            break;

        hdr->pos += 30;
        namepos = hdr->pos;
        hdr->pos += name_len + LE16( data+28 );

        if( !(flags & ZIP_ENCRYPTED) && (hdr->algo < 9) && (hdr->algo!=7) &&
            (name_len < sizeof(data)) )
        {
            pread( fd, data, name_len, namepos );
            if( !strncmp( (void*)data, path, name_len ) && !path[name_len] )
                return 1;
        }
    }
    return 0;
}

static int send_file( int fd, const http_request* req, int filefd, int algo,
                      off_t offset, size_t size, long lastmod )
{
    int pfd[2], hdrsize, ret = ERR_INTERNAL;
    http_file_info info;

    guess_type( req->path, &info );
    info.encoding = algo ? "deflate" : NULL;
    info.size = size;
    info.last_mod = lastmod;
    info.flags = FLAG_STATIC;

    if( req->ifmod >= lastmod )
    {
        info.flags |= FLAG_UNCHANGED;
        info.size = 0;
        info.encoding = NULL;
    }

    if( info.flags & FLAG_UNCHANGED ) { http_ok(fd, &info, NULL); return 0; }
    if( req->method==HTTP_HEAD      ) { http_ok(fd, &info, NULL); return 0; }
    if( req->method!=HTTP_GET       ) return ERR_METHOD;
    if( pipe( pfd )!=0              ) return ERR_INTERNAL;

    if( (hdrsize = http_ok( pfd[1], &info, NULL )) )
    {
        splice_to_sock( pfd, filefd, fd, size, hdrsize, offset );
        ret = 0;
    }

    close( pfd[0] );
    close( pfd[1] );
    return ret;
}

int http_send_file( int dirfd, int fd, const http_request* req )
{
    int filefd = -1, ret;
    struct stat sb;

    if( faccessat(dirfd, req->path, F_OK, 0)!=0 ) return ERR_NOT_FOUND;
    if( faccessat(dirfd, req->path, R_OK, 0)!=0 ) return ERR_FORBIDDEN;
    if( fstatat(dirfd, req->path, &sb, 0)!=0    ) return ERR_INTERNAL;
    if( !S_ISREG(sb.st_mode)                    ) return ERR_FORBIDDEN;

    filefd = openat( dirfd, req->path, O_RDONLY );
    if( filefd < 0 )
        return ERR_INTERNAL;

    ret = send_file( fd, req, filefd, 0, 0, sb.st_size, sb.st_mtim.tv_sec );
    close( filefd );
    return ret;
}

int send_zip( int zipfile, int fd, const http_request* req )
{
    struct stat sb;
    zip_header zip;

    if( fstat( zipfile, &sb )!=0                     ) return ERR_NOT_FOUND;
    if( !zip_find_header( zipfile, &zip, req->path ) ) return ERR_NOT_FOUND;
    if( zip.algo!=0 && !(req->accept & ENC_DEFLATE)  ) return ERR_NOT_FOUND;

    return send_file( fd, req, zipfile, zip.algo,
                      zip.pos, zip.size, sb.st_mtim.tv_sec );
}

