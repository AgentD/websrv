#ifndef ERROR_H
#define ERROR_H

#include <stddef.h>

/* Write HTTP header + error page. Returns number of bytes written. */
size_t gen_error_page( int fd, int errorid, int accept );

#endif /* ERROR_H */

