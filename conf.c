#include "conf.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include <arpa/inet.h>

#define BLK_NONE 0
#define BLK_SERVER 1
#define BLK_HOST 2

static cfg_server* servers = NULL;
static cfg_host* hosts = NULL;

static int is_empty( const char* line )
{
    for( ; *line && *line!='#'; ++line )
    {
        if( !isspace( *line ) )
            return 0;
    }
    return 1;
}

static int skip_line( FILE* f )
{
    int c;

    do
    {
        c = fgetc(f);
        if( c=='#' )
        {
            while( c>0 && c!='\n' )
                c = fgetc(f);
        }
    }
    while( c>0 && isspace(c) && c!='\n' );

    return c=='\n';
}

static int find_open( FILE* f, const char* line, size_t* l )
{
    int c;

    for( ; *line && *line!='#'; ++line )
    {
        if( *line == '{' )
            return is_empty( line+1 );
        if( !isspace(*line) )
            return 0;
    }
    ++(*l);
    do
    {
        c = fgetc(f);
        if( c=='\n' )
            ++(*l);
        if( c=='{' )
            return skip_line( f );
    }
    while( c>0 && isspace(c) );
    return 0;
}

static void print_err_line( const char* line, size_t offset )
{
    size_t i;

    for( i=0; i<(offset+3) && line[i] && line[i]!='\n'; ++i )
        fputc( line[i], stderr );
    fputc( '\n', stderr );

    for( i=0; i<offset; ++i )
        fputc( ' ', stderr );

    fputc( '^', stderr );
    fputc( '\n', stderr );
}

static int handle_server_arg( char* buffer, cfg_server* server,
                              const char* filename, size_t line )
{
    const char* err = "unknown server argument";
    struct in6_addr addr6 = in6addr_any;
    struct in_addr addr4;
    size_t j, i=0;

    addr4.s_addr = INADDR_ANY;

    if( !strncmp( buffer, "port", 4 ) && isspace(buffer[4]) )
    {
        for( i=4; isspace(buffer[i]); ++i ) { }
        if( !isdigit(buffer[i]) )
        {
            err = "expected numeric argument for 'port'";
            goto fail;
        }

        server->port = htons( strtol( buffer+i, NULL, 10 ) & 0xFFFF );
    }
    else if( !strncmp( buffer, "ipv4", 4 ) && isspace(buffer[4]) )
    {
        for( i=4; isspace(buffer[i]); ++i ) { }
        if( !is_empty(buffer+i) && !(buffer[i]=='*' && is_empty(buffer+i+1)) )
        {
            for( j=i; buffer[j] && !isspace(buffer[j]); ++j ) { }
            buffer[j] = '\0';

            if( inet_pton( AF_INET, buffer+i, &addr4 )<=0 )
            {
                err = "expected IP address or '*'";
                goto fail;
            }
        }
        server->bindv4 = addr4.s_addr;
        server->flags |= SERVER_IPV4;
    }
    else if( !strncmp( buffer, "ipv6", 4 ) && isspace(buffer[4]) )
    {
        for( i=4; isspace(buffer[i]); ++i ) { }
        if( !is_empty( buffer+i ) && !(buffer[i]=='*'&&is_empty(buffer+i+1)) )
        {
            for( j=i; buffer[j] && !isspace(buffer[j]); ++j ) { }
            buffer[j] = '\0';

            if( inet_pton( AF_INET6, buffer+i, &addr6 )<=0 )
            {
                err = "expected IP address or '*'";
                goto fail;
            }
        }
        server->flags |= SERVER_IPV6;
        server->bindv6 = addr6;
    }
    else
    {
        goto fail;
    }
    return 1;
fail:
    if( i )
        print_err_line( buffer, i );
    fprintf( stderr, "%s: %s on line %lu\n",
             filename, err, (unsigned long)line );
    return 0;
}

static int isolate_string( char* buffer, size_t* j, size_t* i )
{
    for( ; isspace(buffer[*j]) && buffer[*j]!='"'; ++(*j) ) { }
    if( buffer[*j]!='"' )
        return 0;

    for( *i=*j+1; buffer[*i] && buffer[*i]!='"'; ++(*i) ) { }
    if( buffer[*i]!='"' )
    {
        *j = *i;
        return 0;
    }
    if( ((*j)-(*i)) < 2 )
        return 0;
    ++(*j);
    return 1;
}

