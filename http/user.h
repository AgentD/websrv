#ifndef USER_H
#define USER_H

#include "http.h"
#include "rdb.h"

/*
    Get the data from a user session.
     db:   Socket file handle to talk to the database
     data: Return the session data on success
     sid:  Session ID

    Returns a value >0 on success, 0 if the sid is invalid, a value < 0 if
    an error occoured.
 */
int user_get_session_data( int db, db_session_data* data, uint32_t sid );

/*
    Create a new session for a user.
     db:   Socket file handle to talk to the database
     data: Return the session data on success
     uid:  User ID

    Returns a value >0 on success, 0 if the sid is invalid, a value < 0 if
    an error occoured.
 */
int user_create_session( int db, db_session_data* data, uint32_t uid );

/*
    Destroy a session.
     db:   Socket file handle to talk to the database
     sid:  Session ID

    Returns a value > 0 on success, a value < 0 if an error occoured.
 */
int user_destroy_session( int db, uint32_t sid );

/*
    Read the session cookie from an HTTP request. Returns session ID on
    success, or 0 if not available/malformed.
 */
uint32_t user_get_session_cookie( const http_request* req );

/*
    Print the session cookie to a string (in the form "<name>=<value>").
    Returns non-zero on success, zero if the buffer is not large enough.
 */
int user_print_session_cookie( char* buffer, size_t size, uint32_t sid );

#endif /* USER_H */

