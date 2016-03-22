#include <string.h>
#include <ctype.h>
#include <stdio.h>

#include "ini.h"
#include "log.h"

static char* conf_buffer = NULL;
static size_t conf_size = 0;    /* size of buffer */
static size_t conf_len = 0;     /* total length of usable data in buffer */
static size_t conf_idx = 0;

int ini_compile( char* buffer, size_t size )
{
    char *in, *out = buffer, *end = buffer + size;
    const char* errstr = NULL;
    int line, str = 0;

    conf_buffer = buffer;
    conf_size = size;
    conf_len = 0;
    conf_idx = 0;

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
    conf_len = (out - conf_buffer);
    return 1;
fail_unmatched: errstr = "unmatched '\"'";                  goto fail;
fail_val:       errstr = "expected value string after '='"; goto fail;
fail_ass:       errstr = "expected '=' after key";          goto fail;
fail_secname:   errstr = "expected section name after '['"; goto fail;
fail_secend:    errstr = "expected ']' after section name"; goto fail;
fail_tk:        errstr = "expected end of line or comment"; goto fail;
fail:           CRITICAL("config: %d: %s", line, errstr);   return 0;
}

char* ini_next_section( void )
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

int ini_next_key( char** key, char** value )
{
    while( conf_idx >= conf_len || conf_buffer[conf_idx] == '[' )
        return 0;
    *key = conf_buffer + conf_idx;
    conf_idx += strlen(conf_buffer + conf_idx) + 1;
    *value = conf_buffer + conf_idx;
    conf_idx += strlen(conf_buffer + conf_idx) + 1;
    return 1;
}

