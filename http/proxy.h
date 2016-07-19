#ifndef PROXY_H
#define PROXY_H

#include "http.h"
#include "conf.h"
#include "sock.h"

int proxy_handle_request(sock_t* sock, const cfg_host* h,
                         const http_request* req);

#endif /* PROXY_H */

