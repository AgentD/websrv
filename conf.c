#include "conf.h"

#include <sys/stat.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>

static cfg_host* hosts = NULL;

static char* conf_buffer;
static size_t conf_size;
static size_t conf_idx;

static size_t ini_find_char( size_t start, const char* delim )
{
    size_t i;

    for( i=start; i<conf_size; ++i )
    {
        if( strchr( delim, conf_buffer[i] ) )
            break;
        if( conf_buffer[i]=='#' || conf_buffer[i]==';' )
        {
            for( ; conf_buffer[i]!='\n' && i<conf_size; ++i ) { }
        }
    }

    return i;
}

static const char* ini_next_section( void )
{
    size_t i = ini_find_char( conf_idx, "["      );
    size_t j = ini_find_char( i+1,      "]\n#;[" );

    if( i>=conf_size || j>=conf_size )
        return NULL;

    if( conf_buffer[i]!='[' && conf_buffer[j]!=']' )
        return NULL;

    conf_idx = j + 1;

    for( ++i; i<j && isspace(conf_buffer[i]); ++i ) { }
    for( --j; j>i && isspace(conf_buffer[j]); --j ) { }

    if( j<=i )
        return NULL;

    conf_buffer[j + 1] = 0;
    return conf_buffer + i;
}

static int ini_next_key( const char** key, const char** value )
{
    size_t i, j;

    i = ini_find_char( conf_idx, "=[" );
    if( i>=conf_size || conf_buffer[i]!='=' )
        return 0;

    conf_idx = i + 1;

    /* isolate key */
    while( i && conf_buffer[i]!='\n' ) { --i; }
    while( isspace(conf_buffer[i])   ) { ++i; }

    *key = conf_buffer + i;
    i = conf_idx - 2;

    while( i && isspace(conf_buffer[ i ]) ) { --i; }
    conf_buffer[i+1] = 0;

    /* isolate value */
    i = conf_idx;

    while( i<conf_size && isspace(conf_buffer[i]) && conf_buffer[i]!='\n' )
        ++i;

    if( i>=conf_size || strchr("\n#;", conf_buffer[i]) )
        return 0;

    if( conf_buffer[i]=='"' )
    {
        ++i;
        j = ini_find_char( i, "\"\n#;" );
        if( j>=conf_size || conf_buffer[j]!='"' )
            return 0;
    }
    else
    {
        j = ini_find_char( i, "\n#;" );
        if( j>=conf_size )
            return 0;
        for( --j; j>i && isspace(conf_buffer[j]); --j ) { }
        ++j;
    }

    *value = conf_buffer + i;
    conf_buffer[j] = 0;
    conf_idx = j+1;
    return j > i;
}

int config_read( const char* filename )
{
    const char *key, *value;
    struct stat sb;
    cfg_host* h;
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

    while( (key = ini_next_section( )) )
    {
        if( !strcmp( key, "host" ) )
        {
            h = calloc(1, sizeof(*h));
            h->next = hosts;
            hosts = h;

            while( ini_next_key( &key, &value ) )
            {
                if( !strcmp( key, "hostname" ) )
                    h->hostname = value;
                else if( !strcmp( key, "restdir" ) )
                    h->restdir = value;
                else if( !strcmp( key, "datadir" ) )
                    h->datadir = realpath(value, NULL);
                else if( !strcmp( key, "index" ) )
                    h->index = value;
                else if( !strcmp( key, "zip" ) )
                    h->zip = realpath(value, NULL);
            }
        }
    }

    for( h = hosts; h != NULL; h = h->next )
    {
        if( h->datadir && !h->datadir[0] )
        {
            free(h->datadir);
            h->datadir = NULL;
        }
        if( h->zip && !h->zip[0] )
        {
            free(h->zip);
            h->zip = NULL;
        }
    }
    return 1;
fail:
    munmap( conf_buffer, sb.st_size );
    return 0;
}

cfg_host* config_find_host( const char* hostname )
{
    cfg_host* h;

    if( hostname )
    {
        for( h = hosts; h != NULL; h = h->next )
        {
            if( !strcmp( h->hostname, hostname ) )
                return h;
        }
    }

    for( h = hosts; h != NULL; h = h->next )
    {
        if( !strcmp( h->hostname, "*" ) )
            return h;
    }
    return NULL;
}

void config_cleanup( void )
{
    cfg_host* h;

    while( hosts != NULL )
    {
        h = hosts;
        hosts = hosts->next;

        free( h->datadir );
        free( h->zip );
        free( h );
    }

    munmap( conf_buffer, conf_size );
}

