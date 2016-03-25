#ifndef PROXY_H
#define PROXY_H

#include "http.h"
#include "conf.h"

int proxy_handle_request( int fd, const cfg_host* h, http_request* req );

#endif /* PROXY_H */

