#ifndef CONF_H
#define CONF_H

#include <netinet/in.h>

#define SERVER_IPV4 0x01
#define SERVER_IPV6 0x02

typedef struct cfg_server
{
    unsigned int port;          /* port number to bind, network byte order */
    int flags;                  /* set of SERVER_* flags */
    unsigned int bindv4;        /* IPv4 address to bind to if configured */
    struct in6_addr bindv6;     /* IPv6 address to bind to if configured */
    struct cfg_server* next;
}
cfg_server;

typedef struct cfg_host
{
    char* datadir;              /* base directory for static files */
    char* hostname;             /* hostname to map the directory to */
    struct cfg_host* next;
}
cfg_host;


/* read global config from file, return 0 on failure, non-zero on success */
int config_read( const char* filename );

/* get the linked list of bind configs */
cfg_server* config_get_servers( void );

/* get the host config for a certain host name */
cfg_host* config_find_host( const char* hostname );

/* free all memory of the internal config */
void config_cleanup( void );

#endif /* CONF_H */

