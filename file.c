#include "file.h"
#include "http.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <alloca.h>
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
    size_t total = 0, hdrsize;
    int pfd[2], filefd = -1;
    char* absolute;
    struct stat sb;
    ssize_t count;

    if( pipe( pfd )!=0 )
        return;

    count = strlen(basedir);
    if( count && basedir[count-1]!='/' )
        ++count;

    absolute = alloca( count + strlen(filename) + 1 );
    strcpy( absolute, basedir );

    if( count && absolute[count-1]!='/' )
        absolute[count-1] = '/';

    strcpy( absolute+count, filename );

    if( stat( absolute, &sb )!=0 )
    {
        hdrsize = http_not_found( pfd[1] );
        goto transfer;
    }
    if( !S_ISREG(sb.st_mode) )
    {
        hdrsize = http_forbidden( pfd[1] );
        goto transfer;
    }
    if( method!=HTTP_GET && method!=HTTP_HEAD )
    {
        hdrsize = http_not_allowed( pfd[1] );
        goto transfer;
    }

    if( method==HTTP_GET )
    {
        filefd = open( absolute, O_RDONLY );
        total = sb.st_size;
        if( filefd<=0 )
        {
            hdrsize = http_internal_error( pfd[1] );
            goto transfer;
        }
    }

    hdrsize = http_ok( pfd[1], guess_type(filename), sb.st_size );
    if( !hdrsize )
        goto out;

transfer:
    for( ; hdrsize; hdrsize-=count )
    {
        count = splice(pfd[0],0,fd,0,hdrsize,SPLICE_F_MORE|SPLICE_F_MOVE);
        if( count<=0 )
            goto out;
    }

    for( ; total; total-=count )
    {
        count = splice(filefd,0,pfd[1],0,total,SPLICE_F_MORE|SPLICE_F_MOVE);
        if( count<=0 )
            break;
        if( splice(pfd[0], 0, fd, 0, count, SPLICE_F_MORE|SPLICE_F_MOVE)<=0 )
            break;
    }
out:
    if( filefd>0 )
        close( filefd );
    close( pfd[0] );
    close( pfd[1] );
}

