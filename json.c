#include "json.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#define TK_STR 1
#define TK_NULL 2

typedef struct
{
    char* in;
    char* out;
    size_t size;
}
mem_stream;

static void mem_stream_init( mem_stream* str, char* buffer, size_t size )
{
    str->in = str->out = buffer;
    str->size = size;
}

static int mem_stream_getc( mem_stream* str )
{
    return str->size ? *(str->in++) : -1;
}

static int mem_stream_peek( mem_stream* str )
{
    return str->size ? *str->in : -1;
}

static void mem_stream_putc( mem_stream* str, int c )
{
    *(str->out++) = c;
}

static int mem_stream_copy_string( mem_stream* str )
{
    size_t len = strnlen( str->in, str->size );

    len = (len==str->size) ? len : (len+1);

    memmove( str->out, str->in, len );
    str->in += len;
    str->out += len;
    str->size -= len;
    return str->size!=0;
}

static int mem_stream_copy( mem_stream* str )
{
    int c;
    if( !str->size )
        return -1;
    c = *(str->in++);
    *(str->out++) = c;
    return c;
}

static int mem_stream_tryread( mem_stream* str, const char* word, size_t len )
{
    if( len > str->size )
        return 0;
    if( strncmp(str->in, word, len) )
        return 0;
    str->in += len;
    str->size -= len;
    return 1;
}

static int mem_stream_read_int( mem_stream* str, int* i )
{
    int s = 1;

    *i = 0;

    if( !str->size )
        return 0;

    if( str->in[0]=='-' )
    {
        ++str->in;
        --str->size;
        if( !str->size || !isdigit(*str->in) )
            return 0;
        s = -1;
    }

    if( !isdigit(*str->in) )
        return 0;
    while( str->size && isdigit(*str->in) )
    {
        *i = (*i) * 10 +  (*(str->in++) - '0');
        --str->size;
    }
    *i *= s;
    return 1;
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
                   const char* data, size_t size )
{
    const js_struct* subdesc;
    size_t i, slen, *arrsize;
    unsigned char* ptr = obj;
    void *sub, *memb;
    mem_stream str;
    int c;

    mem_stream_init( &str, (char*)data, size );
    if( mem_stream_getc(&str)!='{' ) goto fail;

    while( 1 )
    {
        c = mem_stream_peek( &str );
        if( c < 0  ) goto fail;
        if( c=='}' ) break;

        for( i=0; i<desc->num_members; ++i )
        {
            slen = strlen(desc->members[i].name) + 1;
            if( mem_stream_tryread( &str, desc->members[i].name, slen ) )
                break;
        }

        if( !str.size || i>=desc->num_members )
            goto fail;

        memb = ptr + desc->members[i].offset;
        arrsize = (size_t*)(ptr + desc->members[i].sizeoffset);
        subdesc = desc->members[i].desc;

        switch( desc->members[i].type )
        {
        case TYPE_OBJ:
            if( *str.in==TK_NULL )
            {
                str.in+=1;
                str.size-=1;
                *((void**)memb)=NULL;
                break;
            }
            if( !(sub = calloc(1, subdesc->objsize)) ) goto fail;
            slen = json_parse(sub,subdesc,str.in,str.size);
            if( !slen ) { free(sub); break; }
            *((void**)memb) = sub;
            str.in += slen;
            str.size -= slen;
            break;
        case TYPE_OBJ_ARRAY:
            slen = json_parse_array(memb, arrsize, subdesc, str.in, str.size);
            if( !slen ) goto fail;
            str.in += slen;
            str.size -= slen;
            break;
        case TYPE_INT:
            if( !mem_stream_read_int( &str, (int*)memb ) )
                goto fail;
            break;
        case TYPE_STRING:
            c = mem_stream_getc( &str );
            if( c == TK_NULL ) { *((char**)memb)=NULL; break; }
            if( c != TK_STR  ) goto fail;
            *((char**)memb) = str.in;
            slen = strnlen(str.in, str.size);
            if( slen==str.size ) goto fail;
            str.in += slen+1;
            str.size -= slen+1;
            break;
        default:
            goto fail;
        }
    }

    return str.in - data + 1;
fail:
    json_free( obj, desc );
    return 0;
}

