#include "conf.h"

#include <sys/stat.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>

static cfg_host* hosts = NULL;

static char* conf_buffer;
static size_t conf_size;    /* size of mmapped buffer */
static size_t conf_len;     /* total length of usable data in buffer */
static size_t conf_idx;

static size_t ini_compile( void )
{
    char *in, *out = conf_buffer, *end = conf_buffer + conf_size;
    const char* errstr = NULL;
    int line, str = 0;

    for( in = conf_buffer; in < end; ++in )
    {
        str ^= ((*in) == '"');
        if( str || !isspace(*in) || (*in) == '\n' )
            *(out++) = (*in);
    }

    end = out;
    in = out = conf_buffer;

    for( line = 1; in < end; ++line, ++in )
    {
        if( (*in) == '[' )
        {
            *(out++) = *(in++);
            while( in < end && isalpha(*in) ) *(out++) = *(in++);
            if( out[-1] == '['              ) goto fail_secname;
            if( in >= end || *(in++) != ']' ) goto fail_secend;
            *(out++) = '\0';
        }
        else if( isalpha(*in) )
        {
            while( in < end && isalpha(*in) ) *(out++) = *(in++);
            if( in >= end || *(in++) != '=' ) goto fail_ass;
            if( in >= end || *(in++) != '"' ) goto fail_val;
            *(out++) = '\0';
            while( in < end && *in != '"' && *in != '\n' && *in )
                *(out++) = *(in++);
            if( in >= end || *(in++) != '"' ) goto fail_unmatched;
            *(out++) = '\0';
        }

        if( in < end && *in != '#' && *in != ';' && *in != '\n' )
            goto fail_tk;
        while( in < end && *in != '\n' )
            ++in;
    }
    return (out - conf_buffer);
fail_unmatched: errstr = "unmatched '\"'";                  goto fail;
fail_val:       errstr = "expected value string after '='"; goto fail;
fail_ass:       errstr = "expected '=' after key";          goto fail;
fail_secname:   errstr = "expected section name after '['"; goto fail;
fail_secend:    errstr = "expected ']' after section name"; goto fail;
fail_tk:        errstr = "expected end of line or comment"; goto fail;
fail:           fprintf(stderr, "%d: %s\n", line, errstr);  return 0;
}

static char* ini_next_section( void )
{
    char* ptr = NULL;
    while( !ptr && conf_idx < conf_len )
    {
        if( conf_buffer[conf_idx] == '[' )
            ptr = conf_buffer + conf_idx + 1;
        else
            conf_idx += strlen(conf_buffer + conf_idx) + 1;
        conf_idx += strlen(conf_buffer + conf_idx) + 1;
    }
    return ptr;
}

static int ini_next_key( char** key, char** value )
{
    while( conf_idx >= conf_len || conf_buffer[conf_idx] == '[' )
        return 0;
    *key = conf_buffer + conf_idx;
    conf_idx += strlen(conf_buffer + conf_idx) + 1;
    *value = conf_buffer + conf_idx;
    conf_idx += strlen(conf_buffer + conf_idx) + 1;
    return 1;
}

int config_read( const char* filename )
{
    char *key, *value;
    struct stat sb;
    cfg_host* h;
    int fd, len;

    if( stat( filename, &sb )!=0 )
        return 0;
    fd = open( filename, O_RDONLY );
    if( fd < 0 )
        return 0;

    conf_size = sb.st_size;
    conf_buffer = mmap(NULL,conf_size,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,0);
    close( fd );
    if( !conf_buffer )
        return 0;

    if( !(conf_len = ini_compile( )) )
        goto fail;

    while( (key = ini_next_section( )) )
    {
        if( !strcmp( key, "host" ) )
        {
            h = calloc(1, sizeof(*h));
            h->next = hosts;
            hosts = h;

            h->datadir = -1;
            h->tpldir = -1;
            h->zip = -1;

            while( ini_next_key( &key, &value ) )
            {
                if( !strcmp( key, "hostname" ) )
                {
                    h->hostname = value;
                }
                else if( !strcmp( key, "restdir" ) )
                {
                    while( *value=='/' ) ++value;
                    for(len=strlen(value); len && value[len-1]=='/'; --len) {}
                    value[len] = 0;
                    h->restdir = value;
                }
                else if( !strcmp( key, "datadir" ) )
                {
                    h->datadir = open(value, O_RDONLY|O_DIRECTORY|O_CLOEXEC);
                    if( h->datadir < 0 )
                        goto failopen;
                }
                else if( !strcmp( key, "templatedir" ) )
                {
                    h->tpldir = open(value, O_RDONLY|O_DIRECTORY|O_CLOEXEC);
                    if( h->tpldir < 0 )
                        goto failopen;
                }
                else if( !strcmp( key, "index" ) )
                {
                    while( *value=='/' ) ++value;
                    for(len=strlen(value); len && value[len-1]=='/'; --len) {}
                    value[len] = 0;
                    h->index = value;
                }
                else if( !strcmp( key, "zip" ) )
                {
                    h->zip = open(value, O_RDONLY|O_CLOEXEC);
                    if( h->zip < 0 )
                        goto failopen;
                }
            }
        }
    }

    return 1;
failopen:
    perror(value);
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

        close( h->datadir );
        close( h->zip );

        free( h );
    }

    munmap( conf_buffer, conf_size );
}

