#ifndef ERROR_H
#define ERROR_H

#include <stddef.h>

#define ERR_BAD_REQ 1
#define ERR_NOT_FOUND 2
#define ERR_METHOD 3
#define ERR_FORBIDDEN 4
#define ERR_TYPE 5
#define ERR_SIZE 6
#define ERR_INTERNAL 7
#define ERR_TIMEOUT 8

/* Write HTTP header + error page. Returns number of bytes written. */
size_t gen_error_page( int fd, int errorid, int accept );

#endif /* ERROR_H */