size_t json_parse_array( void** out, size_t* count, const js_struct* desc,
                         const char* data, size_t length )
{
    size_t size = 10, used = 0, slen;
    char *arr = calloc( desc->objsize, size ), *new;
    mem_stream str;
    int c;

    mem_stream_init( &str, (char*)data, length );
    if( mem_stream_getc(&str)!='[' ) goto fail;

    while( 1 )
    {
        c = mem_stream_peek( &str );
        if( c==']' ) break;
        if( c!='{' ) goto fail;

        if( used > ((3*size)/4) )
        {
            size *= 2;
            if( !(new = realloc( arr, size * desc->objsize )) ) goto fail;
            arr = new;
            memset(arr + used*desc->objsize, 0, (size - used)*desc->objsize);
        }

        slen = json_parse( arr + used*desc->objsize, desc, str.in, str.size );
        if( !slen )
            goto fail;

        str.in += slen;
        str.size -= slen;
        ++used;
    }

    *out = arr;
    *count = used;
    return str.in - data + 1;
fail:
    json_free_array( arr, used, desc );
    return 0;
}

/****************************************************************************/

static int json_preprocess_object( mem_stream* str );
static int json_preprocess_array( mem_stream* str );

static int json_preprocess_object( mem_stream* str )
{
    int c;
    mem_stream_putc( str, '{' );
    while( 1 )
    {
        c = mem_stream_getc( str );
        if( c=='}'                       ) break;
        if( c!=TK_STR                    ) return 0;
        if( !mem_stream_copy_string(str) ) return 0;
        if( mem_stream_getc( str )!=':'  ) return 0;

        if( mem_stream_tryread( str, "null", 4 ) )
        {
            mem_stream_putc( str, TK_NULL );
        }
        else if( mem_stream_tryread( str, "true", 4 ) )
        {
            mem_stream_putc( str, '1' );
        }
        else if( mem_stream_tryread( str, "false", 5 ) )
        {
            mem_stream_putc( str, '0' );
        }
        else
        {
            c = mem_stream_getc( str );
            if( c==TK_STR )
            {
                mem_stream_putc( str, c );
                mem_stream_copy_string( str );
            }
            else if( c=='{' )
            {
                if( !json_preprocess_object(str) )
                    return 0;
            }
            else if( c=='[' )
            {
                if( !json_preprocess_array(str) )
                    return 0;
            }
            else
            {
                if( c=='-' )
                {
                    mem_stream_putc( str, c );
                    c = mem_stream_getc( str );
                }
                if( !isdigit(c) )
                    return 0;
                mem_stream_putc( str, c );
                while( str->size && isdigit(*(str->in)) )
                    mem_stream_copy( str );
            }
        }
        c = mem_stream_getc( str );
        if( c=='}' ) break;
        if( c!=',' ) return 0;
    }
    mem_stream_putc( str, c );
    return 1;
}

static int json_preprocess_array( mem_stream* str )
{
    int c;
    mem_stream_putc( str, '[' );
    while( 1 )
    {
        c = mem_stream_getc( str );
        if( c==']'                       ) break;
        if( c!='{'                       ) return 0;
        if( !json_preprocess_object(str) ) return 0;
        c = mem_stream_getc( str );
        if( c==']'                       ) break;
        if( c!=','                       ) return 0;
    }
    mem_stream_putc( str, c );
    return 1;
}

size_t json_preprocess( char* buffer, size_t size )
{
    int is_str = 0, c;
    mem_stream str;

    mem_stream_init( &str, buffer, size );

    while( (c = mem_stream_getc(&str)) > 0 )
    {
        if( is_str && c == '\\' )
        {
            switch( mem_stream_getc(&str) )
            {
            case '"':  c = '"';  break;
            case '\\': c = '\\'; break;
            case '/':  c = '/';  break;
            case 'b':  c = '\b'; break;
            case 'f':  c = '\f'; break;
            case 'n':  c = '\n'; break;
            case 'r':  c = '\r'; break;
            case 't':  c = '\t'; break;
            default:             return 0;
            }
        }
        else if( c == '"' )
        {
            c = is_str ? 0 : TK_STR;
            is_str = ~is_str;
        }
        else if( !is_str && isspace(c) )
        {
            continue;
        }
        mem_stream_putc( &str, c );
    }

    str.size = str.out - buffer;
    str.out = str.in = buffer;

    switch( mem_stream_getc(&str) )
    {
    case '[': c = json_preprocess_array(&str);  break;
    case '{': c = json_preprocess_object(&str); break;
    default:  c = 0;                            break;
    }

    return c ? (str.out - buffer) : 0;
}

