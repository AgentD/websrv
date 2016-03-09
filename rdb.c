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

static void get_objects( sqlite3* db, int fd )
{
    static const char* query = "SELECT * FROM demotable";
    db_msg msg = { DB_ERR, 0 };
    const char *name, *color;
    int rc, namelen, clen;
    sqlite3_stmt* stmt;
    db_object obj;

    rc = sqlite3_prepare_v2( db, query, strlen(query), &stmt, NULL );

    if( rc != SQLITE_OK )
        goto out;

    while( (rc = sqlite3_step( stmt )) == SQLITE_ROW )
    {
        name = (const char*)sqlite3_column_text(stmt, 0);
        color = (const char*)sqlite3_column_text(stmt, 1);
        obj.value = sqlite3_column_int64(stmt, 2);

        namelen = strlen(name) + 1;
        clen = strlen(color) + 1;

        msg.type = DB_OBJECT;
        msg.length = namelen + clen + sizeof(obj);

        obj.name = (char*)sizeof(obj);
        obj.color = obj.name + namelen;

        write( fd, &msg, sizeof(msg) );
        write( fd, &obj, sizeof(obj) );
        write( fd, name, namelen );
        write( fd, color, clen );
    }

    msg.type = (rc==SQLITE_DONE) ? DB_DONE : DB_ERR;
out:
    write( fd, &msg, sizeof(msg) );
    sqlite3_finalize( stmt );
}

static void handle_client( sqlite3* db, int fd )
{
    db_msg msg;

    while( wait_for_fd( fd, TIMEOUT_MS ) )
    {
        if( read( fd, &msg, sizeof(msg) )!=sizeof(msg) )
            break;

        switch( msg.type )
        {
        case DB_GET_OBJECTS:
            if( msg.length )
            {
                msg.type = DB_ERR;
                msg.length = 0;
                write( fd, &msg, sizeof(msg) );
                goto out;
            }
            get_objects( db, fd );
            goto out;
        default:
            goto out;
        }
    }
out:
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

