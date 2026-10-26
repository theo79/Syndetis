#pragma once
#include <stdint.h>
static inline uint32_t lwip_ntohl(uint32_t x){return (x>>24)|((x>>8)&0xff00)|((x<<8)&0xff0000)|(x<<24);}
#define lwip_htonl lwip_ntohl
