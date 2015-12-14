#ifndef FILE_H
#define FILE_H

void http_send_file( int method, int fd, unsigned long ifmod,
                     const char* filename, const char* basedir );

int send_zip( int method, int fd, unsigned long ifmod,
              const char* path, int zipfile );

#endif /* FILE_H */

