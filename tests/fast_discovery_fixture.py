from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'main/discovery.c').read_text()
callback=s[s.index('static void lookup_arp'):s.index('static const char *arp_step')].replace('static void lookup_arp','constexpr void lookup_arp')
job=s[s.index('static struct {'):s.index('} arp_job;')+len('} arp_job;')].replace('static struct {','struct {')
fixture='''#include "discovery_logic.h"
#include "diagnostic_rules.h"
#include <stddef.h>
#define ARP_TABLE_SIZE 10
#define ERR_IF -1
#define ERR_OK 0
#define portENTER_CRITICAL(x)
#define portEXIT_CRITICAL(x)
using err_t=int;
using u8_t=uint8_t;
struct ip4_addr_t {uint32_t addr;};
struct netif {int index;};
struct eth_addr {uint8_t addr[6];};
constexpr uint32_t lwip_ntohl(uint32_t n) {return n;}
constexpr uint32_t lwip_htonl(uint32_t n) {return n;}
constexpr uint32_t ip4_addr_get_u32(ip4_addr_t *ip) {return ip->addr;}
constexpr void ip4_addr_set_u32(ip4_addr_t *ip,uint32_t n) {ip->addr=n;}
struct Fixture {
    netif ethernet={1};
    discovery_device_t devices[DISCOVERY_MAX_HOSTS]={};
    struct {uint32_t found;} state={};
    struct {ip4_addr_t ip;eth_addr mac;bool valid;} cache[ARP_TABLE_SIZE]={};
    unsigned cursor=0,requests=0,clears=0;
    bool link=true, sparse=false;
    uint32_t active=7;
    int arp_done=0;
'''+job+'''
    constexpr int64_t esp_timer_get_time() {return 1000000;}
    constexpr bool diagnostic_current(uint32_t token) {return token==active;}
    constexpr bool interrupted(uint32_t revision) {return !diagnostic_accept(7,active,revision,1,link);}
    constexpr netif *netif_get_by_index(u8_t i) {return i==1 ? &ethernet : nullptr;}
    constexpr void xSemaphoreGive(int) {}
    constexpr void etharp_cleanup_netif(netif *) {for(auto &e:cache)e.valid=false;++clears;}
    constexpr int etharp_get_entry(size_t i,ip4_addr_t **ip,netif **owner,eth_addr **mac) {
        if(!cache[i].valid)return 0;
        *ip=&cache[i].ip;*owner=&ethernet;*mac=&cache[i].mac;return 1;
    }
    constexpr int etharp_request(netif *,const ip4_addr_t *ip) {
        ++requests;
        uint32_t last=ip->addr & 255;
        if(sparse && last!=2 && last!=5 && last!=10 && last!=21)return ERR_OK;
        auto &e=cache[cursor++%ARP_TABLE_SIZE];
        e.ip=*ip;e.mac={{2,0,0,0,0,(uint8_t)ip->addr}};e.valid=true;
        return ERR_OK;
    }
'''+callback+'''
    constexpr void step(uint32_t target) {arp_job.target=target;lookup_arp(nullptr);}
    constexpr bool test() {
        discovery_subnet(0xc0a8015e,0xffffff00,&arp_job.subnet);
        arp_job.index=1;arp_job.token=7;arp_job.revision=1;
        cache[0]={{0xc0a80104},{{1,1,1,1,1,1}},true};
        arp_job.reset=true;step(0);arp_job.reset=false;
        if(state.found || clears!=1)return false;
        unsigned logical_ms=0;
        for(uint32_t ip=arp_job.subnet.first;ip<=arp_job.subnet.last;++ip) {
            if(!discovery_candidate(&arp_job.subnet,ip))continue;
            step(ip);logical_ms+=DISCOVERY_ARP_STEP_MS;
        }
        step(0);step(0); // Final collection and repeated observations.
        if(requests!=253 || state.found!=(sparse ? 4U : 253U) || logical_ms!=10120)return false;
        for(unsigned i=0;i<state.found;++i) {
            if(!devices[i].arp_present || !devices[i].mac_valid || devices[i].icmp_responsive)return false;
            if(!discovery_present(devices[i].arp_present,devices[i].icmp_responsive))return false;
            if(i && discovery_ip_compare(devices[i-1].ip,devices[i].ip)>=0)return false;
        }
        if(devices[0].ip!=arp_job.subnet.first+(sparse ? 1U : 0U))return false; // Retained after eviction.
        unsigned before=requests;
        link=false;step(0xc0a80104);
        if(requests!=before || arp_job.result!=ERR_IF)return false;
        link=true;active=8;step(0xc0a80104); // Stale callback must not send/copy.
        if(requests!=before || arp_job.result!=ERR_IF)return false;
        // Reconnection obtains a new reservation; old release cannot free it.
        diagnostic_reservation_t owner={};
        uint32_t token=diagnostic_reserve(&owner);
        if(!diagnostic_unreserve(&owner,token))return false;
        uint32_t next=diagnostic_reserve(&owner);
        if(!next || next==token || diagnostic_unreserve(&owner,token))return false;
        active=7;arp_job.token=7;step(0);
        return arp_job.result==ERR_OK && diagnostic_unreserve(&owner,next);
    }
};
constexpr bool test_callback() {Fixture dense={},sparse={};sparse.sparse=true;return dense.test() && sparse.test();}
static_assert(test_callback(),"Actual rolling ARP callback failed");
constexpr bool test_updates() {
    discovery_device_t table[DISCOVERY_MAX_HOSTS]={};uint32_t count=0;
    uint8_t a[6]={2,0,0,0,0,1},b[6]={2,0,0,0,0,2};
    discovery_observe(table,&count,2,a);discovery_observe(table,&count,2,b);
    return count==1 && table[0].mac[5]==2 && table[0].arp_present;
}
static_assert(test_updates(),"Duplicate IP must update MAC");
'''
(ROOT/'build/fast_discovery_test.cpp').write_text(fixture)
