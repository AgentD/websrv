#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include "user.h"

static const char* hexdigits = "0123456789abcdef";

static int read_session_data( int db, db_session_data* data )
{
    db_msg msg;

    if( read( db, &msg, sizeof(msg) ) != sizeof(msg) ) return -1;
    if( msg.type == DB_FAIL                          ) return 0;
    if( msg.type != DB_SESSION_DATA                  ) return -1;
    if( msg.length != sizeof(*data)                  ) return -1;

    return read( db, data, sizeof(*data) ) == sizeof(*data) ? 1 : -1;
}

int user_get_session_data( int db, db_session_data* data, uint32_t sid )
{
    unsigned char buffer[ sizeof(db_msg) + sizeof(uint32_t) ];
    db_msg* msg = (db_msg*)buffer;
    uint32_t* msg_id = (uint32_t*)msg->payload;

    if( !sid )
        return 0;

    msg->type = DB_SESSION_GET_DATA;
    msg->length = sizeof(sid);
    *msg_id = sid;

    if( write( db, buffer, sizeof(buffer) ) != sizeof(buffer) )
        return -1;

    return read_session_data( db, data );
}

int user_create_session( int db, db_session_data* data, uint32_t uid )
{
    unsigned char buffer[ sizeof(db_msg) + sizeof(uint32_t) ];
    db_msg* msg = (db_msg*)buffer;
    uint32_t* msg_id = (uint32_t*)msg->payload;

    msg->type = DB_SESSION_CREATE;
    msg->length = sizeof(uid);
    *msg_id = uid;

    if( write( db, buffer, sizeof(buffer) ) != sizeof(buffer) )
        return -1;

    return read_session_data( db, data );
}

int user_destroy_session( int db, uint32_t sid )
{
    unsigned char buffer[ sizeof(db_msg) + sizeof(uint32_t) ];
    db_msg* msg = (db_msg*)buffer;
    uint32_t* msg_id = (uint32_t*)msg->payload;

    msg->type = DB_SESSION_REMOVE;
    msg->length = sizeof(sid);
    *msg_id = sid;

    if( write( db, buffer, sizeof(buffer) ) != sizeof(buffer) )
        return -1;

    if( read( db, msg, sizeof(*msg) ) != sizeof(*msg) )
        return -1;

    return (msg->type == DB_SUCCESS && msg->length == 0) ? 0 : -1;
}

uint32_t user_get_session_cookie( const http_request* req )
{
    const char *sidstr, *ptr;
    uint32_t sid;
    int i;

    sidstr = http_get_arg( req->cookies, req->numcookies, "session" );
    if( !sidstr )
        return 0;

    sid = 0;
    for( i=0; i<8; ++i )
    {
        ptr = strchr( hexdigits, *(sidstr++) );
        if( !ptr )
            return 0;

        sid = (sid << 4) | (ptr - hexdigits);
    }

    return *sidstr ? 0 : sid;
}

int user_print_session_cookie( char* buffer, size_t size, uint32_t sid )
{
    int i;

    if( size < 17 )
        return 0;

    memcpy( buffer, "session=00000000\0", 17 );
    buffer += 15;

    for( i=0; i<8; ++i )
    {
        *(buffer--) = hexdigits[ sid & 0x0F ];
        sid >>= 4;
    }

    return 1;
}

