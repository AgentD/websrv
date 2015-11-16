#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include "rest.h"
#include "html.h"



static int echo_demo( int fd, const http_request* req );
static int form_get( int fd, const http_request* req );
static int form_post( int fd, const http_request* req );



static const struct
{
    int method;             /* method to map to, negative value for all */
    const char* path;       /* sub-path of request to map to */
    const char* host;       /* if set, only allow for this requested host */
    const char* accept;     /* content type that is accepted */

    int(* callback )( int fd, const http_request* req );
}
restmap[] =
{
    {-1,       "echo",NULL,NULL,                               echo_demo},
    {HTTP_GET, "form",NULL,NULL,                               form_get },
    {HTTP_POST,"form",NULL,"application/x-www-form-urlencoded",form_post},
};



int rest_handle_request( int fd, const http_request* req )
{
    const char* ptr;
    int error;
    size_t i;

    /* path must be absolute */
    error = ERR_FORBIDDEN;

    for( ptr=req->path; ptr; )
    {
        if( ptr[0]=='.' && ptr[1]=='.' && (ptr[2]=='/' || !ptr[2]) )
            goto fail;
        if( ptr[0]=='.' && (ptr[1]=='/' || !ptr[1]) )
            goto fail;
        ptr = strchr( ptr, '/' );
        ptr = ptr ? ptr + 1 : NULL;
    }

    /* find rest callback */
    error = ERR_NOT_FOUND;

    for( i=0; i<sizeof(restmap)/sizeof(restmap[0]); ++i )
    {
        if( restmap[i].host && strcmp(req->host, restmap[i].host) )
            continue;

        if( strcmp(req->path, restmap[i].path) )
            continue;

        error = ERR_METHOD;
        if( restmap[i].method>=0 && req->method != restmap[i].method )
            continue;

        error = ERR_TYPE;
        if( restmap[i].accept && strcmp(req->type, restmap[i].accept) )
            continue;

        error = restmap[i].callback( fd, req );

        if( !error )
            return 1;
    }
fail:
    gen_error_page( fd, error );
    return 0;
}

/****************************************************************************/

static int echo_demo( int fd, const http_request* req )
{
    const char* method = "-unknown-";
    html_page page;

    switch( req->method )
    {
    case HTTP_GET:    method = "GET"; break;
    case HTTP_HEAD:   method = "HEAD"; break;
    case HTTP_POST:   method = "POST"; break;
    case HTTP_PUT:    method = "PUT"; break;
    case HTTP_DELETE: method = "DELETE"; break;
    }

    html_page_init( &page, HTML_4 );
    html_page_begin( &page, "echo", NULL );
    html_append_raw( &page, "<h1>REST API - Echo demo</h1>" );
    html_table_begin( &page, "border: 1px solid black;", STYLE_INLINE );
    html_table_row( &page, 2, "Method", method );
    html_table_row( &page, 2, "Path", req->path );
    html_table_row( &page, 2, "Host", req->host );
    html_table_row( &page, 2, "Arguments", req->getargs );
    html_table_end( &page );
    html_page_end( &page );

    http_ok( fd, "text/html", page.used );
    write( fd, page.data, page.used );
    html_page_cleanup( &page );
    return 0;
}

static void print_arg_table( html_page* page, const char* args )
{
    char buffer[ 128 ];
    char *ptr, *end;
    int len;

    html_table_begin( page, "border: 1px solid black;", STYLE_INLINE );

    ptr = strstr( args, "str1=" );
    if( ptr && (ptr==args || ptr[-1]=='&') )
    {
        ptr += 5;
        end = strchr(ptr, '&');
        len = end ? (end - ptr) : (int)strlen(ptr);

        strncpy( buffer, ptr, len );
        buffer[len] = '\0';

        html_table_row( page, 2, "First Argument", buffer );
    }

    ptr = strstr( args, "str2=" );
    if( ptr && (ptr==args || ptr[-1]=='&') )
    {
        ptr += 5;
        end = strchr(ptr, '&');
        len = end ? (end - ptr) : (int)strlen(ptr);

        strncpy( buffer, ptr, len );
        buffer[len] = '\0';

        html_table_row( page, 2, "Second Argument", buffer );
    }

    html_table_end( page );
}

static void gen_form( html_page* page, int method )
{
    html_form_begin( page, NULL, method );
    html_table_begin( page, NULL, STYLE_NONE );
        html_table_row( page, 0 );
            html_table_element( page );
                html_append_raw( page, "Enter some text:" );
            html_table_end_element( page );
            html_table_element( page );
                html_form_input( page, INP_TEXT, 0, "str1", NULL );
            html_table_end_element( page );
        html_table_end_row( page );
        html_table_row( page, 0 );
            html_table_element( page );
                html_append_raw( page, "Enter some text:" );
            html_table_end_element( page );
            html_table_element( page );
                html_form_input( page, INP_TEXT, 0, "str2", NULL );
            html_table_end_element( page );
        html_table_end_row( page );
        html_table_row( page, 0 );
            html_form_input( page, INP_SUBMIT, 0, NULL, "Ok" );
        html_table_end_row( page );
    html_table_end( page );        
    html_form_end( page );
}

static int form_get( int fd, const http_request* req )
{
    char buffer[1024];
    html_page page;

    html_page_init( &page, HTML_4 );
    html_page_begin( &page, "form", NULL );

    if( req->getargs )
    {
        if( strlen(req->getargs) > sizeof(buffer) )
        {
            html_page_cleanup( &page );
            return ERR_INTERNAL;
        }

        html_append_raw( &page, "<h1>GET arguments</h1>" );
        print_arg_table( &page, req->getargs );
    }
    else
    {
        html_append_raw( &page, "<h1>REST API - Form</h1>" );
        html_append_raw( &page, "<h2>HTTP POST based</h2>" );
        gen_form( &page, HTTP_POST );
        html_append_raw( &page, "<h2>HTTP GET based</h2>" );
        gen_form( &page, HTTP_GET );
        html_form_end( &page );
    }

    html_page_end( &page );
    http_ok( fd, "text/html", page.used );
    write( fd, page.data, page.used );
    html_page_cleanup( &page );
    return 0;
}

static int form_post( int fd, const http_request* req )
{
    char buffer[128];
    html_page page;

    if( req->length > (sizeof(buffer)-1) )
        return ERR_SIZE;

    read( fd, buffer, req->length );
    buffer[ req->length ] = '\0';

    html_page_init( &page, HTML_4 );
    html_page_begin( &page, "form", NULL );
    html_append_raw( &page, "<h1>POST arguments</h1>" );
    print_arg_table( &page, buffer );

    html_page_end( &page );
    http_ok( fd, "text/html", page.used );
    write( fd, page.data, page.used );
    html_page_cleanup( &page );
    return 0;
}

