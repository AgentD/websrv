#ifndef CONF_H
#define CONF_H

#include <stddef.h>

typedef struct
{
    char* hostname;         /* hostname to map the directory to */
    char* restdir;          /* directory to map rest API to */
    char* datadir;          /* base directory for static files */
    char* index;            /* path to serve when root is requested */
}
cfg_host;

typedef struct
{
    int port;               /* port number to bind, network byte order */
    char* ipv4;             /* IPv4 address to bind to if configured */
    char* ipv6;             /* IPv6 address to bind to if configured */
    char* unix;             /* UNIX local socket to bind to if configured */
    cfg_host* hosts;        /* array of hosts for this server */
    size_t num_hosts;       /* number of configured hosts */

    int fd4;                /* TCP/IPv4 server socket fd */
    int fd6;                /* TCP/IPv6 server socket fd */
    int fdu;                /* unix local socekt fd */
}
cfg_server;

/* read global config from file, return 0 on failure, non-zero on success */
int config_read( const char* filename );

/* get array of servers */
cfg_server* config_get_servers( size_t* count );

/* get the host config for a certain host name */
cfg_host* config_find_host( cfg_server* server, const char* hostname );

/* free all memory of the internal config */
void config_cleanup( void );

#endif /* CONF_H */

