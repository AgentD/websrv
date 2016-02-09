#include "conf.h"
#include "json.h"

#include <sys/stat.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>



JSON_BEGIN( cfg_host )
    JSON_STRING( cfg_host, hostname ),
    JSON_STRING( cfg_host, restdir ),
    JSON_STRING( cfg_host, datadir ),
    JSON_STRING( cfg_host, index ),
    JSON_STRING( cfg_host, zip )
JSON_END( cfg_host );

JSON_BEGIN( cfg_server )
    JSON_INT( cfg_server, port ),
    JSON_STRING( cfg_server, ipv4 ),
    JSON_STRING( cfg_server, ipv6 ),
    JSON_STRING( cfg_server, unix ),
    JSON_ARRAY( cfg_server, hosts, cfg_host )
JSON_END( cfg_server );



static cfg_server* servers = NULL;
static size_t num_servers = 0;

static char* conf_buffer;
static size_t conf_size;


static int config_post_process( void )
{
    cfg_host* host;
    size_t i, j;

    for( i=0; i<num_servers; ++i )
    {
        for( j=0; j<servers[ i ].num_hosts; ++j )
        {
            host = servers[ i ].hosts + j;
            host->zipfd = -1;
            if( host->datadir && strlen(host->datadir) )
            {
                host->datadir = realpath( host->datadir, NULL );
                if( !host->datadir )
                    return 0;
            }
            if( host->zip && strlen(host->zip) )
            {
                host->zipfd = open( host->zip, O_RDONLY );
                if( host->zipfd < 0 )
                    return 0;
            }
        }
    }
    return 1;
}

int config_read( const char* filename )
{
    struct stat sb;
    int fd;

    if( stat( filename, &sb )!=0 )
        return 0;
    fd = open( filename, O_RDONLY );
    if( fd < 0 )
        return 0;

    conf_size = sb.st_size;
    conf_buffer = mmap(NULL,conf_size,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,0);
    close( fd );
    if( !conf_buffer )
        goto fail;

    if( !json_deserialize_array( (void**)&servers, &num_servers,
         &JSON_DESC(cfg_server), conf_buffer, conf_size ) )
    {
        goto fail;
    }

    if( config_post_process( ) )
        return 1;
fail:
    munmap( conf_buffer, sb.st_size );
    return 0;
}

cfg_server* config_get_servers( size_t* count )
{
    *count = num_servers;
    return servers;
}

cfg_host* config_find_host( cfg_server* server, const char* hostname )
{
    size_t i;

    if( hostname )
    {
        for( i=0; i<server->num_hosts; ++i )
        {
            if( !strcmp( server->hosts[i].hostname, hostname ) )
                return server->hosts + i;
        }
    }

    for( i=0; i<server->num_hosts; ++i )
    {
        if( !strcmp( server->hosts[i].hostname, "*" ) )
            return server->hosts + i;
    }
    return NULL;
}

void config_cleanup( void )
{
    size_t i, j;

    for( i=0; i<num_servers; ++i )
    {
        for( j=0; j<servers[ i ].num_hosts; ++j )
        {
            close( servers[ i ].hosts[ j ].zipfd );
            free( servers[ i ].hosts[ j ].datadir );
        }
    }

    json_free_array( servers, num_servers, &JSON_DESC(cfg_server) );
    free( servers );
    munmap( conf_buffer, conf_size );
}

cfg_server* config_fork_servers( void )
{
    size_t i;

    for( i=0; i<num_servers; ++i )
    {
        servers[i].pid = fork( );

        if( servers[i].pid==0 )
            return servers + i;

        if( servers[i].pid < 0 )
            perror( "fork" );
    }

    return NULL;
}

void config_kill_all_servers( int sig )
{
    size_t i;

    for( i=0; i<num_servers; ++i )
    {
        if( servers[i].pid > 0 )
            kill( servers[i].pid, sig );
    }
}

