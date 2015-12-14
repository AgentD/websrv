#ifndef FILE_H
#define FILE_H

/*
    Try to send a file
      method: The HTTP request method (see http.h)
      fd: The socket to send the heaer+data to
      ifmod: A timestamp to check against. Send response 304 if file is older.
      filename: The requested path
      basedir: The data root directory
 */
void http_send_file( int method, int fd, unsigned long ifmod,
                     const char* filename, const char* basedir );

/*
    Try to send a file from a ZIP archive.
      method: The HTTP request method (see http.h)
      fd: The socket to send the heaer+data to
      ifmod: A timestamp to check against. Send response 304 if file is older.
      path: The requested path
      zipfile: Filedescriptor of the ZIP archive

    Returns 0 if the file is not in the archive (-> no data or header sent).
 */
int send_zip( int method, int fd, unsigned long ifmod,
              const char* path, int zipfile );

#endif /* FILE_H */

