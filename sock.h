#ifndef SOCK_H
#define SOCK_H

int create_socket( const char* bindaddr, int bindport, int netproto );

int connect_to( const char* addr, int port, int netproto );

int wait_for_fd( int fd, long timeoutms );

#endif /* SOCK_H */

