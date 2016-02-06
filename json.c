#include "json.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#define TK_STR 1
#define TK_NULL 2

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

size_t json_parse( void* obj, const js_struct* desc,
                   const char* str, size_t size )
{
    const js_struct* subdesc;
    size_t i, slen, *arrsize;
    unsigned char* ptr = obj;
    const char* orig = str;
    void *sub, *memb;
    char* end;

    if( !size || *str!='{' ) goto fail;

    do
    {
        ++str; --size;
        if( !size || *str == '}' ) break;
        if( *(str++) != TK_STR || !(--size) ) goto fail;
        slen = strnlen(str, size);
        if( slen==size || str[slen] ) goto fail;

        for( i=0; i<desc->num_members; ++i )
        {
            if( strlen(desc->members[i].name) > slen )
                continue;
            if( !strcmp(str,desc->members[i].name) )
                break;
        }

        if( i>=desc->num_members )
            goto fail;

        str += slen + 1;
        size -= slen + 1;
        if( !size || *(str++)!=':' ) goto fail;

        memb = ptr + desc->members[i].offset;
        arrsize = (size_t*)(ptr + desc->members[i].sizeoffset);
        subdesc = desc->members[i].desc;
        slen = 0;

        switch( desc->members[i].type )
        {
        case TYPE_OBJ:
            if( *str == TK_NULL ) { slen=1; *((void**)memb)=NULL; break; }
            if( !(sub = calloc(1, subdesc->objsize)) ) goto fail;
            if( !(slen = json_parse(sub,subdesc,str,size)) ){free(sub);break;}
            *((void**)memb) = sub;
            break;
        case TYPE_OBJ_ARRAY:
            slen = json_parse_array( memb, arrsize, subdesc, str, size );
            break;
        case TYPE_INT:
            if( isdigit(str[size-1]) ) goto fail;
            *((int*)memb) = strtol( str, &end, 10 );
            slen = end - str;
            break;
        case TYPE_STRING:
            if( *str == TK_NULL ) { slen=1; *((char**)memb)=NULL; break; }
            if( *str != TK_STR  ) break;
            *((char**)memb) = (char*)str+1;
            slen = strnlen(str+1, size) + 2;
            break;
        }

        if( !slen )
            goto fail;
        str += slen;
        size -= slen;
    }
    while( size && *str == ',' );

    if( size && *str == '}' )
        return str - orig + 1;
fail:
    json_free( obj, desc );
    return 0;
}

size_t json_parse_array( void** out, size_t* count, const js_struct* desc,
                         const char* str, size_t length )
{
    size_t size = 10, used = 0, slen;
    char *arr = calloc( desc->objsize, size ), *new;
    const char* orig = str;

    if( !length || *str!='[' ) goto fail;

    do
    {
        if( used > ((3*size)/4) )
        {
            size *= 2;
            if( !(new = realloc( arr, size * desc->objsize )) ) goto fail;
            arr = new;
            memset(arr + used*desc->objsize, 0, (size - used)*desc->objsize);
        }

        ++str; --length;
        if( !length     ) goto fail;
        if( *str == ']' ) break;

        slen = json_parse( arr + used * desc->objsize, desc, str, length );
        if( !slen )
            goto fail;

        str += slen;
        length -= slen;
        ++used;
    }
    while( length && *str == ',' );

    if( length && *str == ']' )
    {
        *out = arr;
        *count = used;
        return str - orig + 1;
    }
fail:
    json_free_array( arr, used, desc );
    return 0;
}

size_t json_preprocess( char* buffer, size_t size )
{
    char *in = buffer, *out = buffer;
    int is_str = 0;

    for( ; size; --size, ++in )
    {
        if( is_str && *in == '\\' )
        {
            ++in;
            --size;
            if( !size )
                return 0;
            switch( *in )
            {
            case '"':  *(out++) = '"';  continue;
            case '\\': *(out++) = '\\'; continue;
            case '/':  *(out++) = '/';  continue;
            case 'b':  *(out++) = '\b'; continue;
            case 'f':  *(out++) = '\f'; continue;
            case 'n':  *(out++) = '\n'; continue;
            case 'r':  *(out++) = '\r'; continue;
            case 't':  *(out++) = '\t'; continue;
            }
            return 0;
        }

        if( *in == '"' )
        {
            *(out++) = is_str ? 0 : TK_STR;
            is_str = ~is_str;
            continue;
        }

        if( !is_str )
        {
            if( isspace(*in) )
                continue;
            if( size>4 && !strncmp(in,"null",4) && !isalnum(in[4]) )
            {
                *(out++) = TK_NULL;
                in += 3;
                size -= 3;
                continue;
            }
            if( size>4 && !strncmp(in,"true",4) && !isalnum(in[4]) )
            {
                *(out++) = '1';
                in += 3;
                size -= 3;
                continue;
            }
            if( size>5 && !strncmp(in,"false",5) && !isalnum(in[5]) )
            {
                *(out++) = '0';
                in += 4;
                size -= 4;
                continue;
            }
        }

        *(out++) = *in;
    }

    size = out - buffer;
    return size;
}

