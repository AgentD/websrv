#ifndef REST_H
#define REST_H

#include "http.h"

/*
    Try to handle a request for the REST API. Returns 0 if an error page
    was sent and the conenction should be terminated.
 */
int rest_handle_request( int fd, const http_request* req );

#endif /* REST_H */

