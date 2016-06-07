#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>

#include "config.h"
#include "error.h"
#include "rest.h"
#include "sock.h"
#include "json.h"
#include "user.h"
#include "str.h"
#include "rdb.h"



#ifdef HAVE_REST
static int echo_demo( int fd, const cfg_host* h, http_request* req );
static int form_get( int fd, const cfg_host* h, http_request* req );
static int form_post( int fd, const cfg_host* h, http_request* req );
static int cookie_get( int fd, const cfg_host* h, http_request* req );
static int inf_get( int fd, const cfg_host* h, http_request* req );
static int table_get( int fd, const cfg_host* h, http_request* req );
static int redirect( int fd, const cfg_host* h, http_request* req );
static int sess_get( int fd, const cfg_host* h, http_request* req );
static int sess_start( int fd, const cfg_host* h, http_request* req );
static int sess_end( int fd, const cfg_host* h, http_request* req );

#ifdef JSON_SERIALIZER
    static int json_get( int fd, const cfg_host* h, http_request* req );
#endif



static const struct
{
    int method;             /* method to map to, negative value for all */
    const char* path;       /* sub-path of request to map to */
    const char* host;       /* if set, only allow for this requested host */
    const char* accept;     /* content type that is accepted */

    int(* callback )( int fd, const cfg_host* h, http_request* req );
}
restmap[] =
{
    {-1,       "echo",  NULL,NULL,                               echo_demo },
    {HTTP_GET, "form",  NULL,NULL,                               form_get  },
    {HTTP_POST,"form",  NULL,"application/x-www-form-urlencoded",form_post },
    {HTTP_GET, "cookie",NULL,NULL,                               cookie_get},
    {HTTP_GET, "inf",   NULL,NULL,                               inf_get   },
    {HTTP_GET, "table", NULL,NULL,                               table_get },
    {HTTP_GET, "sess",  NULL,NULL,                               sess_get  },
    {HTTP_POST,"login", NULL,"application/x-www-form-urlencoded",sess_start},
    {HTTP_GET, "logout",NULL,NULL,                               sess_end  },
#ifdef JSON_SERIALIZER
    {HTTP_GET, "json",  NULL,NULL,                               json_get  },
#endif
    {-1,       "redir", NULL,NULL,                               redirect  },
};



int rest_handle_request( int fd, const cfg_host* h, http_request* req )
{
    int error = ERR_NOT_FOUND;
    size_t i, len;

    len = strlen(h->restdir);

    if( strncmp(req->path, h->restdir, len) )
        goto out;
    if( req->path[len] && req->path[len]!='/' )
        goto out;

    for( req->path+=len; req->path[0]=='/'; ++req->path ) { }

    for( i=0; i<sizeof(restmap)/sizeof(restmap[0]); ++i )
    {
        if( restmap[i].host && strcmp(req->host, restmap[i].host) )
            continue;

        len = strlen(restmap[i].path);

        if( strncmp(req->path, restmap[i].path, len) )
            continue;

        if( req->path[len] && req->path[len]!='/' )
            continue;

        error = ERR_METHOD;
        if( restmap[i].method>=0 && req->method != restmap[i].method )
            continue;

        error = ERR_TYPE;
        if( restmap[i].accept && !req->type )
            continue;
        if( restmap[i].accept && strcmp(req->type, restmap[i].accept) )
            continue;

        if( req->path[len] )
            req->path += len + 1;

        return restmap[i].callback( fd, h, req );
    }
out:
    return error;
}

static void send_page_buffer( string* page, int fd, const http_request* req,
                              const char* setcookies )
{
    http_file_info info;

    memset( &info, 0, sizeof(info) );

    if( req->accept & (ENC_DEFLATE|ENC_GZIP) )
    {
        if( string_compress( page, !(req->accept & ENC_DEFLATE) ) )
            info.encoding = (req->accept & ENC_DEFLATE) ? "deflate" : "gzip";
    }

    info.last_mod = time(0);
    info.type = "text/html; charset=utf-8";
    info.size = page->used;
    info.flags = FLAG_DYNAMIC;
    info.setcookies = setcookies;
    http_response_header( fd, &info );
    write( fd, page->data, page->used );
}

/****************************************************************************/

#define ECHO_METHOD 1
#define ECHO_PATH 2
#define ECHO_HOST 3
#define FORM_STR1 4
#define FORM_STR2 5
#define FORM_COOKIE 6

static const template_map echo_attr[] =
{
    {"$METHOD", ECHO_METHOD},
    {"$PATH",   ECHO_PATH  },
    {"$HOST",   ECHO_HOST  },
    {"$STR1",   FORM_STR1  },
    {"$STR2",   FORM_STR2  },
    {"$COOKIE", FORM_COOKIE},
};

