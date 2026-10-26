#pragma once
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif
#ifndef EADDRNOTAVAIL
#define EADDRNOTAVAIL 2001
#endif
#ifndef ENETDOWN
#define ENETDOWN 2002
#endif
typedef unsigned socklen_t;
struct in_addr { uint32_t s_addr; };
struct sockaddr { unsigned short sa_family; char data[14]; };
struct sockaddr_in { unsigned short sin_family,sin_port; struct in_addr sin_addr; };
struct ifreq { char ifr_name[8]; };
#define AF_INET 2
#define SOCK_RAW 3
#define IPPROTO_ICMP 1
#define SOL_SOCKET 0xfff
#define SO_BINDTODEVICE 100
#define MSG_DONTWAIT 8
#define socket test_socket
#define setsockopt test_setsockopt
#define bind test_bind
#define sendto test_sendto
#define recvfrom test_recvfrom
#define close test_close
int test_socket(int,int,int);
int test_setsockopt(int,int,int,const void*,socklen_t);
int test_bind(int,const struct sockaddr*,socklen_t);
int test_sendto(int,const void*,size_t,int,const struct sockaddr*,socklen_t);
int test_recvfrom(int,void*,size_t,int,struct sockaddr*,socklen_t*);
int test_close(int);
