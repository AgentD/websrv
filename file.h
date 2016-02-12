#ifndef FILE_H
#define FILE_H

/*
    Try to send a file
      method: The HTTP request method (see http.h)
      fd: The socket to send the heaer+data to
      ifmod: A timestamp to check against. Send response 304 if file is older.
      filename: The requested path relative to the data directory.

    Returns 0 on success or an error code (ERR_*) on failure.
 */
int http_send_file( int method, int fd, unsigned long ifmod,
                    const char* filename );

/*
    Try to send a file from a ZIP archive.
      method: The HTTP request method (see http.h)
      fd: The socket to send the heaer+data to
      ifmod: A timestamp to check against. Send response 304 if file is older.
      path: The requested path
      zip: Path of the the ZIP archive

    Returns 0 on success or an error code (ERR_*) on failure.
 */
int send_zip( int method, int fd, unsigned long ifmod,
              const char* path, const char* zip );

#endif /* FILE_H */

