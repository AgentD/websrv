#include "conf.h"
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>



JSON_BEGIN( cfg_host )
    JSON_STRING( cfg_host, hostname ),
    JSON_STRING( cfg_host, restdir ),
    JSON_STRING( cfg_host, datadir ),
    JSON_STRING( cfg_host, index )
JSON_END( cfg_host );

JSON_BEGIN( cfg_server )
    JSON_INT( cfg_server, port ),
    JSON_STRING( cfg_server, ipv4 ),
    JSON_STRING( cfg_server, ipv6 ),
    JSON_ARRAY( cfg_server, hosts, cfg_host )
JSON_END( cfg_server );



static cfg_server* servers = NULL;
static size_t num_servers = 0;



static int config_post_process( void )
{
    size_t i, j;
    char* new;

    for( i=0; i<num_servers; ++i )
    {
        for( j=0; j<servers[ i ].num_hosts; ++j )
        {
            new = realpath( servers[ i ].hosts[ j ].datadir, NULL );
            if( !new )
                return 0;
            free( servers[ i ].hosts[ j ].datadir );
            servers[ i ].hosts[ j ].datadir = new;
        }
    }
    return 1;
}

int config_read( const char* filename )
{
    FILE* f = fopen( filename, "r" );
    char* buffer;
    size_t size;

    if( !f )
        return 0;

    if( fseek( f, 0, SEEK_END )!=0 )
        goto fail;
    size = ftell( f );
    if( fseek( f, 0, SEEK_SET )!=0 )
        goto fail;

    buffer = malloc( size+1 );
    if( !buffer )
        goto fail;
    if( fread( buffer, 1, size, f )!=size )
        goto failfree;

    buffer[ size ] = '\0';

    if( !json_parse_array( (void**)&servers, &num_servers,
                           &JSON_DESC(cfg_server), buffer ) )
    {
        goto failfree;
    }

    free( buffer );
    fclose( f );
    return config_post_process( );
failfree:
    free( buffer );
fail:
    fclose( f );
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
    json_free_array( servers, num_servers, &JSON_DESC(cfg_server) );
    free( servers );
}