static int echo_demo( int fd, const cfg_host* h, http_request* req )
{
    const char* method = "-unknown-";
    int ret, file;
    string page;
    size_t len;

    switch( req->method )
    {
    case HTTP_GET:    method = "GET"; break;
    case HTTP_HEAD:   method = "HEAD"; break;
    case HTTP_POST:   method = "POST"; break;
    case HTTP_PUT:    method = "PUT"; break;
    case HTTP_DELETE: method = "DELETE"; break;
    }

    file = openat( h->tpldir, "echo.tpl", O_RDONLY );
    if( file<0 )
        return ERR_INTERNAL;
    string_init( &page );

    len = sizeof(echo_attr)/sizeof(echo_attr[0]);

    while( (ret = string_process_template( &page, file, echo_attr, len ))!=0 )
    {
        switch( ret )
        {
        case ECHO_METHOD: string_append( &page, method    ); break;
        case ECHO_PATH:   string_append( &page, req->path ); break;
        case ECHO_HOST:   string_append( &page, req->host ); break;
        default:
            close( file );
            string_cleanup( &page );
            return ERR_INTERNAL;
        }
    }

    close( file );

    send_page_buffer( &page, fd, req, NULL );
    string_cleanup( &page );
    return 0;
}

static int form_get( int fd, const cfg_host* h, http_request* req )
{
    const char *first, *second;
    int ret, file;
    string page;
    size_t len;

    first = http_get_arg( req->getargs, req->numargs, "str1" );
    second = http_get_arg( req->getargs, req->numargs, "str2" );

    file = openat( h->tpldir, "form.tpl", O_RDONLY );
    if( file<0 )
        return ERR_INTERNAL;
    string_init( &page );

    len = sizeof(echo_attr)/sizeof(echo_attr[0]);

    while( (ret = string_process_template( &page, file, echo_attr, len ))!=0 )
    {
        switch( ret )
        {
        case FORM_STR1: string_append( &page, first  ); break;
        case FORM_STR2: string_append( &page, second ); break;
        default:
            close( file );
            string_cleanup( &page );
            return ERR_INTERNAL;
        }
    }

    close( file );
    send_page_buffer( &page, fd, req, NULL );
    string_cleanup( &page );
    return 0;
}

static int form_post( int fd, const cfg_host* h, http_request* req )
{
    const char *first, *second;
    char buffer[128];
    int ret, file;
    string page;
    size_t len;
    int count;

    if( req->length > (sizeof(buffer)-1) )
        return ERR_SIZE;

    read( fd, buffer, req->length );
    buffer[ req->length ] = '\0';

    count = http_split_args( buffer );
    first = http_get_arg( buffer, count, "str1" );
    second = http_get_arg( buffer, count, "str2" );

    file = openat( h->tpldir, "form.tpl", O_RDONLY );
    if( file<0 )
        return ERR_INTERNAL;
    string_init( &page );

    len = sizeof(echo_attr)/sizeof(echo_attr[0]);

    while( (ret = string_process_template( &page, file, echo_attr, len ))!=0 )
    {
        switch( ret )
        {
        case FORM_STR1: string_append( &page, first  ); break;
        case FORM_STR2: string_append( &page, second ); break;
        default:
            close( file );
            string_cleanup( &page );
            return ERR_INTERNAL;
        }
    }

    close( file );
    send_page_buffer( &page, fd, req, NULL );
    string_cleanup( &page );
    return 0;
}

static int cookie_get( int fd, const cfg_host* h, http_request* req )
{
    const char *getarg, *value;
    char cookiebuffer[ 512 ];
    int len, ret, file = -1;
    string page;

    value = http_get_arg( req->cookies, req->numcookies, "magic" );
    getarg = http_get_arg( req->getargs, req->numargs, "str1" );

    if( getarg )
        file = openat( h->tpldir, "cookie_ch.tpl", O_RDONLY );
    else if( value )
        file = openat( h->tpldir, "cookie_show.tpl", O_RDONLY );
    else
        file = openat( h->tpldir, "cookie_set.tpl", O_RDONLY );

    if( file < 0 )
        return ERR_INTERNAL;

    if( getarg )
        sprintf( cookiebuffer, "magic=%s", getarg );

    string_init( &page );
    len = sizeof(echo_attr)/sizeof(echo_attr[0]);

    while( (ret = string_process_template( &page, file, echo_attr, len ))!=0 )
    {
        switch( ret )
        {
        case FORM_STR1:   string_append( &page, getarg ); break;
        case FORM_COOKIE: string_append( &page, value  ); break;
        default:
            close( file );
            string_cleanup( &page );
            return ERR_INTERNAL;
        }
    }

    close( file );
    send_page_buffer( &page, fd, req, getarg ? cookiebuffer : NULL );
    string_cleanup( &page );
    return 0;
}

