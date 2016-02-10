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
    char* end;
    size_t size;
}
mem_stream;

static void mem_stream_init( mem_stream* str, char* buffer, size_t size )
{
    str->in = str->out = buffer;
    str->end = buffer + size;
    str->size = size;
}

static int mem_stream_getc( mem_stream* str )
{
    if( !str->size )
        return -1;
    --str->size;
    return *((unsigned char*)str->in++);
}

#define mem_stream_putc(str, c) (*((unsigned char*)((str)->out++)) = (c))

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

static int mem_stream_move_string_to_end( mem_stream* str )
{
    size_t len = strnlen( str->in, str->size ), chunk;
    char buffer[ 128 ];

    len = (len==str->size) ? len : (len+1);

    while( len )
    {
        chunk = len > sizeof(buffer) ? sizeof(buffer) : len;

        memcpy( buffer, str->in + len - chunk, chunk );
        memmove( str->in + len - chunk, str->in + len, str->size - len );
        str->size -= chunk;
        len -= chunk;

        str->end -= chunk;
        memcpy( str->end, buffer, chunk );
    }

    return str->size!=0;
}

static int mem_stream_tryread( mem_stream* str, const char* word, size_t len )
{
    if( len > str->size || strncmp(str->in, word, len) )
        return 0;
    str->in += len;
    str->size -= len;
    return 1;
}

static int mem_stream_parse_int( mem_stream* str, int* i )
{
    int s = 1;

    if( str->size && str->in[0]=='-' )
    {
        --str->size;
        ++str->in;
        s = -1;
    }

    if( !str->size || !isdigit(*str->in) )
        return 0;

    for( *i=0; str->size && isdigit(*str->in); ++str->in, --str->size )
        *i = (*i) * 10 +  (*str->in - '0');

    *i *= s;
    return 1;
}

