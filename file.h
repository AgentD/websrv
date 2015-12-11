#ifndef FILE_H
#define FILE_H

void http_send_file( int method, int fd, unsigned long ifmod,
                     const char* filename, const char* basedir );

#endif /* FILE_H */

