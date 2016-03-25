#ifndef CONF_H
#define CONF_H

typedef struct cfg_host
{
    struct cfg_host* next;

    const char* hostname;   /* hostname to map the directory to */
    const char* restdir;    /* optional directory to map rest API to */
    const char* proxydir;   /* optional directory that forwards requests */
    const char* proxysock;  /* optional unix socket to forward requests to */
    int datadir;            /* optional static file base directory handle */
    int tpldir;             /* optional directory for template files */
    const char* index;      /* path to serve when root is requested */
}
cfg_host;

/* read global config from file, return 0 on failure, non-zero on success */
int config_read( const char* filename );

/* get the host config for a certain host name */
cfg_host* config_find_host( const char* hostname );

/* chroot and drop priviledges */
int config_set_user( void );

/* free all memory of the internal config */
void config_cleanup( void );

#endif /* CONF_H */

