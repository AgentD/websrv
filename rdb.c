#include <sys/wait.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <ctype.h>
#include <poll.h>

#include <sqlite3.h>

#include "sock.h"
#include "rdb.h"
#include "log.h"

#define TIMEOUT_MS 2000

static sig_atomic_t run = 1;

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
                WARN("DB_GET_OBJECTS: received invalid payload length");
                msg.type = DB_ERR;
                msg.length = 0;
                write( fd, &msg, sizeof(msg) );
                goto out;
            }
            get_objects( db, fd );
            goto out;
        case DB_QUIT:
            goto out;
        default:
            WARN("unknown request (ID=%d) received", msg.type);
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
    if( sig == SIGSEGV )
    {
        CRITICAL("SEGFAULT!!");
        exit(EXIT_FAILURE);
    }
    signal( sig, sighandler );
}

static void usage( void )
{
    puts( "Usage: rdb --db <dbfile> --sock <unixsocket> [--log <file>]\n"
          "           [--loglevel <num>]\n\n"
          "  --db           The SQLite data base file to get data from\n"
          "  --sock         Unix socket to listen on\n"
          "  --log          Append log output to a specific file\n"
          "  --loglevel     Higher value means more verbose\n" );
}

int main( int argc, char** argv )
{
    int i, j, fd, loglevel = LEVEL_WARNING, ret = EXIT_FAILURE;
    const char *sockfile = NULL, *dbfile = NULL;
    const char *logfile = NULL, *errstr = NULL;
    struct pollfd pfd;

    for( i=1; i<argc; ++i )
    {
        if( !strcmp(argv[i], "-h") || !strcmp(argv[i], "--help") )
            goto usage;
    }

    for( i=1; i<argc; ++i )
    {
        if( (i+1) > argc )
            goto err_arg;
        if( !strcmp(argv[i], "--sock") )
        {
            sockfile = argv[++i];
        }
        else if( !strcmp(argv[i], "--log") )
        {
            logfile = argv[++i];
        }
        else if( !strcmp(argv[i], "--db") )
        {
            dbfile = argv[++i];
        }
        else if( !strcmp(argv[i], "--loglevel") )
        {
            for( loglevel=0, j=0; isdigit(argv[i+1][j]); ++j )
                loglevel = loglevel * 10 + (argv[i+1][j] - '0');
            if( argv[i+1][j] )
                goto err_num;
            ++i;
        }
        else
        {
            fprintf( stderr, "Unknown option %s\n\n", argv[i] );
            goto fail;
        }
    }

    if( !log_init( logfile, loglevel ) )
        goto out;

    if( !sockfile )
    {
        CRITICAL( "No socket specified!" );
        goto fail;
    }

    if( !dbfile )
    {
        CRITICAL( "No data base file specified!" );
        goto fail;
    }

    /* create server socket */
    pfd.fd = create_socket( sockfile, 0, AF_UNIX );
    pfd.events = POLLIN;

    if( pfd.fd <= 0 )
        return EXIT_FAILURE;

    /* hook signal handlers */
    signal( SIGTERM, sighandler );
    signal( SIGINT, sighandler );
    signal( SIGCHLD, sighandler );
    signal( SIGSEGV, sighandler );
    signal( SIGPIPE, SIG_IGN );

    /* accept and dispatch client connections */
    while( run )
    {
        if( poll( &pfd, 1, -1 )!=1 || !(pfd.revents & POLLIN) )
            continue;

        fd = accept( pfd.fd, NULL, NULL );

        if( fd < 0 )
        {
            WARN( "accept: %m" );
            continue;
        }

        if( fork( ) == 0 )
        {
            sqlite3* db;

            if( sqlite3_open( dbfile, &db ) )
                CRITICAL( "sqlite3_open: %s", sqlite3_errmsg(db) );
            else
                handle_client( db, fd );

            close( fd );
            sqlite3_close( db );
            return EXIT_SUCCESS;
        }

        close( fd );
    }

    /* cleanup */
    ret = EXIT_SUCCESS;
out:
    INFO("shutting down");
    close( pfd.fd );
    signal( SIGCHLD, SIG_IGN );
    while( wait(NULL)!=-1 ) { }
    unlink( sockfile );
    log_cleanup( );
    return ret;
err_num:   errstr = "Expected a numeric argument for"; goto err_print;
err_arg:   errstr = "Missing argument for";            goto err_print;
err_print: fprintf(stderr, "%s option %s\n\n", errstr, argv[i]); goto fail;
fail:
    fprintf(stderr, "Try '%s --help' for more information\n\n", argv[0]);
    goto out;
usage:
    usage( );
    return EXIT_SUCCESS;
}

