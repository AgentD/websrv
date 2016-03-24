#ifndef ERROR_H
#define ERROR_H

#include <stddef.h>

#include "http.h"
#include "str.h"

/*
    Write a default page to an _uninitialized_ string and fill the file info
    structure. If the client accepts compressed data, the string is
    compressed.

    Returns non-zero on sucess.
 */
int gen_error_page(string* page, http_file_info* info,
                   int status, int accept, const char* redirect);

#endif /* ERROR_H */

