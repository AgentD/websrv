#include "file.h"
#include "http.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>

#define ZIP_MAGIC 0x04034b50
#define ZIP_ENCRYPTED 0x41
#define ZIP_NO_SIZE 0x08

#define SPLICE_FLAGS (SPLICE_F_MOVE|SPLICE_F_MORE)

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
    { "js",   "application/javascript" },
    { "json", "application/json"       },
    { "xml",  "application/xml"        },
    { "html", "text/html"              },
    { "htm",  "text/html"              },
    { "css",  "text/css"               },
    { "csv",  "text/csv"               },
    { "pdf",  "application/pdf"        },
    { "zip",  "application/zip"        },
    { "gz",   "application/gzip"       },
    { "png",  "image/png"              },
    { "bmp",  "image/bmp"              },
    { "jpg",  "image/jpeg"             },
    { "jpeg", "image/jpeg"             },
    { "tiff", "image/tiff"             },
    { "txt",  "text/plain"             },
    { "ico",  "image/x-icon"           },
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

static void splice_file( int* pfd, int filefd, int sockfd,
                         size_t filesize, size_t pipedata,
                         off_t offset )
{
    loff_t off = offset;
    ssize_t count;

    while( filesize || pipedata )
    {
        if( filesize )
        {
            count = splice( filefd, &off, pfd[1], 0, filesize, SPLICE_FLAGS );
            if( count<=0 )
                break;
            pipedata += count;
            filesize -= count;
        }
        if( pipedata )
        {
            count = splice( pfd[0], 0, sockfd, 0, pipedata, SPLICE_FLAGS );
            if( count<=0 )
                break;
            pipedata -= count;
        }
    }
}

static int zip_find_header( int fd, zip_header* hdr, const char* path )
{
    unsigned int magic, name_length, extra_length, flags;
    unsigned char data[512];

    hdr->pos = 0;

    while( pread( fd, data, 30, hdr->pos ) == 30 )
    {
        magic         = LE32( data    );
        flags         = LE16( data+6  );
        hdr->algo     = LE16( data+8  );
        hdr->size     = LE32( data+18 );
        name_length   = LE16( data+26 );
        extra_length  = LE16( data+28 );
        hdr->pos     += 30;

        if( (magic != ZIP_MAGIC) || (flags & ZIP_NO_SIZE) )
            break;

        if( (flags & ZIP_ENCRYPTED) || (hdr->algo > 8) || (hdr->algo==7) ||
            (name_length >= sizeof(data)) )
        {
            hdr->pos += hdr->size + extra_length + name_length;
            continue;
        }

        pread( fd, data, name_length, hdr->pos );
        data[ name_length ] = 0;
        hdr->pos += extra_length + name_length;

        if( !strcmp( (void*)data, path ) )
            return 1;

        hdr->pos += hdr->size;
    }
    return 0;
}

void http_send_file( int method, int fd, unsigned long ifmod,
                     const char* filename, const char* basedir )
{
    int pfd[2], filefd = -1, hdrsize;
    http_file_info info;
    struct stat sb;

    if( chdir( basedir )!=0      ) {gen_error_page(fd,ERR_INTERNAL );return;}
    if( stat( filename, &sb )!=0 ) {gen_error_page(fd,ERR_NOT_FOUND);return;}
    if( !S_ISREG(sb.st_mode)     ) {gen_error_page(fd,ERR_FORBIDDEN);return;}

    guess_type(filename, &info);
    info.size = sb.st_size;
    info.last_mod = sb.st_mtim.tv_sec;
    info.flags = FLAG_STATIC;

    if( ifmod >= info.last_mod )
    {
        info.flags |= FLAG_UNCHANGED;
        method = HTTP_HEAD;
    }

    if( method==HTTP_HEAD        ) {http_ok(fd, &info, NULL);        return;}
    if( method!=HTTP_GET         ) {gen_error_page(fd,ERR_METHOD   );return;}
    if( pipe( pfd )!=0           ) {gen_error_page(fd,ERR_INTERNAL );return;}

    filefd = open( filename, O_RDONLY );
    hdrsize = http_ok( pfd[1], &info, NULL );

    if( filefd<=0 ) { gen_error_page(fd,ERR_INTERNAL); goto outpipe; }
    if( !hdrsize  ) { gen_error_page(fd,ERR_INTERNAL); goto out; }

    splice_file( pfd, filefd, fd, sb.st_size, hdrsize, 0 );
out:
    close( filefd );
outpipe:
    close( pfd[0] );
    close( pfd[1] );
}

int send_zip( int method, int fd, unsigned long ifmod,
              const char* path, int zipfile )
{
    http_file_info info;
    size_t hdrsize;
    struct stat sb;
    zip_header zip;
    int pfd[2];

    if( !zip_find_header( zipfile, &zip, path ) )
        return 0;

    if( fstat( zipfile, &sb )!=0 ) {gen_error_page(fd,ERR_INTERNAL);return 1;}

    guess_type( path, &info );
    info.encoding = zip.algo ? "deflate" : NULL;
    info.size = zip.size;
    info.last_mod = sb.st_mtim.tv_sec;
    info.flags = FLAG_STATIC;

    if( ifmod >= info.last_mod )
    {
        info.flags |= FLAG_UNCHANGED;
        method = HTTP_HEAD;
    }

    if( method==HTTP_HEAD ) { http_ok(fd, &info, NULL);         return 1; }
    if( method!=HTTP_GET  ) { gen_error_page(fd,ERR_METHOD   ); return 1; }
    if( pipe( pfd )!=0    ) { gen_error_page(fd,ERR_INTERNAL ); return 1; }
    hdrsize = http_ok( pfd[1], &info, NULL );

    if( hdrsize )
        splice_file( pfd, zipfile, fd, info.size, hdrsize, zip.pos );
    else
        gen_error_page( fd, ERR_INTERNAL );

    close( pfd[0] );
    close( pfd[1] );
    return 1;
}

