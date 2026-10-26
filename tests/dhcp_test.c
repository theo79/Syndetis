// Actual DHCP worker and dispatcher, with only SDK and hardware I/O stubbed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int frees;
static void tracked_free(void *p){++frees;free(p);}
#define free tracked_free
#include "../main/dhcp_test.c"
#include "../main/receive_dispatch.h"
#undef free
int64_t test_time_us;
static esp_netif_t fake;
static int starts, releases, receives, observations, injected_error;
static bool receive_fails, stop_fails;
static void (*manual_set_hook)(void), (*manual_start_hook)(void), (*manual_tx_hook)(void);
static void (*dhcp_read_hook)(void);
static void (*frame_received_hook)(const uint8_t *, size_t);
static bool interface_name_fails;
#ifndef TEST_REAL_PING
void network_ping_step(void) {}
#endif
static int arp_count, garps_suppressed, arp_error;
static uint8_t sent_arp[128][60];
static int64_t arp_times[128];
static ip_addr_t resolver[DNS_MAX_SERVERS];
static bool tcpip_context, restore_fails;
static esp_netif_t ap;
static uint32_t ap_dns = 0xc0a80401;
const ip_addr_t *dns_getserver(int i){CHECK(tcpip_context);return &resolver[i];}
void dns_setserver(int i,const ip_addr_t *v){CHECK(tcpip_context);resolver[i].addr=v?v->addr:0;}
esp_err_t esp_netif_tcpip_exec(esp_err_t (*fn)(void*),void*c){
    if(fn==restore_dns&&restore_fails)return ESP_FAIL;
    CHECK(!tcpip_context);tcpip_context=true;esp_err_t e=fn(c);tcpip_context=false;return e;
}
esp_netif_t *esp_netif_new(const esp_netif_config_t *c){CHECK(c->base->flags==(ESP_NETIF_DHCP_CLIENT|ESP_NETIF_FLAG_AUTOUP));return &fake;}
esp_err_t esp_netif_set_mac(esp_netif_t*n,uint8_t*m){(void)n;(void)m;return 0;}
esp_err_t esp_netif_dhcpc_start(esp_netif_t*n){++starts;n->started=true;return injected_error;}
esp_err_t esp_netif_dhcpc_stop(esp_netif_t*n){n->started=false;return 0;}
esp_err_t esp_netif_set_ip_info(esp_netif_t*n,const esp_netif_ip_info_t*i){
    CHECK(n==&fake&&!n->started);n->ip=*i;
    // The real setter clears global resolver slots, even for a zero address.
    memset(resolver,0,sizeof(resolver));
    if(i->ip.addr&&manual_set_hook){void(*hook)(void)=manual_set_hook;manual_set_hook=NULL;hook();}
    return 0;
}
esp_err_t esp_netif_set_old_ip_info(esp_netif_t*n,const esp_netif_ip_info_t*i){(void)n;(void)i;return 0;}
// Match pinned SDK: ANY/zero DNS is rejected, unlike the original permissive stub.
esp_err_t esp_netif_set_dns_info(esp_netif_t*n,int t,esp_netif_dns_info_t*d){CHECK(n==&fake);if(!d||!d->ip.u_addr.ip4.addr)return 0x5001;resolver[t].addr=d->ip.u_addr.ip4.addr;return 0;}
esp_err_t esp_netif_get_ip_info(esp_netif_t*n,esp_netif_ip_info_t*i){
    *i=n->ip;if(dhcp_read_hook){void(*hook)(void)=dhcp_read_hook;dhcp_read_hook=NULL;hook();}return 0;
}
esp_err_t esp_netif_get_dns_info(esp_netif_t*n,int t,esp_netif_dns_info_t*d){CHECK(n==&fake);memset(d,0,sizeof(*d));d->ip.u_addr.ip4.addr=resolver[t].addr;return 0;}
void esp_netif_action_start(void*n,void*b,int e,void*d){
    (void)b;(void)e;(void)d;((esp_netif_t*)n)->up=true;
    if(((esp_netif_t*)n)->ip.ip.addr){
        CHECK(!((esp_netif_t*)n)->started);
        CHECK(transmit(NULL,NULL,0)==ESP_ERR_INVALID_STATE);++garps_suppressed;
        if(manual_start_hook){void(*hook)(void)=manual_start_hook;manual_start_hook=NULL;hook();}
    }
}
void esp_netif_action_stop(void*n,void*b,int e,void*d){CHECK(n==&fake);(void)b;(void)e;(void)d;++releases;if(!stop_fails)((esp_netif_t*)n)->up=false;}
bool esp_netif_is_netif_up(esp_netif_t*n){return n->up;}
esp_err_t esp_netif_receive(esp_netif_t*n,void*b,size_t l,void*x){(void)n;(void)x;++receives;if(!receive_fails&&frame_received_hook)frame_received_hook(b,l);tracked_free(b);return receive_fails?ESP_FAIL:ESP_OK;}
esp_err_t esp_netif_get_netif_impl_name(esp_netif_t*n,char*name){CHECK(n==&fake);if(interface_name_fails)return ESP_FAIL;strcpy(name,"en1");return ESP_OK;}
esp_err_t esp_eth_transmit(void*h,void*b,size_t n){
    (void)h;
    if(b&&n==60){
        CHECK(arp_count<128);memcpy(sent_arp[arp_count],b,60);arp_times[arp_count++]=test_time_us;
        if(manual_tx_hook){void(*hook)(void)=manual_tx_hook;manual_tx_hook=NULL;hook();}
        return arp_error;
    }
    return 0;
}
void discovery_receive(const uint8_t*b,size_t n){CHECK(b&&n);++observations;}
static void address(uint32_t ip,uint32_t mask){fake.ip.ip.addr=lwip_ntohl(ip);fake.ip.netmask.addr=lwip_ntohl(mask);fake.ip.gw.addr=lwip_ntohl(0xc0000201);}
static void step(void){worker_step();}
static void start(void){CHECK(dhcp_test_request(true));CHECK(!dhcp_test_request(true));step();CHECK(state.state==DHCP_REQUESTING);}
static uint8_t *packet(int type){uint8_t *p=calloc(1,64);p[12]=type>>8;p[13]=type;p[14]=0x45;p[23]=17;p[34]=0;p[35]=67;p[36]=0;p[37]=68;return p;}
static void discovery_check(void){
    int seen=observations, freed=frees, received=receives;
    ethernet_dispatch(packet(0x88cc),64);ethernet_dispatch(packet(0x0000),64); // LLDP / 802.3 CDP dispatch
    CHECK(observations==seen+2&&frees==freed+2&&receives==received);
}
static void passive_check(void){
    CHECK(!state.active&&!state.busy&&!fake.up&&!fake.started&&!fake.ip.ip.addr);
    CHECK(!rx_enabled&&!tx_enabled&&!inflight&&!dns_borrowed&&pending==CMD_NONE);
    CHECK(!state.ip[0]&&!state.mask[0]&&!state.gateway[0]&&!state.dns[0][0]&&!state.dns[1][0]);
    CHECK(ap.up&&ap.started&&ap.ip.ip.addr==0xc0a80401&&ap_dns==0xc0a80401);
    CHECK(resolver[0].addr==0&&resolver[1].addr==0x12345678&&resolver[2].addr==0x87654321);
    discovery_check();
}
int main(void){
    uint8_t mac[6]={2};CHECK(dhcp_test_init(NULL,mac)==ESP_OK);
    ap.up=ap.started=true;ap.ip.ip.addr=0xc0a80401;
    resolver[1].addr=0x12345678;resolver[2].addr=0x87654321;
    esp_netif_dns_info_t zero_dns={0};CHECK(esp_netif_set_dns_info(&fake,0,&zero_dns)==0x5001);
    CHECK(!fake.up&&!fake.started&&starts==0);
    CHECK(transmit(NULL,NULL,0)==ESP_ERR_INVALID_STATE);
    CHECK(!dhcp_test_request(true)&&state.state==DHCP_CONNECT&&starts==0);
    dhcp_test_link(true);start();CHECK(starts==1&&fake.started);
    CHECK(!resolver[1].addr&&!resolver[2].addr);
    resolver[0].addr=lwip_ntohl(0xc0000235);
    test_time_us+=1200000;address(0xc000020a,0xffffff00);step();
    CHECK(state.state==DHCP_ACQUIRED&&state.active&&!strcmp(state.ip,"192.0.2.10")&&state.duration_ms==1200);
    CHECK(!strcmp(state.dns[0],"192.0.2.53")&&!state.dns[1][0]);
    CHECK(!dhcp_test_request(true));CHECK(!dhcp_test_cancel_requesting());
    int count=frees;ethernet_dispatch(packet(0x0800),64);CHECK(receives==1&&frees==count+1);
    receive_fails=true;ethernet_dispatch(packet(0x0800),64);CHECK(receives==2&&frees==count+2);receive_fails=false;
    uint8_t *tcp=packet(0x0800);tcp[23]=6;ethernet_dispatch(tcp,64);CHECK(receives==2&&frees==count+3);
    ethernet_dispatch(packet(0x88cc),64);CHECK(receives==2&&frees==count+4&&observations==4);
    unsigned generation=link_session;
    CHECK(dhcp_test_request(false));CHECK(!dhcp_test_status().active);step();
    CHECK(state.state==DHCP_STOPPED&&!fake.up&&!fake.started&&!fake.ip.ip.addr&&generation==link_session);
    passive_check();
    address(0xc000020a,0xffffff00);step();CHECK(!state.active); // late old acquisition ignored
    count=frees;ethernet_dispatch(packet(0x0800),64);CHECK(receives==2&&frees==count+1);
    fake.ip.ip.addr=0;start();test_time_us+=30000000;step();
    CHECK(state.state==DHCP_TIMEOUT&&!fake.up&&!fake.started&&!state.active);
    start();CHECK(dhcp_test_cancel_requesting());address(0xc000020a,0xffffff00);step();
    CHECK(state.state==DHCP_CANCELLED&&!state.active&&!fake.up&&!fake.ip.ip.addr);
    CHECK(dhcp_test_request(true));CHECK(dhcp_test_cancel_requesting());int old_starts=starts;step();CHECK(starts==old_starts&&!fake.up);
    start();address(0xc000020a,0xffffff00);step();CHECK(state.active);
    dhcp_test_link(false);CHECK(!dhcp_test_status().active);dhcp_test_link(true);step();
    CHECK(state.state==DHCP_LINK_LOST&&!state.busy&&!fake.up);step();CHECK(starts==old_starts+1);
    start();address(0xc000020a,0xffffff00);step();CHECK(state.active);
    fake.ip.ip.addr=0;step();CHECK(state.state==DHCP_ADDRESS_LOST&&!state.active&&!fake.up);
    old_starts=starts;step();CHECK(starts==old_starts);
    start();address(0xc0a80480,0xffffff00);step();CHECK(state.state==DHCP_OVERLAP&&!state.active&&!fake.up);
    start();address(0xc0a80180,0xffff0000);step();CHECK(state.state==DHCP_OVERLAP&&!fake.up);
    CHECK(dhcp_management_overlap(0xc0a80401,0xffffffff));CHECK(!dhcp_management_overlap(0xc000020a,0xffffff00));
    injected_error=ESP_FAIL;CHECK(dhcp_test_request(true));step();CHECK(state.state==DHCP_ERROR&&state.error==ESP_FAIL&&!fake.up);injected_error=0;
    start();stop_fails=true;CHECK(dhcp_test_request(false));step();CHECK(state.cleanup_error&&state.state==DHCP_CANCELLED&&state.cleanup_code==ESP_FAIL&&!state.error&&!dhcp_test_request(true));
    stop_fails=false;CHECK(dhcp_test_request(false));step();CHECK(!state.cleanup_error&&!fake.up&&!state.error);
    CHECK(dhcp_test_request(true));CHECK(dhcp_test_request(false));CHECK(!dhcp_test_request(false));old_starts=starts;step();CHECK(starts==old_starts&&!fake.up);
    for(int cycle=0;cycle<32;++cycle){
        start();discovery_check();test_time_us+=30000000;step();CHECK(state.state==DHCP_TIMEOUT&&!state.error&&!state.cleanup_error);passive_check();
        start();discovery_check();CHECK(dhcp_test_cancel_requesting());address(0xc000020a,0xffffff00);step();CHECK(state.state==DHCP_CANCELLED);passive_check();
    }
    start();address(0xc000020a,0xffffff00);step();CHECK(state.active&&!state.dns[0][0]&&!state.dns[1][0]);
    CHECK(dhcp_test_request(false));step();passive_check();
    // Timeout stays primary when cleanup fails; restore remains retryable.
    start();resolver[0].addr=0x11111111;restore_fails=true;test_time_us+=30000000;step();
    CHECK(state.state==DHCP_TIMEOUT&&state.cleanup_error&&state.cleanup_code==ESP_FAIL&&!state.error&&dns_borrowed&&!dhcp_test_request(true));
    CHECK(strstr(state.cleanup_step,"restore DNS"));
    restore_fails=false;CHECK(dhcp_test_request(false));step();passive_check();CHECK(!state.cleanup_error&&!state.cleanup_code&&!state.cleanup_step[0]);
    // Independent primary and cleanup API failures retain both operation names.
    injected_error=ESP_FAIL;restore_fails=true;CHECK(dhcp_test_request(true));step();
    CHECK(state.state==DHCP_ERROR&&state.error==ESP_FAIL&&strstr(state.error_step,"dhcpc_start")&&state.cleanup_error&&strstr(state.cleanup_step,"restore DNS"));
    injected_error=0;restore_fails=false;CHECK(dhcp_test_request(false));step();passive_check();
    uint8_t arp[64]={0};arp[12]=8;arp[13]=6;CHECK(dhcp_stack_frame(arp,64));CHECK(!dhcp_stack_frame(arp,14));
    uint8_t *udp=packet(0x0800);CHECK(dhcp_stack_frame(udp,64));udp[20]=0x20;CHECK(!dhcp_stack_frame(udp,64));udp[20]=0;udp[35]=53;CHECK(!dhcp_stack_frame(udp,64));udp[35]=67;udp[12]=0x81;udp[13]=0;CHECK(!dhcp_stack_frame(udp,64));tracked_free(udp);
    puts("PASS: actual DHCP worker: boot passive, explicit start, duplicates, acquisition/duration, timeout, cancel/late state, link/address loss, overlap, cleanup failures/retry; dispatcher consumes each owned buffer once and blocks Ethernet TCP");
    return 0;
}