static int handle_host_arg( char* buffer, cfg_host* host,
                            const char* filename, size_t line )
{
    const char* err = "unknown host argument";
    size_t j, i;

    if( !strncmp( buffer, "datadir", 7 ) && isspace(buffer[7]) )
    {
        j = 7;
        if( !isolate_string( buffer, &j, &i ) )
            goto errstr;

        host->datadir = calloc( 1, buffer[i-1]=='/' ? (i-j+1) : (i-j+2) );
        memcpy( host->datadir, buffer+j, i-j );
        if( buffer[i-1]!='/' )
            host->datadir[i-j] = '/';
    }
    else if( !strncmp( buffer, "hostname", 8 ) && isspace(buffer[8]) )
    {
        j = 8;
        if( !isolate_string( buffer, &j, &i ) )
            goto errstr;
        host->hostname = strndup( buffer+j, i-j );
    }
    else if( !strncmp( buffer, "index", 5 ) && isspace(buffer[5]) )
    {
        j = 5;
        if( !isolate_string( buffer, &j, &i ) )
            goto errstr;
        host->indexfile = strndup( buffer+j, i-j );
    }
    else
    {
        goto err;
    }
    return 1;
errstr:
    print_err_line( buffer, j );
    err = "expected \"<string>\"";
err:
    fprintf(stderr,"%s: %s on line %lu\n",filename,err,(unsigned long)line);
    return 0;
}

int config_read( const char* filename )
{
    int blktype = BLK_NONE, rc=1;
    cfg_server* server = NULL;
    const char* err = NULL;
    cfg_host* host = NULL;
    size_t i, j, line, l2;
    char buffer[ 512 ];
    FILE* f;

    f = fopen( filename, "r" );

    if( !f )
    {
        fprintf( stderr, "%s: cannot open file\n", filename );
        return 0;
    }

    for( line=1; fgets( buffer, sizeof(buffer), f ); ++line )
    {
        for( i=0; isspace(buffer[i]); ++i ) { }

        if( !buffer[i] || buffer[i]=='#' )
            continue;

        if( buffer[i]=='}' && !is_empty( buffer+i+1 ) )
        {
            err = "unexpected data after '}'";
            goto fail;
        }

        if( !isalpha( buffer[i] ) && buffer[i]!='}' )
        {
            print_err_line( buffer+(i>=3 ? i-3 : 0), i>=3 ? 3 : i );
            err = "unexpected character";
            goto fail;
        }

        for( j=i; isalpha(buffer[j]); ++j ) { }

        switch( blktype )
        {
        case BLK_NONE:
            if( buffer[j]!='{' && !isspace(buffer[j]) )
            {
                print_err_line( buffer+i, j-i );
                err = "unexpected character in block type";
                goto fail;
            }

            if( (j-i)==6 && !strncmp( buffer+i, "server", 6 ) )
            {
                server = calloc( 1, sizeof(*server) );
                blktype = BLK_SERVER;
            }
            else if( (j-i)==4 && !strncmp( buffer+i, "host", 4 ) )
            {
                host = calloc( 1, sizeof(*host) );
                blktype = BLK_HOST;
            }
            else
            {
                err = "unknown block type";
                goto fail;
            }

            l2 = line;
            if( !find_open(f, buffer+j, &l2) )
            {
                err = "cannot find '{' after";
                goto fail;
            }
            line = l2;
            break;
        case BLK_SERVER:
            if( buffer[i]=='}' )
            {
                server->next = servers;
                servers = server;
                server = NULL;
                blktype = BLK_NONE;
            }
            else if( !handle_server_arg( buffer+i, server, filename, line ) )
            {
                goto fail;
            }
            break;
        case BLK_HOST:
            if( buffer[i]=='}' )
            {
                host->next = hosts;
                hosts = host;
                host = NULL;
                blktype = BLK_NONE;
            }
            else if( !handle_host_arg( buffer+i, host, filename, line ) )
            {
                goto fail;
            }
            break;
        }
    }

    if( blktype!=BLK_NONE )
    {
        err = "cannot find '}' for final block";
        goto fail;
    }

out:
    free( server );
    free( host );
    fclose( f );
    return rc;
fail:
    if( err )
    {
        fprintf( stderr, "%s: %s on line %lu\n",
                 filename, err, (unsigned long)line );
    }
    rc = 0;
    goto out;
}

cfg_server* config_get_servers( void )
{
    return servers;
}

cfg_host* config_find_host( const char* hostname )
{
    cfg_host* h = NULL;

    if( hostname )
    {
        for( h=hosts; h!=NULL; h=h->next )
        {
            if( !strcmp( h->hostname, hostname ) )
                return h;
        }
    }

    for( h=hosts; h!=NULL; h=h->next )
    {
        if( !strcmp( h->hostname, "*" ) )
            break;
    }
    return h;
}

void config_cleanup( void )
{
    cfg_server *s=servers, *olds;
    cfg_host *h=hosts, *oldh;

    while( s )
    {
        olds = s;
        s = s->next;
        free( olds );
    }

    while( h )
    {
        free( h->datadir );
        free( h->hostname );
        free( h->indexfile );
        oldh = h;
        h = h->next;
        free( oldh );
    }

    hosts = NULL;
    servers = NULL;
}

