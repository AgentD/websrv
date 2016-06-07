#ifndef DB_H
#define DB_H

#include <stdint.h>
#include <time.h>

/* maximum size of databse message */
#define DB_MAX_MSG_SIZE 1024

enum
{
    DB_QUIT = 1,            /* sent by client for gracefull disconnect */

    /* Sent by DB if an error occoured. Connection is closed */
    DB_ERR = 2,

    /* Sent by DB if query is completed. Connection is closed */
    DB_DONE = 3,

    /* Sent by DB if query is completed. Connection is _not_ closed */
    DB_SUCCESS = 4,

    /* Sent by DB if query failed. Connection is _not_ closed */
    DB_FAIL = 5,

    /* Get a list of all objects in the demo table. Payload: none */
    DB_GET_OBJECTS = 10,

    /* Payload is a db_object struct */
    DB_OBJECT = 11,

    DB_SESSION_MIN = 20,    /* smallest session request type */
    DB_SESSION_MAX = 24,    /* largets session request type */

    /* payload: db_session_data object */
    DB_SESSION_DATA = 20,

    /* Payload: uint32_t UID. Returns DB_SESSION_DATA on success */
    DB_SESSION_CREATE = 21,

    /* Payload: uint32_t SID. Returns DB_SUCCESS on success */
    DB_SESSION_REMOVE = 22,

    /* Payload: uint32_t SID. Returns DB_SESSION_DATA on success */
    DB_SESSION_GET_DATA = 23,

    /* Payload: none. Returns list of uint32_t UIDs via DB_SESSION_LIST */
    DB_SESSION_LIST = 24
};

/* Databse Object used by demo */
typedef struct
{
    char name[64];
    char color[64];
    uint64_t value;
}
__attribute__((__packed__)) db_object;

typedef struct
{
    uint32_t sid;   /* unique ID of the session */
    uint32_t uid;   /* unique ID of the user */
    time_t atime;   /* last time that _specific_ session was accessed */
}
__attribute__((__packed__)) db_session_data;

typedef struct
{
    uint8_t type;       /* type identifier */
    uint16_t length;    /* payload size */
    uint8_t payload[];
}
__attribute__((__packed__)) db_msg;

#endif /* DB_H */

