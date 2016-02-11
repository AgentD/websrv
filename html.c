#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "html.h"
#include "http.h"


const char* html5_doctype = "<!DOCTYPE html>";
const char* html4_doctype = "<!DOCTYPE HTML PUBLIC "
                            "\"-//W3C//DTD HTML 4.01//EN\" "
                            "\"http://www.w3.org/TR/html4/strict.dtd\">";


int html_page_init( string* page, int standard )
{
    int ret;

    if( !string_init( page ) )
        return 0;

    switch( standard )
    {
    case HTML_5: ret = string_append( page, html5_doctype ); break;
    case HTML_4: ret = string_append( page, html4_doctype ); break;
    default:     ret = 1;
    }

    if( !ret )
        string_cleanup( page );

    return ret;
}

int html_page_begin( string* page, const char* title, const char* stylesheet )
{
    int ret = string_append( page, "<head>" );

    if( title && ret )
    {
        ret = string_append( page, "<title>" ) &&
              string_append( page, title ) &&
              string_append( page, "</title>" );
    }

    if( stylesheet && ret )
    {
        ret = string_append( page, "<link rel=\"stylesheet\" "
                                   "type=\"text/css\" href=\"" ) &&
              string_append( page, stylesheet ) &&
              string_append( page, "\">" );
    }

    return ret && string_append( page, "</head><body>" );
}

int html_table_begin( string* page, const char* style, int styletype )
{
    int ret = string_append(page, "<table");

    if( style )
    {
        switch( styletype )
        {
        case STYLE_ID:     ret = string_append(page, " class=\""); break;
        case STYLE_CLASS:  ret = string_append(page, " id=\""   ); break;
        case STYLE_INLINE: ret = string_append(page, " style=\""); break;
        default:           goto out;
        }
        ret = ret && string_append( page, style ) &&
                     string_append( page, "\"" );
    }
out:
    return ret && string_append( page, ">" );
}

int html_table_row( string* page, int elements, ... )
{
    const char* str;
    va_list ap;
    int i;

    if( !string_append( page, "<tr>" ) )
        return 0;

    va_start(ap, elements);
    for( i=0; i<elements; ++i )
    {
        str = va_arg(ap,char*);
        str = str ? str : "";

        if( !string_append( page, "<td>"  ) ) return 0;
        if( !string_append( page, str     ) ) return 0;
        if( !string_append( page, "</td>" ) ) return 0;
    }
    va_end(ap);

    return !elements || string_append( page, "</tr>" );
}

int html_form_begin( string* page, const char* action, int method )
{
    int ret = string_append( page, "<form" );

    if( action && ret )
    {
        ret = string_append( page, " action=\"" ) &&
              string_append( page, action ) &&
              string_append( page, "\"" );
    }

    return ret && string_append( page, " method=\"" ) &&
                  string_append( page, method==HTTP_POST?"post":"get" ) &&
                  string_append( page, "\">" );
}

int html_form_input( string* page, int type, int flags, const char* name,
                     const char* value )
{
    int ret = string_append( page, "<input type=\"" );

    switch( type )
    {
    case INP_TEXT:     ret = ret && string_append(page, "text"    ); break;
    case INP_PASSWD:   ret = ret && string_append(page, "password"); break;
    case INP_SUBMIT:   ret = ret && string_append(page, "submit"  ); break;
    case INP_RADIO:    ret = ret && string_append(page, "radio"   ); break;
    case INP_CHECKBOX: ret = ret && string_append(page, "checkbox"); break;
    case INP_BUTTON:   ret = ret && string_append(page, "button"  ); break;
    case INP_RESET:    ret = ret && string_append(page, "reset"   ); break;
    }

    ret = ret && string_append( page, "\"" );

    if( name && ret )
    {
        ret = ret && string_append( page, " name=\"" ) &&
                     string_append( page, name ) &&
                     string_append( page, "\"" );
    }
    if( value && ret )
    {
        ret = ret && string_append( page, " value=\"" ) &&
                     string_append( page, value ) &&
                     string_append( page, "\"" );
    }

    if( flags & INP_CHECKED   ) ret=ret && string_append(page," checked" );
    if( flags & INP_DISABLED  ) ret=ret && string_append(page," disabled");
    if( flags & INP_READONYLY ) ret=ret && string_append(page," readonly");

    return ret && string_append( page, ">" );
}

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

