#include "json.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

static size_t parse_int( int* value, const char* str )
{
    char* end;

    if( !strncmp( str, "true", 4 ) )
    {
        *value = 1;
        return 4;
    }

    if( !strncmp( str, "false", 5 ) )
    {
        *value = 0;
        return 5;
    }

    *value = strtol( str, &end, 10 );

    if( !end || end==str )
        return 0;

    return end - str;
}

static size_t parse_string( char** out, const char* str )
{
    char* cpy;
    size_t i;

    if( !strncmp( str, "null", 4 ) )
    {
        *out = malloc( 1 );
        if( !*out )
            return 0;
        **out = '\0';
        return 4;
    }

    if( *(str++)!='"' )
        return 0;

    for( i=0; str[i] && str[i]!='"'; ++i )
    {
        if( str[i]=='\\' )
        {
            ++i;
            if( !str[i] )
                return 0;
        }
    }

    if( str[i]!='"' )
        return 0;

    *out = malloc( i+1 );
    if( !(*out) )
        return 0;

    cpy = *out;

    while( *str != '"' )
    {
        if( *str == '\\' )
        {
            ++str;
            switch( *str )
            {
            case '"':  *(cpy++) = '"';  break;
            case '\\': *(cpy++) = '\\'; break;
            case '/':  *(cpy++) = '/';  break;
            case 'b':  *(cpy++) = '\b'; break;
            case 'f':  *(cpy++) = '\f'; break;
            case 'n':  *(cpy++) = '\n'; break;
            case 'r':  *(cpy++) = '\r'; break;
            case 't':  *(cpy++) = '\t'; break;
            default:   *(cpy++) = *str; break;
            }
            ++str;
        }
        else
        {
            *(cpy++) = *(str++);
        }
    }

    *cpy = '\0';
    return i + 2;
}

/****************************************************************************/

void json_free( void* obj, const js_struct* desc )
{
    unsigned char* ptr = obj;
    void* sub;
    size_t i;

    if( !obj )
        return;

    for( i=0; i<desc->num_members; ++i )
    {
        sub = *((void**)(ptr + desc->members[i].offset));

        switch( desc->members[i].type )
        {
        case TYPE_OBJ:
            json_free( sub, desc->members[i].desc );
            break;
        case TYPE_OBJ_ARRAY:
            json_free_array( sub,
                             *((size_t*)(ptr + desc->members[i].sizeoffset)),
                             desc->members[i].desc );
            break;
        case TYPE_STRING:
            break;
        default:
            continue;
        }

        free( sub );
    }
}

void json_free_array( void* arr, size_t count, const js_struct* desc )
{
    size_t i;

    for( i=0; i<count; ++i )
        json_free( (char*)arr + i*desc->objsize, desc );
}

size_t json_parse( void* obj, const js_struct* desc, const char* str )
{
    unsigned char* ptr = obj;
    const char* orig = str;
    size_t i, slen;
    void* sub;

    while( isspace(*str) ) { ++str; }
    if( *str!='{' ) goto fail;

    do
    {
        for( ++str; isspace(*str); ++str ) { }
        if( *str == '}' ) break;
        if( *(str++) != '"' ) goto fail;

        for( i=0; i<desc->num_members; ++i )
        {
            slen = strlen(desc->members[i].name);
            if( strncmp(str,desc->members[i].name,slen)!=0 || str[slen]!='"' )
                continue;

            for( str+=slen+1; isspace(*str); ++str ) { }
            if( *str!=':' ) goto fail;
            for( ++str; isspace(*str); ++str ) { }
            break;
        }

        if( i>=desc->num_members )
            goto fail;

        switch( desc->members[i].type )
        {
        case TYPE_INT:
            slen = parse_int( (int*)(ptr + desc->members[i].offset), str );
            break;
        case TYPE_STRING:
            slen = parse_string((char**)(ptr + desc->members[i].offset), str);
            break;
        case TYPE_OBJ:
            sub = calloc( 1, desc->members[i].desc->objsize );
            slen = json_parse( sub, desc->members[i].desc, str );
            if( !slen )
                free( sub );
            else
                *((void**)(ptr + desc->members[i].offset)) = sub;
            break;
        case TYPE_OBJ_ARRAY:
            slen=json_parse_array( &sub,
                                   (size_t*)(ptr+desc->members[i].sizeoffset),
                                   desc->members[i].desc, str );
            *((void**)(ptr + desc->members[i].offset)) = sub;
            break;
        default:
            slen = 0;
            break;
        }

        if( !slen )
            goto fail;
        for( str += slen; isspace(*str); ++str ) { }
    }
    while( *str == ',' );

    if( *str != '}' )
        goto fail;

    return str - orig + 1;
fail:
    json_free( obj, desc );
    return 0;
}

size_t json_parse_array( void** out, size_t* count, const js_struct* desc,
                         const char* str )
{
    size_t size = 10, used = 0, slen;
    void *arr = calloc( desc->objsize, size ), *new;
    const char* orig = str;

    while( isspace(*str) ) { ++str; }
    if( *str!='[' ) goto fail;

    do
    {
        if( used > size/2 )
        {
            size *= 2;
            new = realloc( arr, size * desc->objsize );
            if( !new )
                goto fail;
            arr = new;
            memset( (char*)arr + used*desc->objsize, 0,
                    (size - used) * desc->objsize );
        }

        for( ++str; isspace(*str); ++str ) { }
        if( *str == ']' ) break;

        slen = json_parse( (char*)arr + used * desc->objsize, desc, str );
        if( !slen )
            goto fail;

        for( str+=slen; isspace(*str); ++str ) { }
        ++used;
    }
    while( *str == ',' );

    if( *str!=']' )
        goto fail;

    *out = arr;
    *count = used;
    return str - orig + 1;
fail:
    json_free_array( arr, used, desc );
    return 0;
}

