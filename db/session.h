#ifndef SESSION_H
#define SESSION_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

#define SESSION_EXPIRE 600

struct session
{
    uint32_t sid;   /* unique session ID */
    uint32_t uid;   /* ID of user that created the session */
    time_t atime;   /* last access to session */
};

int sesion_init( void );

void session_cleanup( void );

/* gain exclusive access to the session buffer */
void session_lock( void );

/* release exclusive access to the session buffer */
void session_unlock( void );

/* get the number of sessions */
size_t sessions_get_count( void );

/* get a session by an index into the session array */
struct session* sessions_get( size_t index );

/* get a session from a session ID */
struct session* sessions_get_by_id( uint32_t id );

/* delete sessions that are expired */
void sessions_check_expire( void );

/* initialize and add a new session */
struct session* session_create( void );

/* remove a session by its unique ID */
void session_remove_by_id( uint32_t id );


#endif /* SESSION_H */

