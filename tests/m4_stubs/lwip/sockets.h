#pragma once
#include <stdint.h>
typedef unsigned socklen_t;
struct in_addr {uint32_t s_addr;};
struct sockaddr {unsigned short sa_family;char data[14];};
struct sockaddr_in {unsigned short sin_family,sin_port;struct in_addr sin_addr;};
#define AF_INET 2
#define getsockname test_getsockname
#define getpeername test_getpeername
int test_getsockname(int,struct sockaddr*,socklen_t*);
int test_getpeername(int,struct sockaddr*,socklen_t*);
