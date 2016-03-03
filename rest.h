#ifndef REST_H
#define REST_H

#include "http.h"
#include "conf.h"

/*
    Try to handle a request for the REST API. Returns 0 on success or an
    error code (ERR_*) on failure.
 */
int rest_handle_request( int fd, const cfg_host* h, http_request* req );

#endif /* REST_H */

