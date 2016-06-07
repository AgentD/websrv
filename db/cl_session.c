#include <string.h>
#include <unistd.h>

#include "cl_session.h"
#include "session.h"
#include "config.h"
#include "log.h"


#ifdef HAVE_SESSION
static int create_session( int fd, db_msg *msg )
{
    db_session_data resp;
    struct session* s;
    uint32_t uid;
    db_msg rmsg;

    if( msg->length != sizeof(uid) )
    {
        WARN("DB_SESSION_CREATE: received invalid payload length");
        return 0;
    }

    if( read( fd, &uid, sizeof(uid) ) != sizeof(uid) )
    {
        WARN("DB_SESSION_CREATE: could not read UID");
        return 0;
    }

    s = session_create( );

    if( s )
    {
        s->uid = uid;
        s->atime = time(NULL);

        resp.sid = s->sid;
        resp.uid = s->uid;
        resp.atime = s->atime;

        rmsg.type = DB_SESSION_DATA;
        rmsg.length = sizeof(resp);
        write( fd, &rmsg, sizeof(rmsg) );
        write( fd, &resp, sizeof(resp) );
    }
    else
    {
        WARN("DB_SESSION_CREATE: creating session failed");
        memset( &rmsg, 0, sizeof(rmsg) );
        rmsg.type = DB_FAIL;
        write( fd, &rmsg, sizeof(rmsg) );
    }
    return 1;
}

static int remove_session( int fd, db_msg *msg )
{
    db_msg rmsg = { DB_SUCCESS, 0 };
    uint32_t sid;

    if( msg->length != sizeof(sid) )
    {
        WARN("DB_SESSION_REMOVE: received invalid payload length");
        return 0;
    }

    read( fd, &sid, sizeof(sid) );
    session_remove_by_id( sid );

    write( fd, &rmsg, sizeof(rmsg) );
    return 1;
}

static int get_session_data( int fd, db_msg *msg )
{
    db_session_data resp;
    struct session* s;
    uint32_t sid;
    db_msg rmsg;

    if( msg->length!=sizeof(sid) || read(fd,&sid,sizeof(sid))!=sizeof(sid) )
    {
        WARN("DB_SESSION_GET_DATA: received invalid payload length");
        return 0;
    }

    s = sessions_get_by_id( sid );

    if( s )
    {
        s->atime = time(NULL);

        resp.sid = s->sid;
        resp.uid = s->uid;
        resp.atime = s->atime;

        rmsg.type = DB_SESSION_DATA;
        rmsg.length = sizeof(resp);
        write( fd, &rmsg, sizeof(rmsg) );
        write( fd, &resp, sizeof(resp) );
    }
    else
    {
        memset( &rmsg, 0, sizeof(rmsg) );
        rmsg.type = DB_FAIL;
        write( fd, &rmsg, sizeof(rmsg) );
    }
    return 1;
}

static int get_session_list( int fd, db_msg *msg )
{
    struct session* s;
    size_t i, count;
    db_msg rmsg;

    if( msg->length )
    {
        WARN("DB_SESSION_LIST: received invalid payload length");
        return 0;
    }

    count = sessions_get_count( );
    rmsg.type = DB_SESSION_LIST;
    rmsg.length = count * sizeof(uint32_t);
    write( fd, &rmsg, sizeof(rmsg) );

    for( i = 0; i < count; ++i )
    {
        s = sessions_get( i );

        if( !s )
        {
            CRITICAL("DB_SESSION_LIST: got NULL for index %lu",
                     (unsigned long)i);
            return 0;
        }

        write( fd, &s->uid, sizeof(s->uid) );
    }
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

