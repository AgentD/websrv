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



static cfg_host* hosts = NULL;
static size_t num_hosts = 0;

static char* conf_buffer;
static size_t conf_size;


static int config_post_process( void )
{
    size_t i;

    for( i = 0; i < num_hosts; ++i )
    {
        if( hosts[i].datadir && !hosts[i].datadir[0] )
            hosts[i].datadir = NULL;
        if( hosts[i].zip && !hosts[i].zip[0] )
            hosts[i].zip = NULL;

        if( hosts[i].datadir )
        {
            hosts[i].datadir = realpath( hosts[i].datadir, NULL );
            if( !hosts[i].datadir )
                return 0;
        }
        if( hosts[i].zip )
        {
            hosts[i].zip = realpath( hosts[i].zip, NULL );
            if( !hosts[i].zip )
                return 0;
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

    if( !json_deserialize_array( (void**)&hosts, &num_hosts,
         &JSON_DESC(cfg_host), conf_buffer, conf_size ) )
    {
        goto fail;
    }

    if( config_post_process( ) )
        return 1;
fail:
    munmap( conf_buffer, sb.st_size );
    return 0;
}

cfg_host* config_find_host( const char* hostname )
{
    size_t i;

    if( hostname )
    {
        for( i=0; i<num_hosts; ++i )
        {
            if( !strcmp( hosts[i].hostname, hostname ) )
                return hosts + i;
        }
    }

    for( i=0; i<num_hosts; ++i )
    {
        if( !strcmp( hosts[i].hostname, "*" ) )
            return hosts + i;
    }
    return NULL;
}

void config_cleanup( void )
{
    size_t i;

    for( i = 0; i < num_hosts; ++i )
    {
        free( hosts[ i ].datadir );
        free( hosts[ i ].zip );
    }

    munmap( conf_buffer, conf_size );
}