static int inf_get( int fd, const cfg_host* h, http_request* req )
{
    (void)fd; (void)req; (void)h;
    while( 1 ) { }
    return 0;
}

static int table_get( int fd, const cfg_host* h, http_request* req )
{
    char buffer[ sizeof(db_msg) + sizeof(db_object) ];
    db_msg* msg = (db_msg*)buffer;
    db_object* obj = (db_object*)msg->payload;
    string page;
    int db;
    (void)h;

    string_init( &page );
    string_append( &page, "<html><head><title>Database</title></head>" );
    string_append( &page, "<body><h1>Database Tabe</h1>" );

    db = connect_to( "/tmp/rdb", 0, AF_UNIX );

    if( db<0 )
    {
        string_append( &page, "<b>Connection Failed</b><br>" );
    }
    else
    {
        msg->type = DB_GET_OBJECTS;
        msg->length = 0;
        write( db, msg, sizeof(*msg) );

        string_append( &page, "<table>\n<tr><th>Name</th><th>Color</th>"
                              "<th>Value</th></tr>\n" );

        while( read( db, buffer, sizeof(buffer) ) == sizeof(buffer) )
        {
            if( msg->type != DB_OBJECT || msg->length != sizeof(*obj) )
                break;

            string_append( &page, "<tr><td>" );
            string_append( &page, obj->name );
            string_append( &page, "</td><td>" );
            string_append( &page, obj->color );
            string_append( &page, "</td><td>" );
            sprintf( buffer, "%ld", obj->value );
            string_append( &page, buffer );
            string_append( &page, "</td></tr>\n" );
        }

        string_append( &page, "</table>\n" );
        close( db );
    }

    string_append( &page, "</body></html>" );

    send_page_buffer( &page, fd, req, NULL );
    string_cleanup( &page );
    return 0;
}

static int redirect( int fd, const cfg_host* h, http_request* req )
{
    http_file_info info;
    string page;
    int ret;
    (void)h;
    ret = gen_default_page(&page, &info, ERR_REDIRECT_GET,
                           req->accept, "/Lenna.png");
    if( !ret )
        return ERR_INTERNAL;

    http_response_header( fd, &info );
    write( fd, page.data, page.used );
    string_cleanup( &page );
    return 0;
}

