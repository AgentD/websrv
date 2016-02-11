#ifndef HTML_H
#define HTML_H

#include <stddef.h>

#include "str.h"

#define HTML_NONE 0
#define HTML_4 4
#define HTML_5 5

#define STYLE_NONE 0
#define STYLE_ID 1
#define STYLE_CLASS 2
#define STYLE_INLINE 3

#define INP_TEXT 0
#define INP_PASSWD 1
#define INP_SUBMIT 2
#define INP_RADIO 3
#define INP_CHECKBOX 4
#define INP_BUTTON 5
#define INP_RESET 6

#define INP_CHECKED 0x01
#define INP_DISABLED 0x02
#define INP_READONYLY 0x04

typedef struct
{
    const char* name;
    int id;
}
template_map;

/* initialize a string, write doctype and <html> tag if desired */
int html_page_init( string* page, int standard );

/* generate a tile section and <body> tag */
int html_page_begin( string* page, const char* title,
                                      const char* stylesheet );

/* begin generating an HTML table */
int html_table_begin( string* page, const char* style, int styletype );

/* generate an entire table row. If elements is 0, only a <tr> is generated */
int html_table_row( string* page, int elements, ... );

/* begin generating an HTML form */
int html_form_begin( string* page, const char* action, int method );

/* generate an input element for a form */
int html_form_input( string* page, int type, int flags, const char* name,
                     const char* value );

/*
    Read a template from a file descriptor (fd) and append it to a page.

    If the tag "<? .... >" is encoutered in the template, it is not inserted
    into the page. Instead, the template map is used to resolve whatever is in
    the tag to a numeric ID that this function returns.

    If end of file is reached, the function returns 0. On error, -1 is
    returned.
 */
int html_process_template( string* page, int fd, const template_map* map,
                           unsigned int map_size );

#define html_table_header( page ) string_append( (page), "<th>" )
#define html_table_end_header( page ) string_append( (page), "</th>" )
#define html_table_element( page ) string_append( (page), "<td>" )
#define html_table_end_element( page ) string_append( (page), "</td>" )
#define html_table_end_row( page ) string_append( (page), "</tr>" )
#define html_table_end( page ) string_append( (page), "</table>" )
#define html_form_end( page ) string_append( (page), "</form>" )
#define html_page_end( page ) string_append( (page), "</body></html>" )

#endif /* HTML_H */

