#ifndef REST_H
#define REST_H

#include "http.h"

int rest_handle_request( int fd, const http_request* req );

#endif /* REST_H */

