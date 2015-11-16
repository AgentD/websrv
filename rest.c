#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include "rest.h"



static int echo_demo( int fd, const http_request* req );
static int form_get( int fd, const http_request* req );
static int form_post( int fd, const http_request* req );



static const struct
{
    int method;             /* method to map to */
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
    char buffer[1024];
    size_t len;

    strcpy( buffer, "<!DOCTYPE html><html><head><title>echo</title></head>"
                    "<body><table border=\"1\">" );

    strcat( buffer, "<h1>REST API - Echo demo</h1>" );
    strcat( buffer, "<tr><td>Method</td>" );

    switch( req->method )
    {
    case HTTP_GET:    strcat( buffer, "<td>GET</td></tr>"    ); break;
    case HTTP_HEAD:   strcat( buffer, "<td>HEAD</td></tr>"   ); break;
    case HTTP_POST:   strcat( buffer, "<td>POST</td></tr>"   ); break;
    case HTTP_PUT:    strcat( buffer, "<td>PUT</td></tr>"    ); break;
    case HTTP_DELETE: strcat( buffer, "<td>DELETE</td></tr>" ); break;
    }

    strcat( buffer, "<tr><td>Path</td><td>" );
    strcat( buffer, req->path );
    strcat( buffer, "</td></tr>" );

    strcat( buffer, "<tr><td>Host</td><td>" );
    strcat( buffer, req->host );
    strcat( buffer, "</td></tr>" );

    strcat( buffer, "<tr><td>Arguments</td><td>" );
    strcat( buffer, req->getargs );
    strcat( buffer, "</td></tr>" );

    strcat( buffer, "</table></body></html>" );

    len = strlen(buffer);
    http_ok( fd, "text/html", len );
    write( fd, buffer, len );
    return 0;
}

static void print_arg_table( char* buffer, const char* args )
{
    char *ptr, *end;
    int len;

    strcat( buffer, "<table border=\"1\">" );

    ptr = strstr( args, "str1=" );
    if( ptr && (ptr==args || ptr[-1]=='&') )
    {
        ptr += 5;
        end = strchr(ptr, '&');
        len = end ? (end - ptr) : (int)strlen(ptr);

        strcat( buffer, "<tr><td>First argument</td><td>" );
        strncat( buffer, ptr, len );
        strcat( buffer, "</td></tr>" );
    }

    ptr = strstr( args, "str2=" );
    if( ptr && (ptr==args || ptr[-1]=='&') )
    {
        ptr += 5;
        end = strchr(ptr, '&');
        len = end ? (end - ptr) : (int)strlen(ptr);

        strcat( buffer, "<tr><td>Second argument</td><td>" );
        strncat( buffer, ptr, len );
        strcat( buffer, "</td></tr>" );
    }

    strcat( buffer, "</table>" );
}

static int form_get( int fd, const http_request* req )
{
    const char* page =
       "<!DOCTYPE html><html><head><title>form</title></head><body>"
       "<h1>REST API - Form</h1>"
       "<h2>HTTP POST based</h2>"
       "<form method=\"post\">"
       "Enter some text:<br>"
       "<input type=\"text\" name=\"str1\"><br>"
       "Some more text:<br>"
       "<input type=\"text\" name=\"str2\"><br>"
       "<input type=\"submit\" value=\"Ok\">"
       "</form>"
       "<h2>HTTP GET based</h2>"
       "<form method=\"get\">"
       "Enter some text:<br>"
       "<input type=\"text\" name=\"str1\"><br>"
       "Some more text:<br>"
       "<input type=\"text\" name=\"str2\"><br>"
       "<input type=\"submit\" value=\"Ok\">"
       "</form></body></html>";
    char buffer[1024];
    size_t len = strlen(page);

    if( req->getargs )
    {
        if( strlen(req->getargs) > sizeof(buffer) )
            return ERR_INTERNAL;

        strcpy(buffer,
               "<!DOCTYPE html><html><head><title>form</title></head><body>");
        strcat(buffer, "<h1>GET arguments</h1>");
        print_arg_table(buffer+strlen(buffer), req->getargs);

        len = strlen(buffer);
        http_ok( fd, "text/html", len );
        write( fd, buffer, len );
    }
    else
    {
        len = strlen(page);
        http_ok( fd, "text/html", len );
        write( fd, page, len );
    }
    return 0;
}

static int form_post( int fd, const http_request* req )
{
    char buffer[128], page[1024];
    size_t len;

    if( req->length > (sizeof(buffer)-1) )
        return ERR_SIZE;

    read( fd, buffer, req->length );
    buffer[ req->length ] = '\0';

    strcpy(page,
           "<!DOCTYPE html><html><head><title>form</title></head><body>");
    strcat(page, "<h1>POST arguments</h1>");
    print_arg_table(page+strlen(page), buffer);

    len = strlen(page);
    http_ok( fd, "text/html", len );
    write( fd, page, len );
    return 0;
}