static int mem_stream_read_int( mem_stream* str, int tk, int* i )
{
    size_t len;
    switch( tk )
    {
    case TK_S8:
    case TK_U8:  *i = *((uint8_t*)str->in);  len = sizeof(uint8_t);  break;
    case TK_S16:
    case TK_U16: *i = *((uint16_t*)str->in); len = sizeof(uint16_t); break;
    case TK_S32:
    case TK_U32: *i = *((uint32_t*)str->in); len = sizeof(uint32_t); break;
    default:
        return 0;
    }
    if( tk == TK_S32 || tk == TK_S16 || tk == TK_S8 )
        *i = -(*i);
    str->in += len;
    str->size -= len;
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

static int mem_stream_read_hex( mem_stream* str, size_t count, int* i )
{
    int c;
    if( count >= str->size )
        return 0;
    *i = 0;
    while( count-- )
    {
        c = *(str->in++);
        if( !isxdigit(c) ) return 0;
             if( isupper(c) ) c = c - 'A' + 10;
        else if( islower(c) ) c = c - 'a' + 10;
        else                  c = c - '0';
        *i = ((*i)<<4) | c;
        --str->size;
    }
    return 1;
}

static int mem_stream_write_utf8( mem_stream* str, unsigned int cp )
{
    if( cp > 0x10FFFF || cp==0xFFFE || cp==0xFEFF )
        return 0;
    if( cp>=0xD800 && cp<=0xDFFF )
        return 0;
    if( cp < 0x80    ) { mem_stream_putc(str, cp);              return 1;  }
    if( cp < 0x800   ) { mem_stream_putc(str, 0xC0|(cp >> 6) ); goto out1; }
    if( cp < 0x10000 ) { mem_stream_putc(str, 0xE0|(cp >> 12)); goto out2; }
    mem_stream_putc( str, 0xF0 | (cp >> 18) );
    mem_stream_putc( str, 0x80 | ((cp >> 12) & 0x3F) );
out2:
    mem_stream_putc( str, 0x80 | ((cp >> 6) & 0x3F) );
out1:
    mem_stream_putc( str, 0x80 | (cp & 0x3F) );
    return 1;
}

/****************************************************************************/

static int deserialize_array( void** out, size_t* count,
                              const js_struct* desc, mem_stream* str );

static int deserialize( void* obj, const js_struct* desc, mem_stream* str )
{
    const js_struct* subdesc;
    char* start = str->in;
    size_t i, *arrsize;
    void *sub, *memb;
    int c;

    while( (c = mem_stream_getc(str)) != 0xFF )
    {
        i = c & 0xFF;
        memb = (char*)obj + desc->members[i].offset;
        arrsize = (size_t*)((char*)obj + desc->members[i].sizeoffset);
        subdesc = desc->members[i].desc;

        c = mem_stream_getc(str);

        if( c==TK_NULL ) { *((void**)memb)=NULL; continue; }

        switch( desc->members[i].type )
        {
        case TYPE_OBJ:
        {
            sub = alloca( subdesc->objsize );
            memset( sub, 0, subdesc->objsize );
            if( !deserialize(sub,subdesc,str) ) { return 0; }

            if( start!=str->in )
            {
                memmove( start, str->in, str->size );
                str->in = start;
            }

            str->end -= subdesc->objsize;
            memcpy( str->end, sub, subdesc->objsize );
            *((void**)memb) = str->end;
            break;
        }
        case TYPE_OBJ_ARRAY:
            if( !deserialize_array(memb, arrsize, subdesc, str) ) return 0;
            break;
        case TYPE_INT:
            if( !mem_stream_read_int( str, c, (int*)memb ) ) return 0;
            break;
        case TYPE_STRING:
            mem_stream_move_string_to_end( str );
            *((char**)memb) = str->end;
            break;
        default:
            return 0;
        }
    }
    return 1;
}

static int deserialize_array( void** out, size_t* count,
                              const js_struct* desc, mem_stream* str )
{
    size_t size = 10, used = 0;
    char *arr = calloc( desc->objsize, size ), *new, *start = str->in;
    int c;

    while( 1 )
    {
        c = mem_stream_getc( str );
        if( c==']' ) break;

        if( used > ((3*size)/4) )
        {
            size *= 2;
            if( !(new = realloc( arr, size * desc->objsize )) ) goto fail;
            arr = new;
            memset(arr + used*desc->objsize, 0, (size - used)*desc->objsize);
        }

        if( !deserialize( arr + used*desc->objsize, desc, str ) ) goto fail;
        ++used;

        if( start!=str->in )
        {
            memmove( start, str->in, str->size );
            str->in = start;
        }
    }

    str->end -= desc->objsize * used;
    memcpy( str->end, arr, desc->objsize * used );
    free( arr );

    *out = str->end;
    *count = used;
    return 1;
fail:
    free( arr );
    return 0;
}

/****************************************************************************/

static int json_skip( mem_stream* str );

static int json_skip_object( mem_stream* str )
{
    size_t slen;
    int c;

    if( mem_stream_getc( str )!='{' ) return 0;
    if( str->size && *str->in=='}' ) { ++str->in; --str->size; return 1; }
    do
    {
        if( mem_stream_getc( str )!=TK_STR ) return 0;

        slen = strnlen(str->in, str->size);
        if( slen == str->size )
            return 0;
        str->in += slen + 1;

        if( mem_stream_getc( str )!=':' ) return 0;
        if( !json_skip( str ) ) return 0;
        c = mem_stream_getc( str );
    }
    while( c==',' );
    return c == '}';
}

static int json_skip_array( mem_stream* str )
{
    int c;
    if( mem_stream_getc( str )!='[' ) return 0;
    if( str->size && *str->in==']' ) { ++str->in; --str->size; return 1; }
    do
    {
        if( !json_skip( str ) ) return 0;
        c = mem_stream_getc( str );
    }
    while( c==',' );
    return c==']';
}

static int json_skip( mem_stream* str )
{
    size_t slen;

    if( !str->size                            ) return 0;
    if( mem_stream_tryread( str, "null",  4 ) ) return 1;
    if( mem_stream_tryread( str, "true",  4 ) ) return 1;
    if( mem_stream_tryread( str, "false", 5 ) ) return 1;
    if( *str->in == '{'                       ) return json_skip_object(str);
    if( *str->in == '['                       ) return json_skip_array(str);

    if( *str->in == TK_STR )
    {
        slen = strnlen(str->in, str->size);
        if( slen == str->size )
            return 0;
        str->in += slen + 1;
        return 1;
    }

    if( str->size && *str->in == '-' )
    {
        ++str->in;
        --str->size;
    }
    if( !str->size || !isdigit(*str->in) )
        return 0;
    while( str->size && isdigit(*str->in) ) { --str->size; ++str->in; }

    if( str->size && *str->in == '.' )
    {
        ++str->in;
        --str->size;
        if( !str->size || !isdigit(*str->in) )
            return 0;
        while( str->size && isdigit(*str->in) ) { --str->size; ++str->in; }
    }
    if( str->size && (*str->in=='e' || *str->in=='E') )
    {
        ++str->in;
        --str->size;
        if( str->size && (*str->in=='+' || *str->in=='-') )
        {
            ++str->in;
            --str->size;
        }
        if( !str->size || !isdigit(*str->in) )
            return 0;
        while( str->size && isdigit(*str->in) ) { --str->size; ++str->in; }
    }
    return 1;
}

/****************************************************************************/

static int json_preprocess_array( mem_stream* str, const js_struct* desc );

static int json_preprocess_object( mem_stream* str, const js_struct* desc )
{
    size_t i, slen;
    int c;
    mem_stream_putc( str, '{' );
    while( 1 )
    {
        if( mem_stream_getc( str )!=TK_STR ) return 0;

        for( i=0; i<desc->num_members; ++i )
        {
            slen = strlen(desc->members[i].name) + 1;
            if( mem_stream_tryread( str, desc->members[i].name, slen ) )
                break;
        }

        if( i>=desc->num_members || i>=0xFF )
        {
            slen = strnlen(str->in, str->size);
            if( slen == str->size           ) return 0;
            str->in += slen + 1;
            str->size -= slen + 1;
            if( mem_stream_getc( str )!=':' ) return 0;
            if( !json_skip( str )           ) return 0;
            goto next;
        }

        mem_stream_putc( str, i & 0xFF );
        if( mem_stream_getc( str )!=':' ) return 0;

        if( mem_stream_tryread( str, "null", 4 ) )
        {
            if( desc->members[i].type!=TYPE_STRING &&
                desc->members[i].type!=TYPE_OBJ &&
                desc->members[i].type!=TYPE_OBJ_ARRAY )
            {
                return 0;
            }
            mem_stream_putc( str, TK_NULL );
        }
        else if( mem_stream_tryread( str, "true", 4 ) )
        {
            if( desc->members[i].type!=TYPE_INT )
                return 0;
            mem_stream_write_int( str, 1 );
        }
        else if( mem_stream_tryread( str, "false", 5 ) )
        {
            if( desc->members[i].type!=TYPE_INT )
                return 0;
            mem_stream_write_int( str, 0 );
        }
        else
        {
            c = mem_stream_getc( str );
            if( c==TK_STR )
            {
                if( desc->members[i].type != TYPE_STRING )
                    return 0;
                mem_stream_putc( str, c );
                mem_stream_copy_string( str );
            }
            else if( c=='{' )
            {
                if( desc->members[i].type != TYPE_OBJ )
                    return 0;
                if( !json_preprocess_object(str, desc->members[i].desc) )
                    return 0;
            }
            else if( c=='[' )
            {
                if( desc->members[i].type != TYPE_OBJ_ARRAY )
                    return 0;
                if( !json_preprocess_array(str, desc->members[i].desc) )
                    return 0;
            }
            else
            {
                if( desc->members[i].type != TYPE_INT )
                    return 0;
                --(str->in);
                ++(str->size);
                if( !mem_stream_parse_int( str, &c ) )
                    return 0;
                mem_stream_write_int( str, c );
            }
        }
    next:
        c = mem_stream_getc( str );
        if( c=='}' ) break;
        if( c!=',' ) return 0;
    }
    mem_stream_putc( str, 0xFF );
    return 1;
}

static int json_preprocess_array( mem_stream* str, const js_struct* desc )
{
    int c;
    mem_stream_putc( str, '[' );
    while( 1 )
    {
        if( mem_stream_getc( str )!='{'        ) return 0;
        if( !json_preprocess_object(str, desc) ) return 0;
        c = mem_stream_getc( str );
        if( c==']'                             ) break;
        if( c!=','                             ) return 0;
    }
    mem_stream_putc( str, c );
    return 1;
}

static int json_preprocess( mem_stream* str )
{
    char* old = str->in;
    int is_str = 0, c;

    while( (c = mem_stream_getc(str)) > 0 )
    {
        if( is_str && c == '\\' )
        {
            switch( mem_stream_getc(str) )
            {
            case '"':  c = '"';  break;
            case '\\': c = '\\'; break;
            case '/':  c = '/';  break;
            case 'b':  c = '\b'; break;
            case 'f':  c = '\f'; break;
            case 'n':  c = '\n'; break;
            case 'r':  c = '\r'; break;
            case 't':  c = '\t'; break;
            case 'u':
                if( !mem_stream_read_hex(str,4,&c) ) return 0;
                if( c==0                           ) return 0;
                if( !mem_stream_write_utf8(str, c) ) return 0;
                break;
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
        mem_stream_putc( str, c );
    }

    str->size = str->out - old;
    str->out = str->in = old;
    return 1;
}

/****************************************************************************/

int json_deserialize( void* obj, const js_struct* desc,
                      char* buffer, size_t size )
{
    mem_stream str;

    mem_stream_init( &str, buffer, size );
    if( !json_preprocess(&str)              ) return 0;
    if( mem_stream_getc(&str)!='{'          ) return 0;
    if( !json_preprocess_object(&str, desc) ) return 0;

    mem_stream_init( &str, buffer+1, str.out-buffer-1 );
    str.end = buffer + size;
    return deserialize( obj, desc, &str );
}

int json_deserialize_array( void** out, size_t* count, const js_struct* desc,
                            char* buffer, size_t size )
{
    mem_stream str;

    mem_stream_init( &str, buffer, size );
    if( !json_preprocess(&str)             ) return 0;
    if( mem_stream_getc(&str)!='['         ) return 0;
    if( !json_preprocess_array(&str, desc) ) return 0;

    mem_stream_init( &str, buffer+1, str.out-buffer-1 );
    str.end = buffer + size;
    return deserialize_array( out, count, desc, &str );
}

