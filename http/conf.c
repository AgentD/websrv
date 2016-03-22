#include "conf.h"
#include "ini.h"
#include "log.h"

#include <sys/stat.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

static cfg_host* hosts = NULL;
static char* conf_buffer = NULL;
static size_t conf_size = 0;

static cfg_host* get_host_by_name( const char* hostname )
{
    cfg_host* h;
    for( h = hosts; h != NULL; h = h->next )
    {
        if( !strcmp( h->hostname, hostname ) )
            break;
    }
    return h;
}

static char* fix_vpath( char* value )
{
    int len;
    while( *value=='/' ) ++value;
    for( len = strlen(value); len && value[len-1]=='/'; --len ) { }
    value[len] = 0;
    return value;
}

int config_read( const char* filename )
{
    char *key, *value;
    struct stat sb;
    cfg_host* h;
    int fd = -1;

    if( stat( filename, &sb ) != 0 )
        goto fail_open;
    if( (fd = open( filename, O_RDONLY )) < 0 )
        goto fail_open;

    conf_size = sb.st_size;
    conf_buffer = mmap(NULL,conf_size,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,0);
    if( !conf_buffer )
        goto fail_open;

    close( fd );
    if( !ini_compile( conf_buffer, conf_size ) )
        return 0;

    while( (key = ini_next_section( )) )
    {
        if( !strcmp( key, "host" ) )
        {
            if( !(h = calloc(1, sizeof(*h))) )
                goto fail_alloc;

            h->next = hosts;
            hosts = h;

            h->datadir = -1;
            h->tpldir = -1;

            while( ini_next_key( &key, &value ) )
            {
                if( !strcmp( key, "hostname" ) )
                {
                    h->hostname = value;
                }
                else if( !strcmp( key, "restdir" ) )
                {
                    h->restdir = fix_vpath( value );
                }
                else if( !strcmp( key, "datadir" ) )
                {
                    h->datadir = open(value, O_RDONLY|O_DIRECTORY|O_CLOEXEC);
                    if( h->datadir < 0 )
                        goto fail_errno;
                }
                else if( !strcmp( key, "templatedir" ) )
                {
                    h->tpldir = open(value, O_RDONLY|O_DIRECTORY|O_CLOEXEC);
                    if( h->tpldir < 0 )
                        goto fail_errno;
                }
                else if( !strcmp( key, "index" ) )
                {
                    h->index = fix_vpath( value );
                }
            }
        }
    }

    return 1;
fail_open:
    CRITICAL( "%s: %s", filename, strerror(errno) );
    close( fd );
    return 0;
fail_alloc:
    CRITICAL("Out of memory");
    return 0;
fail_errno:
    CRITICAL( "%s: %s", value, strerror(errno) );
    return 0;
}

cfg_host* config_find_host( const char* hostname )
{
    cfg_host* h = hostname ? get_host_by_name( hostname ) : NULL;

    return h ? h : get_host_by_name( "*" );
}

void config_cleanup( void )
{
    cfg_host* h;

    while( hosts != NULL )
    {
        h = hosts;
        hosts = hosts->next;

        close( h->datadir );
        close( h->tpldir );
        free( h );
    }

    if( conf_buffer && conf_size )
        munmap( conf_buffer, conf_size );
}

