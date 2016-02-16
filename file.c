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

    info->encoding = NULL;
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

int http_send_file( int method, int fd, long ifmod, const char* filename )
{
    int pfd[2], filefd = -1, hdrsize, ret = ERR_INTERNAL;
    http_file_info info;
    struct stat sb;

    if( stat( filename, &sb )!=0 ) return ERR_NOT_FOUND;
    if( !S_ISREG(sb.st_mode)     ) return ERR_FORBIDDEN;

    guess_type(filename, &info);
    info.size = sb.st_size;
    info.last_mod = sb.st_mtim.tv_sec;
    info.flags = FLAG_STATIC;

    if( ifmod >= info.last_mod )
    {
        info.flags |= FLAG_UNCHANGED;
        info.size = 0;
        info.encoding = NULL;
        method = HTTP_HEAD;
    }

    if( method==HTTP_HEAD        ) {http_ok(fd, &info, NULL); return 0;}
    if( method!=HTTP_GET         ) return ERR_METHOD;
    if( pipe( pfd )!=0           ) return ERR_INTERNAL;

    filefd = open( filename, O_RDONLY );
    hdrsize = http_ok( pfd[1], &info, NULL );

    if( filefd<=0 ) goto outpipe;
    if( !hdrsize  ) goto out;

    splice_to_sock( pfd, filefd, fd, sb.st_size, hdrsize, 0 );
    ret = 0;
out:
    close( filefd );
outpipe:
    close( pfd[0] );
    close( pfd[1] );
    return ret;
}

int send_zip( int method, int fd, long ifmod,
              const char* path, const char* zippath, int accept )
{
    int pfd[2], ret = ERR_NOT_FOUND, zipfile;
    http_file_info info;
    size_t hdrsize;
    struct stat sb;
    zip_header zip;

    if( stat( zippath, &sb )!=0                 ) goto out;
    if( (zipfile = open(zippath,O_RDONLY)) < 0  ) goto out;
    if( !zip_find_header( zipfile, &zip, path ) ) goto outzip;
    if( zip.algo!=0 && !(accept & ENC_DEFLATE)  ) goto outzip;

    guess_type( path, &info );
    info.encoding = zip.algo ? "deflate" : NULL;
    info.size = zip.size;
    info.last_mod = sb.st_mtim.tv_sec;
    info.flags = FLAG_STATIC;

    if( ifmod >= info.last_mod )
    {
        info.flags |= FLAG_UNCHANGED;
        info.size = 0;
        info.encoding = NULL;
        method = HTTP_HEAD;
    }

    if( method==HTTP_HEAD ) { http_ok(fd, &info, NULL); ret=0; goto outzip; }
    if( method!=HTTP_GET  ) { ret = ERR_METHOD; goto outzip; }
    if( pipe( pfd )!=0    ) { ret = ERR_INTERNAL; goto outzip; }
    hdrsize = http_ok( pfd[1], &info, NULL );

    if( hdrsize )
    {
        splice_to_sock( pfd, zipfile, fd, info.size, hdrsize, zip.pos );
        ret = 0;
    }

    close( pfd[0] );
    close( pfd[1] );
outzip:
    close( zipfile );
out:
    return ret;
}

