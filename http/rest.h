#ifndef REST_H
#define REST_H

#include "http.h"
#include "conf.h"
#include "sock.h"

/*
    Try to handle a request for the REST API. Returns 0 on success or an
    error code (ERR_*) on failure.
 */
int rest_handle_request( sock_t* sock, const cfg_host* h, http_request* req );

#endif /* REST_H */

