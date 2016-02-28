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
    char buffer[ 1025 ], *ptr;
    unsigned int k, len;
    ssize_t i, diff;

    while( (diff = read(fd, buffer, sizeof(buffer)-1)) > 0 )
    {
        buffer[ diff ] = 0;

        ptr = strchr( buffer, '$' );

        if( ptr )
        {
            i = ptr - buffer;
            lseek( fd, i - diff, SEEK_CUR );

            if( i && !string_append_len( page, buffer, i ) )
                return -1;

            for( k=0; k<map_size; ++k )
            {
                len = strlen( map[k].name );
                if( !strncmp(ptr, map[k].name, len) && !isalnum(ptr[len]) )
                {
                    lseek( fd, len, SEEK_CUR );
                    return map[k].id;
                }
            }
        }
        else if( !string_append_len( page, buffer, diff ) )
        {
            return -1;
        }
    }
    return 0;
}

