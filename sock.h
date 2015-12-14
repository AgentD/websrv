#ifndef SOCK_H
#define SOCK_H

#include <sys/types.h>

/* Create a server socket, bind it to an address and start listening. */
int create_socket( const char* bindaddr, int bindport, int netproto );

/* Create a socket and connect to a remote host. ADDR IS NOT RESOLVED! */
int connect_to( const char* addr, int port, int netproto );

/* Wait until data is ready or a timeout occours. Returns 0 if timeout */
int wait_for_fd( int fd, long timeoutms );

/*
    Copy data from a file to a socket via a pipe.
      pfd: pipe fds 0 -> read end, 1 -> write end
      filefd: fd to read from
      sockfd: fd to write to
      filesize: the number of bytes to transfer
      pipedata: bytes already in the pipe (e.g. http header)
      offset: offset from the start of filefd
 */
void splice_to_sock( int* pfd, int filefd, int sockfd,
                     size_t filesize, size_t pipedata, loff_t offset );

#endif /* SOCK_H */

