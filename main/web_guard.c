#include "web_guard.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

bool dashboard_hotspot_request(httpd_req_t *request)
{
    struct sockaddr_in local = {0}, peer = {0};
    socklen_t local_len = sizeof(local), peer_len = sizeof(peer);
    int fd = httpd_req_to_sockfd(request);
    if (getsockname(fd, (struct sockaddr *)&local, &local_len) ||
        getpeername(fd, (struct sockaddr *)&peer, &peer_len)) return false;
    return local.sin_family == AF_INET && peer.sin_family == AF_INET &&
        ntohl(local.sin_addr.s_addr) == 0xc0a80401u &&
        (ntohl(peer.sin_addr.s_addr) & 0xffffff00u) == 0xc0a80400u;
}
