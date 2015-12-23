#include "json.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

static size_t parse_int( int* value, const char* str )
{
    char* end;

    if( !strncmp( str, "true",  4 ) ) { *value = 1; return 4; }
    if( !strncmp( str, "false", 5 ) ) { *value = 0; return 5; }

    *value = strtol( str, &end, 10 );
    return end - str;
}

static size_t parse_string( char** out, const char* str )
{
    char* cpy;
    size_t i;

    if( !strncmp( str, "null", 4 ) ) { *out = NULL; return 4; }
    if( *(str++)!='"' ) return 0;
    for( i=0; str[i] && (str[i]!='"' || (i && str[i-1]=='\\')); ++i ) { }
    if( str[i]!='"' || !(*out = malloc( i+1 )) ) return 0;

    for( cpy = *out; *str != '"'; ++str, ++cpy )
    {
        if( *str == '\\' )
        {
            switch( *(++str) )
            {
            case '"':  *cpy = '"';  continue;
            case '\\': *cpy = '\\'; continue;
            case '/':  *cpy = '/';  continue;
            case 'b':  *cpy = '\b'; continue;
            case 'f':  *cpy = '\f'; continue;
            case 'n':  *cpy = '\n'; continue;
            case 'r':  *cpy = '\r'; continue;
            case 't':  *cpy = '\t'; continue;
            }
        }

        *cpy = *str;
    }

    *cpy = '\0';
    return i + 2;
}

/****************************************************************************/

void json_free( void* obj, const js_struct* desc )
{
    unsigned char* ptr = obj;
    const js_struct* subdesc;
    size_t i, *asize;
    void* sub;

    if( !obj )
        return;

    for( i=0; i<desc->num_members; ++i )
    {
        sub = *((void**)(ptr + desc->members[i].offset));
        asize = (size_t*)(ptr + desc->members[i].sizeoffset);
        subdesc = desc->members[i].desc;

        switch( desc->members[i].type )
        {
        case TYPE_STRING:                                             break;
        case TYPE_OBJ:       json_free( sub, subdesc );               break;
        case TYPE_OBJ_ARRAY: json_free_array( sub, *asize, subdesc ); break;
        default:             continue;
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
    const js_struct* subdesc;
    size_t i, slen, *arrsize;
    unsigned char* ptr = obj;
    const char* orig = str;
    void *sub, *memb;

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
            if( !strncmp(str,desc->members[i].name,slen) && str[slen]=='"' )
                break;
        }

        if( i>=desc->num_members )
            goto fail;

        for( str+=slen+1; isspace(*str); ++str ) { }
        if( *(str++)!=':' ) goto fail;
        while( isspace(*str) ) { ++str; }

        memb = ptr + desc->members[i].offset;
        arrsize = (size_t*)(ptr + desc->members[i].sizeoffset);
        subdesc = desc->members[i].desc;

        switch( desc->members[i].type )
        {
        case TYPE_OBJ:
            if( !(sub = calloc(1, subdesc->objsize)) ) goto fail;
            if( !(slen = json_parse(sub,subdesc,str)) ) { free(sub); break; }
            *((void**)memb) = sub;
            break;
        case TYPE_OBJ_ARRAY:
            slen = json_parse_array( memb, arrsize, subdesc, str );
            break;
        case TYPE_INT:    slen = parse_int( memb, str );    break;
        case TYPE_STRING: slen = parse_string( memb, str ); break;
        default:          slen = 0;                         break;
        }

        if( !slen )
            goto fail;
        for( str += slen; isspace(*str); ++str ) { }
    }
    while( *str == ',' );

    if( *str == '}' )
        return str - orig + 1;
fail:
    json_free( obj, desc );
    return 0;
}

size_t json_parse_array( void** out, size_t* count, const js_struct* desc,
                         const char* str )
{
    size_t size = 10, used = 0, slen;
    char *arr = calloc( desc->objsize, size ), *new;
    const char* orig = str;

    while( isspace(*str) ) { ++str; }
    if( *str!='[' ) goto fail;

    do
    {
        if( used > ((3*size)/4) )
        {
            size *= 2;
            if( !(new = realloc( arr, size * desc->objsize )) ) goto fail;
            arr = new;
            memset(arr + used*desc->objsize, 0, (size - used)*desc->objsize);
        }

        for( ++str; isspace(*str); ++str ) { }
        if( *str == ']' ) break;

        slen = json_parse( arr + used * desc->objsize, desc, str );
        if( !slen )
            goto fail;

        for( str+=slen; isspace(*str); ++str ) { }
        ++used;
    }
    while( *str == ',' );

    if( *str == ']' )
    {
        *out = arr;
        *count = used;
        return str - orig + 1;
    }
fail:
    json_free_array( arr, used, desc );
    return 0;
}

