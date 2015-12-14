#ifndef SOCK_H
#define SOCK_H

#include <sys/types.h>

int create_socket( const char* bindaddr, int bindport, int netproto );

int connect_to( const char* addr, int port, int netproto );

int wait_for_fd( int fd, long timeoutms );

void splice_to_sock( int* pfd, int filefd, int sockfd,
                     size_t filesize, size_t pipedata, loff_t offset );

#endif /* SOCK_H */

