#ifndef DB_H
#define DB_H

enum
{
    DB_QUIT = 1,            /* sent by client for gracefull disconnect */

    /* Sent by DB if an error occoured. Connection is closed */
    DB_ERR = 2,

    /* Sent by DB if query is completed. Connection is closed */
    DB_DONE = 3,

    /* Get a list of all objects in the demo table. Payload: none */
    DB_GET_OBJECTS = 10,

    /* Payload is a db_object struct */
    DB_OBJECT = 11
};

/* Databse Object used by demo */
typedef struct
{
    const char* name;
    const char* color;
    unsigned long value;
}
db_object;

typedef struct
{
    int type;               /* type identifier */
    unsigned int length;    /* payload size */
}
db_msg;

#endif /* DB_H */

