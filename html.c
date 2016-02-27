#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "html.h"
#include "http.h"

int html_process_template( string* page, int fd, const template_map* map,
                           unsigned int map_size )
{
    char buffer[ 1025 ];
    unsigned int k, len;
    ssize_t i, j, diff;
    int string = 0;

    while( (diff = read(fd, buffer, sizeof(buffer)-1)) > 0 )
    {
        if( buffer[diff-1]=='<' )
        {
            --diff;
            lseek( fd, -1, SEEK_CUR );
        }

        buffer[ diff ] = 0;

        if( !string_append( page, buffer ) )
            return -1;

        for( i=0; i<diff; ++i )
        {
            if( buffer[i]=='"' )
                string = ~string;

            if( !string && buffer[i]=='<' && buffer[i+1]=='?' )
            {
                lseek( fd, i-diff, SEEK_CUR );
                page->used -= diff - i;

                for( j=2, i+=2; isspace(buffer[i]); ++i, ++j ) { }

                for( k=0; k<map_size; ++k )
                {
                    len = strlen( map[k].name );
                    if( !strncmp( buffer+i, map[k].name, len ) )
                    {
                        if( isspace(buffer[i+len]) || buffer[i+len]=='>' )
                            break;
                    }
                }

                while( buffer[i] && buffer[i]!='>' ) { ++i; ++j; }

                /* End of buffer? Retry at tag start position */
                if( !buffer[i] )
                    break;

                /* Must be tag end, seek past tag & return id */
                if( buffer[i]!='>' || k==map_size )
                    return -1;

                lseek( fd, j+1, SEEK_CUR );
                return map[k].id;
            }
        }
    }
    return 0;
}

