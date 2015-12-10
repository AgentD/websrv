#ifndef DB_H
#define DB_H

enum
{
    DB_QUIT = 1,            /* sent by client for gracefull disconnect */
    DB_QUERY = 2,           /* submitt an SQL query (payload is SQL string) */
    DB_ROW_DONE = 3,        /* No payload. Current row completed. */
    DB_RESULT_DONE = 4,     /* No payload. Entire table completed. */
    DB_RESULT_ERR = 5,      /* No payload. An error occoured. */
    DB_COL_INT = 6,         /* payload is a long integer */
    DB_COL_DBL = 7,         /* payload is a double value */
    DB_COL_BLOB = 8,        /* payload is a raw block of bytes */
    DB_COL_TEXT = 9,        /* payload is a null-terminated UTF-8 string */
    DB_COL_NULL = 10,       /* empty blob, text string or NULL value */
    DB_QUERY_HDR = 11       /* same as DB_QUERY but first result row
                               is table header */
};

typedef struct
{
    int type;               /* type identifier */
    unsigned int length;    /* payload size */
}
db_msg;

#endif /* DB_H */

