#include "file.h"
#include "http.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>

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
    size_t i, count, len;
    const char* ptr;

    info->encoding = NULL;
    info->type = "application/octet-stream";

    if( !(ptr = strrchr( name, '.' )) )
        return;

    len = strlen(ptr+1);

    if( !strcmp( ptr, ".gz" ) )
    {
        for( --ptr; ptr>name && *ptr!='.'; ) { --ptr; }

        if( ptr==name || !strcmp( ptr, ".tar.gz" ) )
        {
            ptr = strrchr( name, '.' );
        }
        else
        {
            len = strchr( ptr+1, '.' ) - ptr - 1;
            info->encoding = "gzip";
        }
    }

    ++ptr;
    count = sizeof(mimemap) / sizeof(mimemap[0]);

    for( i=0; i<count; ++i )
    {
        if( strlen( mimemap[i].ending )!=len )
            continue;
        if( !strncmp( ptr, mimemap[i].ending, len ) )
            info->type = mimemap[i].mime;
    }
}

static void splice_file( int* pfd, int filefd, int sockfd,
                         size_t filesize, size_t pipedata )
{
    ssize_t count;

    while( filesize || pipedata )
    {
        if( filesize )
        {
            count = splice( filefd, 0, pfd[1], 0, filesize,
                            SPLICE_F_MOVE|SPLICE_F_MORE );
            if( count<=0 )
                break;
            pipedata += count;
            filesize -= count;
        }
        if( pipedata )
        {
            count = splice( pfd[0], 0, sockfd, 0, pipedata,
                            SPLICE_F_MOVE|SPLICE_F_MORE );
            if( count<=0 )
                break;
            pipedata -= count;
        }
    }
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

    splice_file( pfd, filefd, fd, sb.st_size, hdrsize );
out:
    close( filefd );
outpipe:
    close( pfd[0] );
    close( pfd[1] );
}

