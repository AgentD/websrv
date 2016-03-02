#ifndef FILE_H
#define FILE_H

/*
    Try to send a file
      dirfd: A file descriptor for a directory containing the file
      method: The HTTP request method (see http.h)
      fd: The socket to send the heaer+data to
      ifmod: A timestamp to check against. Send response 304 if file is older.
      filename: The requested path relative to the data directory.

    Returns 0 on success or an error code (ERR_*) on failure.
 */
int http_send_file( int dirfd, int method, int fd, long ifmod,
                    const char* filename );

/*
    Try to send a file from a ZIP archive.
      zip: A file descriptor for a ZIP archive
      method: The HTTP request method (see http.h)
      fd: The socket to send the heaer+data to
      ifmod: A timestamp to check against. Send response 304 if file is older.
      path: The requested path
      accept: Accept flags from the HTTP request

    Returns 0 on success or an error code (ERR_*) on failure.
 */
int send_zip( int zip, int method, int fd, long ifmod,
              const char* path, int accept );

#endif /* FILE_H */

