#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "html.h"
#include "http.h"


const char* html5_doctype = "<!DOCTYPE html>";
const char* html4_doctype = "<!DOCTYPE HTML PUBLIC "
                            "\"-//W3C//DTD HTML 4.01//EN\" "
                            "\"http://www.w3.org/TR/html4/strict.dtd\">";


int html_page_init( html_page* page, int standard )
{
    page->avail = 1024;
    page->data = malloc( page->avail );

    if( !page->data )
        return 0;

    if( standard==HTML_5 )
    {
        page->used = strlen(html5_doctype);
        memcpy( page->data, html5_doctype, page->used );
    }
    else if( standard==HTML_4 )
    {
        page->used = strlen(html4_doctype);
        memcpy( page->data, html4_doctype, page->used );
    }
    else
    {
        page->used = 0;
        return 1;
    }

    return html_append_raw( page, "<html>" );
}

void html_page_cleanup( html_page* page )
{
    free( page->data );
}

int html_append_raw( html_page* page, const char* str )
{
    size_t len = strlen(str), newsize;
    char* new;

    if( (page->used + len) > page->avail )
    {
        newsize = page->used + len + 512;

        new = realloc( page->data, newsize );

        if( !new )
            return 0;

        page->data = new;
        page->avail = newsize;
    }

    memcpy( page->data + page->used, str, len );
    page->used += len;
    return 1;
}

int html_page_begin( html_page* page, const char* title,
                                      const char* stylesheet )
{
    int ret = html_append_raw( page, "<head>" );

    if( title && ret )
    {
        ret = html_append_raw( page, "<title>" ) &&
              html_append_raw( page, title ) &&
              html_append_raw( page, "</title>" );
    }

    if( stylesheet && ret )
    {
        ret = html_append_raw( page, "<link rel=\"stylesheet\" "
                                     "type=\"text/css\" href=\"" ) &&
              html_append_raw( page, stylesheet ) &&
              html_append_raw( page, "\">" );
    }

    return ret && html_append_raw( page, "</head><body>" );
}

int html_table_begin( html_page* page, const char* style, int styletype )
{
    int ret = html_append_raw(page, "<table");

    if( style )
    {
        switch( styletype )
        {
        case STYLE_ID:     ret = html_append_raw(page, " class=\""); break;
        case STYLE_CLASS:  ret = html_append_raw(page, " id=\""   ); break;
        case STYLE_INLINE: ret = html_append_raw(page, " style=\""); break;
        default:           goto out;
        }
        ret = ret && html_append_raw( page, style ) &&
                     html_append_raw( page, "\"" );
    }
out:
    return ret && html_append_raw( page, ">" );
}

int html_table_row( html_page* page, int elements, ... )
{
    const char* str;
    va_list ap;
    int i;

    if( !html_append_raw( page, "<tr>" ) )
        return 0;

    va_start(ap, elements);
    for( i=0; i<elements; ++i )
    {
        str = va_arg(ap,char*);
        str = str ? str : "";

        if( !html_append_raw( page, "<td>"  ) ) return 0;
        if( !html_append_raw( page, str     ) ) return 0;
        if( !html_append_raw( page, "</td>" ) ) return 0;
    }
    va_end(ap);

    return !elements || html_append_raw( page, "</tr>" );
}

int html_form_begin( html_page* page, const char* action, int method )
{
    int ret = html_append_raw( page, "<form" );

    if( action && ret )
    {
        ret = html_append_raw( page, " action=\"" ) &&
              html_append_raw( page, action ) &&
              html_append_raw( page, "\"" );
    }

    return ret && html_append_raw( page, " method=\"" ) &&
                  html_append_raw( page, method==HTTP_POST?"post":"get" ) &&
                  html_append_raw( page, "\">" );
}

int html_form_input( html_page* page, int type, int flags, const char* name,
                     const char* value )
{
    int ret = html_append_raw( page, "<input type=\"" );

    switch( type )
    {
    case INP_TEXT:     ret = ret && html_append_raw(page, "text"    ); break;
    case INP_PASSWD:   ret = ret && html_append_raw(page, "password"); break;
    case INP_SUBMIT:   ret = ret && html_append_raw(page, "submit"  ); break;
    case INP_RADIO:    ret = ret && html_append_raw(page, "radio"   ); break;
    case INP_CHECKBOX: ret = ret && html_append_raw(page, "checkbox"); break;
    case INP_BUTTON:   ret = ret && html_append_raw(page, "button"  ); break;
    case INP_RESET:    ret = ret && html_append_raw(page, "reset"   ); break;
    }

    ret = ret && html_append_raw( page, "\"" );

    if( name && ret )
    {
        ret = ret && html_append_raw( page, " name=\"" ) &&
                     html_append_raw( page, name ) &&
                     html_append_raw( page, "\"" );
    }
    if( value && ret )
    {
        ret = ret && html_append_raw( page, " value=\"" ) &&
                     html_append_raw( page, value ) &&
                     html_append_raw( page, "\"" );
    }

    if( flags & INP_CHECKED   ) ret=ret && html_append_raw(page," checked" );
    if( flags & INP_DISABLED  ) ret=ret && html_append_raw(page," disabled");
    if( flags & INP_READONYLY ) ret=ret && html_append_raw(page," readonly");

    return ret && html_append_raw( page, ">" );
}

