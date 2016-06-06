#include "conf.h"
#include "ini.h"
#include "log.h"

#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

static cfg_host* hosts = NULL;
static cfg_socket* sockets = NULL;
static char* conf_buffer = NULL;
static size_t conf_size = 0;

static struct
{
    int is_set;
    uid_t uid;
    gid_t gid;
}
user;

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
    char *key, *value, *end;
    struct stat sb;
    cfg_socket* s;
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
                else if( !strcmp( key, "proxydir" ) )
                {
                    h->proxydir = fix_vpath( value );
                }
                else if( !strcmp( key, "proxysock" ) )
                {
                    h->proxysock = value;
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
        else if( !strcmp( key, "user" ) )
        {
            user.is_set = 1;

            while( ini_next_key( &key, &value ) )
            {
                if( !strcmp( key, "uid" ) )
                {
                    user.uid = strtol( value, &end, 10 );
                    if( end == value || (end && *end) )
                        goto fail_num;
                }
                else if( !strcmp( key, "gid" ) )
                {
                    user.gid = strtol( value, &end, 10 );
                    if( end == value || (end && *end) )
                        goto fail_num;
                }
            }
        }
        else if( !strcmp(key,"ipv4") || !strcmp(key,"ipv6") ||
                 !strcmp(key,"unix") )
        {
            if( !(s = calloc( 1, sizeof(*s) )) )
                goto fail_alloc;

            s->next = sockets;
            sockets = s;

            if( !strcmp( key, "unix" ) )
                s->type = AF_UNIX;
            else
                s->type = !strcmp(key, "ipv6") ? AF_INET6 : AF_INET;

            while( ini_next_key( &key, &value ) )
            {
                if( !strcmp( key, "bind" ) )
                {
                    s->bind = value;
                }
                else if( !strcmp( key, "port" ) )
                {
                    s->port = strtol( value, &end, 10 );
                    if( end == value || (end && *end) )
                        goto fail_num;
                    if( s->port < 0 || s->port > 0xFFFF )
                        goto fail_port;
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
fail_num:
    CRITICAL( "%s: %s", key, "Expected integer argument" );
    return 0;
fail_port:
    CRITICAL( "%s: %s", filename, "Port must be in range [0, 65535]" );
    return 0;
}

cfg_host* config_find_host( const char* hostname )
{
    cfg_host* h = hostname ? get_host_by_name( hostname ) : NULL;

    return h ? h : get_host_by_name( "*" );
}

int config_set_user( void )
{
    uid_t uid = geteuid( );
    gid_t gid = getegid( );

    if( user.is_set )
    {
        if( user.gid!=gid && setresgid( user.gid, user.gid, user.gid )!=0 )
        {
            CRITICAL("setresgid: %s", strerror(errno));
            return 0;
        }
        if( user.uid!=uid && setresuid( user.uid, user.uid, user.uid )!=0 )
        {
            CRITICAL("setresuid: %s", strerror(errno));
            return 0;
        }
    }
    return 1;
}

cfg_socket* config_get_sockets( void )
{
    return sockets;
}

void config_cleanup( void )
{
    cfg_socket* s;
    cfg_host* h;

    while( hosts != NULL )
    {
        h = hosts;
        hosts = hosts->next;

        close( h->datadir );
        close( h->tpldir );
        free( h );
    }

    while( sockets != NULL )
    {
        s = sockets;
        sockets = sockets->next;

        free( s );
    }

    if( conf_buffer && conf_size )
        munmap( conf_buffer, conf_size );

    user.is_set = 0;
}

