#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>

#include "error.h"
#include "rest.h"
#include "sock.h"
#include "json.h"
#include "str.h"
#include "rdb.h"



static int echo_demo( int fd, const cfg_host* h, http_request* req );
static int form_get( int fd, const cfg_host* h, http_request* req );
static int form_post( int fd, const cfg_host* h, http_request* req );
static int cookie_get( int fd, const cfg_host* h, http_request* req );
static int inf_get( int fd, const cfg_host* h, http_request* req );
static int table_post( int fd, const cfg_host* h, http_request* req );
static int json_get( int fd, const cfg_host* h, http_request* req );
static int redirect( int fd, const cfg_host* h, http_request* req );



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
    {HTTP_POST,"table", NULL,"application/x-www-form-urlencoded",table_post},
    {HTTP_GET, "json",  NULL,NULL,                               json_get  },
    {-1,       "redir", NULL,NULL,                               redirect  },
};



int rest_handle_request( int fd, const cfg_host* h, http_request* req )
{
    int error = ERR_NOT_FOUND;
    size_t i, len;

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
        if( restmap[i].accept && strcmp(req->type, restmap[i].accept) )
            continue;

        if( req->path[len] )
            req->path += len + 1;

        return restmap[i].callback( fd, h, req );
    }

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
    info.type = "text/html";
    info.size = page->used;
    info.flags = FLAG_DYNAMIC;
    http_ok( fd, &info, setcookies );
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

static int table_post( int fd, const cfg_host* h, http_request* req )
{
    char buffer[ 512 ];
    const char* query;
    int count, db;
    string page;
    db_msg msg;
    double dbl;
    long l;
    (void)h;

    if( req->length > (sizeof(buffer)-1) )
        return ERR_SIZE;

    read( fd, buffer, req->length );
    buffer[ req->length ] = '\0';

    count = http_split_args( buffer );
    query = http_get_arg( buffer, count, "query" );

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
        msg.type = DB_QUERY_HDR;
        msg.length = strlen(query);
        write( db, &msg, sizeof(msg) );
        write( db, query, msg.length );
        count = 0;

        string_append( &page, "<table>" );
        string_append( &page, "<tr>" );

        while( read( db, &msg, sizeof(msg) )==sizeof(msg) )
        {
            switch( msg.type )
            {
            case DB_ROW_DONE:
                ++count;
                string_append( &page, "</tr><tr>" );
                continue;
            case DB_COL_INT:
                read( db, &l, sizeof(long) );
                sprintf( buffer, "%ld", l );
                break;
            case DB_COL_DBL:
                read( db, &dbl, sizeof(double) );
                sprintf( buffer, "%f", dbl );
                break;
            case DB_COL_BLOB:
                strcpy( buffer, "<b>BLOB</b>" );
                break;
            case DB_COL_TEXT:
                if( msg.length > sizeof(buffer) )
                    goto out;
                read( db, buffer, msg.length );
                break;
            case DB_COL_NULL:
                strcpy( buffer, "<b>NULL</b>" );
            default:
                goto out;
            }

            string_append( &page, count ? "<td>" : "<th>" );
            string_append( &page, buffer );
            string_append( &page, count ? "</td>" : "</th>" );
        }
    out:
        string_append( &page, "</table>" );

        msg.type = DB_QUIT;
        msg.length = 0;
        write( db, &msg, sizeof(msg) );
        close( db );
    }

    string_append( &page, "</body></html>" );

    send_page_buffer( &page, fd, req, NULL );
    string_cleanup( &page );
    return 0;
}

static int redirect( int fd, const cfg_host* h, http_request* req )
{
    static const char* page = "<html><body>Redirecting to "
                              "<a href=\"/Lenna.png\">here</a>."
                              "</body></html>";
    (void)req;
    (void)h;
    http_redirect( fd, "/Lenna.png", REDIR_FORCE_GET, strlen(page) );
    write( fd, page, strlen(page) );
    return 0;
}

/****************************************************************************/

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

