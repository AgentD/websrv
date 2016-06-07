#include <sys/stat.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <stdio.h>
#include <ctype.h>
#include <poll.h>

#include <sqlite3.h>

#include "cl_session.h"
#include "session.h"
#include "config.h"
#include "sock.h"
#include "rdb.h"
#include "log.h"

#define TIMEOUT_MS 2000

static const struct option options[] =
{
    { "db", required_argument, NULL, 'd' },
    { "sock", required_argument, NULL, 's' },
    { "log", required_argument, NULL, 'f' },
    { "loglevel", required_argument, NULL, 'l' },
    { NULL, 0, NULL, 0 },
};

static sig_atomic_t run = 1;

static void get_objects( sqlite3* db, int fd )
{
    unsigned char buffer[ sizeof(db_msg) + sizeof(db_object) ];
    static const char* query = "SELECT * FROM demotable";
    db_msg* msg = (db_msg*)buffer;
    db_object* obj = (db_object*)msg->payload;
    const char *name, *color;
    sqlite3_stmt* stmt;
    int rc;

    rc = sqlite3_prepare_v2( db, query, strlen(query), &stmt, NULL );

    if( rc != SQLITE_OK )
    {
        msg->type = DB_FAIL;
        msg->length = 0;
        goto out;
    }

    msg->type = DB_OBJECT;
    msg->length = sizeof(*obj);

    while( (rc = sqlite3_step( stmt )) == SQLITE_ROW )
    {
        name = (const char*)sqlite3_column_text(stmt, 0);
        strncpy( obj->name, name, sizeof(obj->name) - 1 );
        obj->name[ sizeof(obj->name) - 1 ] = 0;

        color = (const char*)sqlite3_column_text(stmt, 1);
        strncpy( obj->color, color, sizeof(obj->color) - 1 );
        obj->color[ sizeof(obj->color) - 1 ] = 0;

        obj->value = sqlite3_column_int64(stmt, 2);

        write( fd, msg, sizeof(*msg) + msg->length );
    }

    msg->type = (rc==SQLITE_DONE) ? DB_DONE : DB_ERR;
    msg->length = 0;
out:
    write( fd, msg, sizeof(*msg) + msg->length );
    sqlite3_finalize( stmt );
}

static void handle_client( sqlite3* db, int fd )
{
    unsigned char buffer[ DB_MAX_MSG_SIZE ];
    db_msg* msg = (db_msg*)buffer;

    while( wait_for_fd( fd, TIMEOUT_MS ) )
    {
        if( read( fd, msg, sizeof(*msg) ) != sizeof(*msg) )
            break;

        if( msg->length != 0 )
        {
            if( (msg->length + sizeof(*msg)) > sizeof(buffer) )
                goto err;
            if( read( fd, msg->payload, msg->length ) != msg->length )
                break;
        }

#ifdef HAVE_SESSION
        if( msg->type >= DB_SESSION_MIN && msg->type <= DB_SESSION_MAX )
        {
            if( !handle_session_message( fd, msg ) )
                goto err;
            continue;
        }
#endif

        switch( msg->type )
        {
        case DB_GET_OBJECTS:
            get_objects( db, fd );
            goto out;
        case DB_QUIT:
            goto out;
        default:
            WARN("unknown request (ID=%d) received", msg->type);
            goto err;
        }
    }
out:
    close( fd );
    return;
err:
    msg->type = DB_ERR;
    msg->length = 0;
    write( fd, msg, sizeof(*msg) );
    goto out;
}

static void sighandler( int sig )
{
    if( sig == SIGTERM || sig == SIGINT )
        run = 0;
    if( sig == SIGCHLD )
    {
        while( waitpid( -1, NULL, WNOHANG )!=-1 ) { }
    }
    if( sig == SIGSEGV )
    {
        CRITICAL("SEGFAULT!!");
        exit(EXIT_FAILURE);
    }
}

static void usage( int status )
{
    fputs( "Usage: rdb --db <dbfile> --sock <unixsocket> [--log <file>]\n"
           "           [--loglevel <num>]\n\n"
           "  -d, --db           The SQLite data base file to get data from\n"
           "  -s, --sock         Unix socket to listen on\n"
           "  -f, --log          Append log output to a specific file\n"
           "  -l, --loglevel     Higher value means more verbose\n",
           status==EXIT_FAILURE ? stderr : stdout );
    exit(status);
}

int main( int argc, char** argv )
{
    const char *sockfile = NULL, *dbfile = NULL, *logfile = NULL;
    int i, j, fd, loglevel = LEVEL_WARNING, ret = EXIT_FAILURE;
    struct sigaction act;
    struct pollfd pfd;

    for( i=1; i<argc; ++i )
    {
        if( !strcmp(argv[i], "-h") || !strcmp(argv[i], "--help") )
            usage(EXIT_SUCCESS);
    }

    while( (i=getopt_long(argc,argv,"d:s:f:l:",options,NULL)) != -1 )
    {
        switch( i )
        {
        case 'd': dbfile   = optarg; break;
        case 's': sockfile = optarg; break;
        case 'f': logfile  = optarg; break;
        case 'l':
            for( loglevel=0, j=0; optarg[j]; ++j )
                loglevel = loglevel * 10 + (optarg[j] - '0');
            if( optarg[j] )
                goto fail_num;
            break;
        default:
            usage(EXIT_FAILURE);
        }
    }

    if( optind < argc )
    {
        WARN( "unknown extra arguments" );
        usage(EXIT_FAILURE);
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
#ifdef HAVE_SESSION
    if( !sesion_init( ) )
    {
        CRITICAL( "Cannot initialize session store!" );
        goto out;
    }
#endif
    /* create server socket */
    pfd.fd = create_socket( sockfile, 0, AF_UNIX );
    pfd.events = POLLIN;

    if( pfd.fd <= 0 )
        return EXIT_FAILURE;

    chmod( sockfile, 0770 );

    /* hook signal handlers */
    memset( &act, 0, sizeof(act) );
    act.sa_handler = sighandler;
    sigaction( SIGTERM, &act, NULL );
    sigaction( SIGINT, &act, NULL );
    sigaction( SIGCHLD, &act, NULL );
    sigaction( SIGSEGV, &act, NULL );
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
#ifdef HAVE_SESSION
    session_cleanup( );
#endif
    return ret;
fail_num:
    fprintf(stderr, "Expected numeric argument, found '%s'\n", optarg);
fail:
    fprintf(stderr, "Try '%s --help' for more information\n\n", argv[0]);
    goto out;
}

