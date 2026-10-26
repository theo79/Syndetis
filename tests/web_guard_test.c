#include <stdio.h>
#include <stdlib.h>
#include "../main/web_guard.c"
static uint32_t local_ip,peer_ip;
static int failure;
int httpd_req_to_sockfd(httpd_req_t*r){(void)r;return 3;}
int test_getsockname(int fd,struct sockaddr*a,socklen_t*l){(void)fd;(void)l;struct sockaddr_in*s=(void*)a;s->sin_family=AF_INET;s->sin_addr.s_addr=lwip_ntohl(local_ip);return failure;}
int test_getpeername(int fd,struct sockaddr*a,socklen_t*l){(void)fd;(void)l;struct sockaddr_in*s=(void*)a;s->sin_family=AF_INET;s->sin_addr.s_addr=lwip_ntohl(peer_ip);return failure;}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d\n",__LINE__);exit(1);}}while(0)
int main(void){httpd_req_t r={0};local_ip=0xc0a80401;peer_ip=0xc0a80402;CHECK(dashboard_hotspot_request(&r));
local_ip=0xc000020a;CHECK(!dashboard_hotspot_request(&r));local_ip=0xc0a80401;peer_ip=0xc0000211;CHECK(!dashboard_hotspot_request(&r));
peer_ip=0xc0a80402;failure=1;CHECK(!dashboard_hotspot_request(&r));puts("PASS: actual socket guard accepts AP local address and management peer only, rejects Ethernet addresses and socket lookup failures");return 0;}