static int sess_get( int fd, const cfg_host* h, http_request* req )
{
    uint32_t sid = 0, uid;
    db_session_data data;
    size_t i, count;
    char buffer[32];
    struct tm stm;
    int db, ret;
    string page;
    db_msg msg;
    (void)h;

    sid = user_get_session_cookie( req );

    string_init( &page );
    string_append( &page, "<html><head><title>Session</title></head>\n" );
    string_append( &page, "<body><h1>Session Management</h1>\n" );

    db = connect_to( "/tmp/rdb", 0, AF_UNIX );
    if( db < 0 )
        goto fail;

    ret = user_get_session_data( db, &data, sid );
    if( ret < 0 )
        goto fail;
    if( ret == 0 )
        sid = 0;

    msg.type = DB_SESSION_LIST;
    msg.length = 0;
    if( write( db, &msg, sizeof(msg) )!=sizeof(msg) ) goto fail;
    if( read( db, &msg, sizeof(msg) )!=sizeof(msg) ) goto fail;
    if( msg.type != DB_SESSION_LIST ) goto fail;
    if( msg.length % sizeof(uint32_t) ) goto fail;

    string_append( &page, "<table>\n<tr><th>UID</th></tr>\n" );
    count = msg.length / sizeof(uint32_t);

    for( i=0; i<count; ++i )
    {
        read( db, &uid, sizeof(uid) );

        sprintf( buffer, "%u", (unsigned int)uid );

        string_append( &page, "<tr><td>" );
        string_append( &page, buffer );
        string_append( &page, "</td><td>\n" );
    }

    string_append( &page, "</table>\n<hr>\n" );

    if( sid )
    {
        string_append( &page, "Your session ID: " );
        sprintf( buffer, "%u", (unsigned int)sid );
        string_append( &page, buffer );
        string_append( &page, "<br>\nYour user ID: " );
        sprintf( buffer, "%u", (unsigned int)data.uid );
        string_append( &page, buffer );
        string_append( &page, "<br>\nLast active: " );

        localtime_r( &data.atime, &stm );
        strftime( buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S %Z", &stm );
        string_append( &page, buffer );

        string_append( &page, "<br>\n<a href=\"/rest/logout\">Logout</a>\n" );
    }
    else
    {
        string_append( &page, "You are not logged on.\n" );
        string_append( &page, "<a href=\"/login.html\">Login</a>\n" );
    }

    msg.type = DB_QUIT;
    msg.length = 0;
    write( db, &msg, sizeof(msg) );
out:
    close( db );
    string_append( &page, "</body></html>" );
    send_page_buffer( &page, fd, req, NULL );
    string_cleanup( &page );
    return 0;
fail:
    string_append( &page, "<b>Database Error</b><br>\n" );
    goto out;
}

static int sess_start( int fd, const cfg_host* h, http_request* req )
{
    db_session_data data;
    const char* uidstr;
    int count, db, ret;
    char buffer[128];
    uint32_t uid;
    string page;
    db_msg msg;
    char* end;
    (void)h;

    string_init( &page );
    string_append( &page, "<html><head><title>Login</title></head><body>" );
    string_append( &page, "<h1>Login</h1>\n" );

    /* get UID */
    if( req->length > (sizeof(buffer)-1) )
        return ERR_SIZE;

    read( fd, buffer, req->length );
    buffer[ req->length ] = '\0';

    count = http_split_args( buffer );
    uidstr = http_get_arg( buffer, count, "uid" );
    if( !uidstr || !isdigit(*uidstr) )
        goto nouid;

    uid = strtol( uidstr, &end, 10 );
    if( !end || *end )
        goto nouid;

    /* create session */
    db = connect_to( "/tmp/rdb", 0, AF_UNIX );
    if( db < 0 )
        goto dberr;

    ret = user_create_session( db, &data, uid );
    if( ret <= 0 )
        goto dberr;

    string_append(&page, "Successfully logged in.<br>");
    string_append(&page, "<a href=\"/rest/sess\">go back</a></body></html>");

    msg.type = DB_QUIT;
    msg.length = 0;
    write( db, &msg, sizeof(msg) );
    close( db );

    user_print_session_cookie( buffer, sizeof(buffer), data.sid );
    send_page_buffer( &page, fd, req, buffer );
    string_cleanup( &page );
    return 0;
dberr:
    string_append( &page, "Database Error!" );
    string_append( &page, "</body></html>" );
    send_page_buffer( &page, fd, req, NULL );
    string_cleanup( &page );

    if( db >= 0 )
    {
        msg.type = DB_QUIT;
        msg.length = 0;
        write( db, &msg, sizeof(msg) );
        close( db );
    }
    return 0;
nouid:
    string_append( &page, "Error: UID must be a positive number!" );
    string_append( &page, "</body></html>" );
    send_page_buffer( &page, fd, req, NULL );
    string_cleanup( &page );
    return 0;
}

static int sess_end( int fd, const cfg_host* h, http_request* req )
{
    char buffer[128];
    uint32_t sid;
    string page;
    db_msg msg;
    int db;
    (void)h;

    sid = user_get_session_cookie( req );

    if( sid )
    {
        db = connect_to( "/tmp/rdb", 0, AF_UNIX );
        if( db > 0 )
        {
            user_destroy_session( db, sid );

            msg.type = DB_QUIT;
            msg.length = 0;
            write( db, &msg, sizeof(msg) );
        }
    }

    user_print_session_cookie( buffer, sizeof(buffer), 0 );

    string_init( &page );
    string_append(&page, "<html><head><title>Logout</title></head><body>"  );
    string_append(&page, "<h1>Logout</h1>You have been logged out.<br>\n"  );
    string_append(&page, "<a href=\"/rest/sess\">go back</a></body></html>");
    send_page_buffer( &page, fd, req, buffer );
    string_cleanup( &page );
    return 0;
}

/****************************************************************************/
#ifdef JSON_SERIALIZER
typedef struct node
{
    struct node* left;
    struct node* right;
    int isLeaf;
    int value;
    const char* tag;
}
node_t;

JSON_BEGIN( node_t )
    JSON_INT( node_t, value ),
    JSON_BOOL( node_t, isLeaf ),
    JSON_STRING( node_t, tag ),
    JSON_OBJ( node_t, left, node_t ),
    JSON_OBJ( node_t, right, node_t )
JSON_END( node_t );

static int json_get( int fd, const cfg_host* h, http_request* req )
{
    node_t a, b, c;
    string str;
    (void)req;
    (void)h;

    if( !string_init( &str ) )
        return ERR_INTERNAL;

    a.left = &b;
    a.right = &c;
    a.isLeaf = 0;
    a.value = 1337;
    a.tag = "Hello World";

    b.left = b.right = NULL;
    b.isLeaf = 1;
    b.value = 42;
    b.tag = "Another node in the tree";

    c.left = c.right = NULL;
    c.isLeaf = 1;
    c.value = 69;
    c.tag = "These are automatically serialized C structs";

    if( !json_serialize( &str, &a, &JSON_DESC( node_t ) ) )
    {
        string_cleanup( &str );
        return ERR_INTERNAL;
    }

    send_page_buffer( &str, fd, req, NULL );
    string_cleanup( &str );
    return 0;
}
#endif /* JSON_SERIALIZER */
#endif /* HAVE_REST */

