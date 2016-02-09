#include "json.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#define TK_STR 1
#define TK_NULL 2
#define TK_S8 3
#define TK_S16 4
#define TK_S32 5
#define TK_U8 6
#define TK_U16 7
#define TK_U32 8

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

static int mem_stream_parse_int( mem_stream* str, int* i )
{
    int s = 1, c = mem_stream_getc( str );

    *i = 0;

    if( c == '-' )
    {
        s = -1;
        c = mem_stream_getc( str );
    }
    if( !isdigit(c) )
        return 0;
    do
    {
        *i = (*i) * 10 +  (c - '0');
        c = mem_stream_getc( str );
    }
    while( isdigit(c) );
    --str->in;
    ++str->size;
    *i *= s;
    return 1;
}

static int mem_stream_read_int( mem_stream* str, int* i )
{
    int tk = mem_stream_getc( str );
    switch( tk )
    {
    case TK_S8:
    case TK_U8:
        if( str->size < sizeof(uint8_t) )
            return 0;
        *i = *((uint8_t*)str->in);
        str->in += sizeof(uint8_t);
        str->size -= sizeof(uint8_t);
        break;
    case TK_S16:
    case TK_U16:
        if( str->size < sizeof(uint16_t) )
            return 0;
        *i = *((uint16_t*)str->in);
        str->in += sizeof(uint16_t);
        str->size -= sizeof(uint16_t);
        break;
    case TK_S32:
    case TK_U32:
        if( str->size < sizeof(uint32_t) )
            return 0;
        *i = *((uint32_t*)str->in);
        str->in += sizeof(uint32_t);
        str->size -= sizeof(uint32_t);
        break;
    default:
        return 0;
    }
    if( tk == TK_S32 || tk == TK_S16 || tk == TK_S8 )
        *i = -(*i);
    return 1;
}

static void mem_stream_write_int( mem_stream* str, int i )
{
    int tk;

    if( i < 0 )
    {
        i = -i;
             if( i<=0xFF   ) tk = TK_S8;
        else if( i<=0xFFFF ) tk = TK_S16;
        else                 tk = TK_S32;
    }
    else
    {
             if( i<=0xFF   ) tk = TK_U8;
        else if( i<=0xFFFF ) tk = TK_U16;
        else                 tk = TK_U32;
    }

    *(str->out++) = tk;
    switch( tk )
    {
    case TK_S8:
    case TK_U8:
        *((uint8_t*)str->out) = i & 0xFF;
        str->out += sizeof(uint8_t);
        break;
    case TK_S16:
    case TK_U16:
         *((uint16_t*)str->out) = i & 0xFFFF;
         str->out += sizeof(uint16_t);
         break;
    default:
        *((uint32_t*)str->out) = i & 0xFFFFFFFF;
        str->out += sizeof(uint32_t);
        break;
    }
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
            mem_stream_write_int( str, 1 );
        }
        else if( mem_stream_tryread( str, "false", 5 ) )
        {
            mem_stream_write_int( str, 0 );
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
                --(str->in);
                ++(str->size);
                if( !mem_stream_parse_int( str, &c ) )
                    return 0;
                mem_stream_write_int( str, c );
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

