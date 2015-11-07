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

static const char* guess_type( const char* name )
{
    size_t i, count;

    name = strrchr( name, '.' );

    if( name )
    {
        ++name;
        count = sizeof(mimemap) / sizeof(mimemap[0]);

        for( i=0; i<count; ++i )
        {
            if( !strcmp( name, mimemap[i].ending ) )
                return mimemap[i].mime;
        }
    }

    return "application/octet-stream";
}

void http_send_file( int method, int fd,
                     const char* filename, const char* basedir )
{
    size_t total, hdrsize, count, pipedata;
    int pfd[2], filefd = -1;
    struct stat sb;

    if( chdir( basedir )!=0 )
    {
        http_internal_error( fd );
        return;
    }

    /* get file size (if it exists and is a regular file) */
    if( stat( filename, &sb )!=0 )
    {
        http_not_found( fd );
        return;
    }
    if( !S_ISREG(sb.st_mode) )
    {
        http_forbidden( fd );
        return;
    }

    /* check HTTP method */
    if( method==HTTP_HEAD )
    {
        http_ok( fd, guess_type(filename), sb.st_size );
        return;
    }
    if( method!=HTTP_GET )
    {
        http_not_allowed( fd );
        return;
    }

    /* create pipe for splicing */
    if( pipe( pfd )!=0 )
    {
        http_internal_error( fd );
        return;
    }

    /* open file */
    filefd = open( filename, O_RDONLY );
    total = sb.st_size;

    if( filefd<=0 )
    {
        http_internal_error( fd );
        goto outpipe;
    }

    /* write header to pipe */
    hdrsize = http_ok( pfd[1], guess_type(filename), sb.st_size );

    if( !hdrsize )
    {
        http_internal_error( fd );
        goto out;
    }

    /* send data */
    pipedata = hdrsize;

    while( total || pipedata )
    {
        if( total )
        {
            count = splice( filefd, 0, pfd[1], 0, total,
                            SPLICE_F_MOVE|SPLICE_F_MORE );
            if( count<=0 )
                break;
            pipedata += count;
            total -= count;
        }
        if( pipedata )
        {
            count = splice( pfd[0], 0, fd, 0, pipedata,
                            SPLICE_F_MOVE|SPLICE_F_MORE );
            if( count<=0 )
                break;
            pipedata -= count;
        }
    }

out:
    close( filefd );
outpipe:
    close( pfd[0] );
    close( pfd[1] );
}

