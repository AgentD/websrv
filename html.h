#ifndef HTML_H
#define HTML_H

#include <stddef.h>

#include "str.h"

typedef struct
{
    const char* name;
    int id;
}
template_map;

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

#endif /* HTML_H */

