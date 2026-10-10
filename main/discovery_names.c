#include <errno.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "network_status.h"
#include "firmware_update.h"
#include "diagnostic.h"
#include "discovery.h"
#include "discovery_name.h"
#include "ptr_dns.h"

/* Single existing worker owns all sockets and this scratch. Closing each socket
 * at expiry/cancellation removes the receive queue: no late callback lifetime. */
static uint8_t packet[PTR_DNS_PACKET_SIZE];
typedef struct { int fd; uint32_t ip; uint16_t id; int64_t deadline; } name_request_t;
static bool name_current(uint32_t token,uint32_t revision)
{
    network_status_t n; ota_status_t ota;
    network_status_get(&n); firmware_update_status(&ota);
    return diagnostic_current(token) && n.link_up && n.ipv4_ready && n.revision==revision &&
           ota.phase!=OTA_RECEIVING && ota.phase!=OTA_SUCCESS;
}
discovery_hostname_status_t discovery_names_run(uint32_t token, uint32_t revision, unsigned count)
{
    discovery_hostname_status_t metrics={0};
    if (!count) return metrics;
    int64_t start=esp_timer_get_time(), deadline=start+DISCOVERY_NAME_BUDGET_MS*1000;
    network_status_t n;
    /* Reuse existing bounded, TCP/IP-thread-safe DNS configuration inspection.
     * Its <=1 second wait is INCLUDED in the global 2500 ms budget. */
    network_status_inspect(&n);
    uint32_t server=0;
    for (unsigned i=0;i<2 && !server;++i) {
        uint32_t ip=lwip_ntohl(n.dns[i].addr);
        if (ip && (ip>>24)>0 && (ip>>24)<224 && (ip>>24)!=127) server=ip;
    }
    if (!name_current(token,revision)) metrics.cancelled=true;
    if (!server || metrics.cancelled) {
        metrics.duration_ms=(esp_timer_get_time()-start)/1000; return metrics;
    }
    name_request_t requests[DISCOVERY_NAME_CONCURRENCY];
    for (unsigned i=0;i<DISCOVERY_NAME_CONCURRENCY;++i) requests[i]=(name_request_t){.fd=-1};
    unsigned next=0, limit=count<DISCOVERY_NAME_MAX_ATTEMPTS ? count:DISCOVERY_NAME_MAX_ATTEMPTS;
    int64_t next_send=start;
    while (esp_timer_get_time()<deadline) {
        if (!name_current(token,revision)) { metrics.cancelled=true; break; }
        int64_t now=esp_timer_get_time();
        unsigned busy=0;
        for (unsigned i=0;i<DISCOVERY_NAME_CONCURRENCY;++i) {
            name_request_t *r=&requests[i];
            if (r->fd>=0 && now>=r->deadline) { ++metrics.timeout; close(r->fd); r->fd=-1; }
            if (r->fd>=0) {
                char hostname[64];
                int bytes=recv(r->fd,packet,sizeof(packet),MSG_DONTWAIT);
                if (bytes>=0) {
                    ptr_dns_result_t result=ptr_dns_response(packet,(size_t)bytes,r->id,r->ip,hostname);
                    if (result!=PTR_IGNORE) {
                        if (result==PTR_FOUND && esp_timer_get_time()<deadline &&
                            name_current(token,revision) && discovery_hostname_result(r->ip,hostname,token,revision)) ++metrics.resolved;
                        else ++metrics.failed;
                        close(r->fd); r->fd=-1;
                    }
                } else if (errno!=EAGAIN && errno!=EWOULDBLOCK) {
                    ++metrics.failed; close(r->fd); r->fd=-1;
                }
            }
            if (r->fd<0 && next<limit && now>=next_send && now<deadline) {
                discovery_device_t host;
                if (!discovery_device(next++,&host)) { metrics.cancelled=true; break; }
                ++metrics.attempted;
                r->ip=host.ip; r->id=(uint16_t)esp_random();
                r->fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
                if (r->fd>=0) {
                    struct sockaddr_in local={.sin_family=AF_INET,.sin_addr={.s_addr=n.ipv4.ip.addr}};
                    struct sockaddr_in remote={.sin_family=AF_INET,.sin_port=htons(53),.sin_addr={.s_addr=lwip_htonl(server)}};
                    int flags=1;
                    size_t bytes=ptr_dns_query(packet,sizeof(packet),r->id,r->ip);
                    if (ioctl(r->fd,FIONBIO,&flags)!=0 || bind(r->fd,(struct sockaddr *)&local,sizeof(local))!=0 ||
                        connect(r->fd,(struct sockaddr *)&remote,sizeof(remote))!=0 ||
                        !name_current(token,revision) || send(r->fd,packet,bytes,0)!=(int)bytes) {
                        close(r->fd); r->fd=-1;
                    } else {
                        ++metrics.packets_sent;
                        r->deadline=now+DISCOVERY_NAME_TIMEOUT_MS*1000;
                        next_send=now+DISCOVERY_NAME_PACE_MS*1000;
                    }
                }
                /* Setup/send failures count as failed attempts without packets. */
                if (r->fd<0) { ++metrics.failed; next_send=now+DISCOVERY_NAME_PACE_MS*1000; }
            }
            if (r->fd>=0) ++busy;
        }
        if (metrics.cancelled || (!busy && next>=limit)) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    for (unsigned i=0;i<DISCOVERY_NAME_CONCURRENCY;++i) if (requests[i].fd>=0) {
        if (metrics.cancelled) ++metrics.failed; else ++metrics.timeout;
        close(requests[i].fd);
    }
    metrics.duration_ms=(esp_timer_get_time()-start)/1000;
    return metrics;
}
