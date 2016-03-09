#ifndef FILE_H
#define FILE_H

#include "http.h"

/*
    Try to send a file
      dirfd: A file descriptor for a directory containing the file
      fd: The socket to send the header + data to
      req: The HTTP request received from the client

    Returns 0 on success or an error code (ERR_*) on failure.
 */
int http_send_file( int dirfd, int fd, const http_request* req );

#endif /* FILE_H */

