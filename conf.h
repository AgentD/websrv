#ifndef CONF_H
#define CONF_H

typedef struct cfg_host
{
    struct cfg_host* next;

    const char* hostname;   /* hostname to map the directory to */
    const char* restdir;    /* optional directory to map rest API to */
    int datadir;            /* optional static file base directory handle */
    int tpldir;             /* optional directory for template files */
    const char* index;      /* path to serve when root is requested */
    int zip;                /* optional zip overlay file descriptor */
}
cfg_host;

/* read global config from file, return 0 on failure, non-zero on success */
int config_read( const char* filename );

/* get the host config for a certain host name */
cfg_host* config_find_host( const char* hostname );

/* free all memory of the internal config */
void config_cleanup( void );

#endif /* CONF_H */

