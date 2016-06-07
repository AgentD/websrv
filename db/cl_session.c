#include <string.h>
#include <unistd.h>

#include "cl_session.h"
#include "session.h"
#include "config.h"
#include "log.h"


#ifdef HAVE_SESSION
static int create_session( int fd, db_msg *msg )
{
    uint32_t* uid = (uint32_t*)msg->payload;
    db_session_data* resp = (db_session_data*)msg->payload;
    struct session* s;

    if( msg->length < sizeof(*uid) )
    {
        WARN("DB_SESSION_CREATE: received invalid payload length");
        return 0;
    }

    s = session_create( );

    if( s )
    {
        s->uid = *uid;
        s->atime = time(NULL);

        msg->type = DB_SESSION_DATA;
        msg->length = sizeof(*resp);
        resp->sid = s->sid;
        resp->uid = s->uid;
        resp->atime = s->atime;
    }
    else
    {
        WARN("DB_SESSION_CREATE: creating session failed");
        msg->type = DB_FAIL;
        msg->length = 0;
    }

    write( fd, msg, sizeof(*msg) + msg->length );
    return 1;
}

static int remove_session( int fd, db_msg *msg )
{
    uint32_t* sid = (uint32_t*)msg->payload;

    if( msg->length < sizeof(*sid) )
    {
        WARN("DB_SESSION_REMOVE: received invalid payload length");
        return 0;
    }

    session_remove_by_id( *sid );

    msg->type = DB_SUCCESS;
    msg->length = 0;
    write( fd, msg, sizeof(*msg) );
    return 1;
}

static int get_session_data( int fd, db_msg *msg )
{
    db_session_data* resp = (db_session_data*)msg->payload;
    uint32_t* sid = (uint32_t*)msg->payload;
    struct session* s;

    if( msg->length < sizeof(*sid) )
    {
        WARN("DB_SESSION_GET_DATA: received invalid payload length");
        return 0;
    }

    s = sessions_get_by_id( *sid );

    if( s )
    {
        s->atime = time(NULL);

        msg->type = DB_SESSION_DATA;
        msg->length = sizeof(*resp);

        resp->sid = s->sid;
        resp->uid = s->uid;
        resp->atime = s->atime;
    }
    else
    {
        msg->type = DB_FAIL;
        msg->length = 0;
    }

    write( fd, msg, sizeof(*msg) + msg->length );
    return 1;
}

static int get_session_list( int fd, db_msg *msg )
{
    uint32_t* uids = (uint32_t*)msg->payload;
    struct session* s;
    size_t i, count;

    count = sessions_get_count( );
    msg->type = DB_SESSION_LIST;
    msg->length = 0;

    if( (sizeof(*msg) + sizeof(s->uid) * count) > DB_MAX_MSG_SIZE )
    {
        CRITICAL("DB_SESSION_LIST: list too long, truncating");
        count = DB_MAX_MSG_SIZE - sizeof(*msg);
        count -= count % sizeof(s->uid);
        count /= sizeof(s->uid);
    }

    for( i = 0; i < count; ++i )
    {
        s = sessions_get( i );

        if( !s )
        {
            CRITICAL("DB_SESSION_LIST: got NULL for index %lu",
                     (unsigned long)i);
            return 0;
        }

        uids[i] = s->uid;
        msg->length += sizeof(s->uid);
    }

    write( fd, msg, sizeof(*msg) + msg->length );
    return 1;
}

int handle_session_message( int fd, db_msg* msg )
{
    int ret = 0;

    session_lock( );
    sessions_check_expire( );

    switch( msg->type )
    {
    case DB_SESSION_CREATE:   ret = create_session( fd, msg ); break;
    case DB_SESSION_REMOVE:   ret = remove_session( fd, msg ); break;
    case DB_SESSION_GET_DATA: ret = get_session_data( fd, msg ); break;
    case DB_SESSION_LIST:     ret = get_session_list( fd, msg ); break;
    }

    session_unlock( );
    return ret;
}
#endif

