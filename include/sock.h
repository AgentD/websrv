#ifndef SOCK_H
#define SOCK_H

#include <sys/types.h>

typedef struct
{
    char buffer[128];   /* buffer for buffered reads */
    int offset;         /* current read position in buffer */
    int size;           /* number of bytes in buffer */
    int fd;             /* wrapped file discriptor */
}
sock_t;

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
 */
void splice_to_sock( int* pfd, int filefd, int sockfd,
                     size_t filesize, size_t pipedata );

/* create a buffered read wrapper for a file descriptor */
sock_t* create_wrapper( int fd );

/* destroy a buffered read wrapper and close the underlying FD */
void destroy_wrapper( sock_t* sock );

/*
    Wait for a socket wrapper to become readable. Returns instantly if
    it already holds data in the read buffer, waits if not and prefetches
    a buffer full of data.

    Returns positive on success, zero if end of file was reached or the remote
    site hung up. Negative if a timeout occoured or an error happened while
    reading.

    On error, errno is set apropriately (EWOULDBLOCK for timeout).
 */
int sock_wait( sock_t* sock, long timeout );

/*
    Read a block of data from a socket wrapper.

    Returns the number of bytes read on success. Zero or less than the
    specified count means the end of file was reached or the remote
    site hung up. A negative value indiciates a timeout or an error while
    reading.

    On error, errno is set apropriately (EWOULDBLOCK for timeout).
 */
ssize_t sock_read( sock_t* sock, void* buffer, size_t size, long timeoutms );

/*
    Read a line from a socket (terminated by '\n'). Automatically does the
    following input transformations:
      - replace space characters other than '\n' with a normal whitespace
      - replace backslashes with forward slashes
      - replace sequences of multiple whitespaces with a single whitespace
      - replace sequences of slashes with a single slash
      - trim whitespace at the beginning
      - trim whitespace at the end

    Returns non-zero on success, zero if connection terminated or out of
    buffer space, negative if a timeout occoured.
 */
int read_line( sock_t* sock, char* buffer, size_t size, long timeout );

#endif /* SOCK_H */

