#ifndef HTML_H
#define HTML_H

#include <stddef.h>

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
    size_t used;
    size_t avail;
    char* data;
}
html_page;

/* initialize a page, write doctype and <html> tag */
int html_page_init( html_page* page, int standard );

/* free all memory asociated with a page */
void html_page_cleanup( html_page* page );

/* append a raw string to a page */
int html_append_raw( html_page* page, const char* str );

/* generate a tile section and <body> tag */
int html_page_begin( html_page* page, const char* title,
                                      const char* stylesheet );

/* begin generating an HTML table */
int html_table_begin( html_page* page, const char* style, int styletype );

/* generate an entire table row. If elements is 0, only a <tr> is generated */
int html_table_row( html_page* page, int elements, ... );

/* begin generating an HTML form */
int html_form_begin( html_page* page, const char* action, int method );

/* generate an input element for a form */
int html_form_input( html_page* page, int type, int flags, const char* name,
                     const char* value );

#define html_table_header( page ) html_append_raw( (page), "<th>" )
#define html_table_end_header( page ) html_append_raw( (page), "</th>" )
#define html_table_element( page ) html_append_raw( (page), "<td>" )
#define html_table_end_element( page ) html_append_raw( (page), "</td>" )
#define html_table_end_row( page ) html_append_raw( (page), "</tr>" )
#define html_table_end( page ) html_append_raw( (page), "</table>" )
#define html_form_end( page ) html_append_raw( (page), "</form>" )
#define html_page_end( page ) html_append_raw( (page), "</body></html>" )

#endif /* HTML_H */

