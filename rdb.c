#include <sys/wait.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <poll.h>

#include <sqlite3.h>

#include "sock.h"
#include "rdb.h"

#define TIMEOUT_MS 2000

static volatile int run = 1;

static void send_col( int fd, sqlite3_stmt* stmt, int i )
{
    int type = sqlite3_column_type( stmt, i );
    db_msg msg = { DB_COL_NULL, 0 };
    const void* blob = NULL;
    double dv;
    long lv;

    switch( type )
    {
    case SQLITE_INTEGER:
        msg.type = DB_COL_INT;
        msg.length = sizeof(lv);
        lv = sqlite3_column_int64(stmt, i);
        blob = &lv;
        break;
    case SQLITE_FLOAT:
        msg.type = DB_COL_DBL;
        msg.length = sizeof(dv);
        dv = sqlite3_column_double(stmt, i);
        blob = &dv;
        break;
    case SQLITE_BLOB:
        blob = sqlite3_column_blob(stmt, i);
        if( !blob )
            break;
        msg.type = DB_COL_BLOB;
        msg.length = sqlite3_column_bytes(stmt, i);
        break;
    case SQLITE_TEXT:
        blob = sqlite3_column_text(stmt, i);
        if( !blob )
            break;
        msg.type = DB_COL_TEXT;
        msg.length = sqlite3_column_bytes(stmt, i) + 1;
        break;
    }

    write( fd, &msg, sizeof(msg) );

    if( msg.length )
        write( fd, blob, msg.length );
}

static void exec_query( int type, sqlite3* db, int fd,
                        const char* query, unsigned int length )
{
    db_msg msg = { DB_RESULT_ERR, 0 };
    sqlite3_stmt* stmt;
    int i, count, rc;
    const char* name;

    if( sqlite3_prepare_v2( db, query, length, &stmt, NULL )!=SQLITE_OK )
        goto out;

    count = sqlite3_column_count( stmt );

    if( type==DB_QUERY_HDR )
    {
        msg.type = DB_COL_TEXT;

        for( i=0; i<count && (name = sqlite3_column_name(stmt,i)); ++i )
        {
            msg.length = strlen(name) + 1;
            write( fd, &msg, sizeof(msg) );
            write( fd, name, msg.length );
        }

        msg.type = DB_ROW_DONE;
        msg.length = 0;
        write( fd, &msg, sizeof(msg) );
    }

    while( (rc = sqlite3_step( stmt )) == SQLITE_ROW )
    {
        for( i=0; i<count; ++i )
            send_col( fd, stmt, i );

        msg.type = DB_ROW_DONE;
        write( fd, &msg, sizeof(msg) );
    }

    msg.type = (rc==SQLITE_DONE) ? DB_RESULT_DONE : DB_RESULT_ERR;
out:
    write( fd, &msg, sizeof(msg) );
    sqlite3_finalize( stmt );
}

static void handle_client( sqlite3* db, int fd )
{
    void* buffer = NULL;
    db_msg msg;

    while( wait_for_fd( fd, TIMEOUT_MS ) )
    {
        free( buffer );
        buffer = NULL;

        if( read( fd, &msg, sizeof(msg) )!=sizeof(msg) )
            break;

        if( msg.length )
        {
            buffer = malloc( msg.length );

            if( !buffer || read( fd, buffer, msg.length )!=msg.length )
            {
                msg.type = DB_RESULT_ERR;
                msg.length = 0;
                write( fd, &msg, sizeof(msg) );
                continue;
            }
        }

        switch( msg.type )
        {
        case DB_QUIT:
            goto out;
        case DB_QUERY:
        case DB_QUERY_HDR:
            exec_query( msg.type, db, fd, buffer, msg.length );
            break;
        }
    }
out:
    free( buffer );
    close( fd );
}

static void sighandler( int sig )
{
    if( sig == SIGTERM || sig == SIGINT )
        run = 0;
    if( sig == SIGCHLD )
        wait( NULL );
    signal( sig, sighandler );
}

int main( int argc, char** argv )
{
    struct pollfd pfd;
    int fd;

    if( argc!=3 )
    {
        fputs("Usage: rdb <dbfile> <unixsocket>\n",argc==1 ? stdout : stderr);
        return argc==1 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    /* create server socket */
    pfd.fd = create_socket( argv[2], 0, AF_UNIX );
    pfd.events = POLLIN;

    if( pfd.fd <= 0 )
        return EXIT_FAILURE;

    /* hook signal handlers */
    signal( SIGTERM, sighandler );
    signal( SIGINT, sighandler );
    signal( SIGCHLD, sighandler );
    signal( SIGPIPE, SIG_IGN );

    /* accept and dispatch client connections */
    while( run )
    {
        if( poll( &pfd, 1, -1 )!=1 || !(pfd.revents & POLLIN) )
            continue;

        fd = accept( pfd.fd, NULL, NULL );

        if( fd < 0 )
        {
            perror( "accept" );
            continue;
        }

        if( fork( ) == 0 )
        {
            sqlite3* db;

            if( sqlite3_open( argv[1], &db ) )
                fprintf( stderr, "sqlite3_open: %s\n", sqlite3_errmsg(db) );
            else
                handle_client( db, fd );

            close( fd );
            sqlite3_close( db );
            return EXIT_SUCCESS;
        }

        close( fd );
    }

    /* cleanup */
    close( pfd.fd );
    signal( SIGCHLD, SIG_IGN );
    while( wait(NULL)!=-1 ) { }
    unlink( argv[2] );
    return EXIT_SUCCESS;
}

