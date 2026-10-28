#include "network_policy.h"
#include "ota_http_policy.h"
#include <string.h>

bool dhcp_management_overlap(uint32_t ip, uint32_t mask)
{
    // CIDR ranges overlap when either contains the other's network address.
    return (ip & 0xffffff00u) == 0xc0a80400u ||
           (ip & mask) == (0xc0a80400u & mask);
}

bool dhcp_stack_frame(const uint8_t *f, size_t n)
{
    if (!f || n < 14) return false;
    unsigned type = (unsigned)f[12] << 8 | f[13];
    if (type == 0x0806) return n >= 42; // ARP; stack validates its fields.
    // Tagged discovery remains supported; tagged IP is deliberately not a new feature.
    if (type != 0x0800 || n < 34 || (f[14] >> 4) != 4) return false;
    size_t ihl = (f[14] & 15u) * 4u;
    if (ihl < 20 || n < 14 + ihl + 8 || f[23] != 17) return false;
    if ((f[20] & 0x3f) || f[21]) return false; // Reject MF and fragment offsets.
    const uint8_t *udp = f + 14 + ihl;
    return udp[0] == 0 && udp[1] == 67 && udp[2] == 0 && udp[3] == 68;
}

bool dhcp_browser_request(const char *host, const char *origin,
                          const char *type, size_t length)
{
    return host && origin && type && !strcmp(host, "192.168.4.1") &&
        !strcmp(origin, "http://192.168.4.1") &&
        !strcmp(type, "application/json") && length == 2;
}

static uint32_t ipv4_value(const uint8_t *p)
{ return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
bool update_http_frame(const uint8_t *f,size_t n,uint32_t local,uint32_t mask,bool incoming)
{
    if(!f || n<54 || !local || !mask || dhcp_management_overlap(local,mask) ||
       f[12]!=8 || f[13]!=0 || (f[14]>>4)!=4 || f[23]!=6 || (f[20]&0x3f) || f[21]) return false;
    size_t ihl=(f[14]&15u)*4u, total=(size_t)f[16]*256+f[17];
    if(ihl<20 || total<ihl+20 || n<14+total) return false;
    const uint8_t *tcp=f+14+ihl;
    size_t th=(tcp[12]>>4)*4u;
    if(th<20 || total<ihl+th) return false;
    uint32_t src=ipv4_value(f+26),dst=ipv4_value(f+30),peer=incoming?src:dst;
    if((incoming?dst:src)!=local || peer==local || (peer&mask)!=(local&mask) ||
       (peer&~mask)==0 || (peer&~mask)==~mask || peer>=0xe0000000u || (peer>>24)==127) return false;
    unsigned port=incoming?(unsigned)tcp[2]*256+tcp[3]:(unsigned)tcp[0]*256+tcp[1];
    return port==OTA_HTTP_PORT;
}
