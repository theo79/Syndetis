// Actual 5A owner/dispatcher and 5B socket state machine; only SDK/socket I/O is mocked.
#define TEST_REAL_PING
#define MANUAL_TEST_ENTRY milestone5a_tests
#include "manual_ipv4_test.c"
#include "../main/network_ping.c"

static int sockets_open, sockets_created, sockets_closed, ping_sends, fault, recv_calls;
static bool device_bound, source_bound, flood;
static uint8_t last_request[24], queue[64][44];
static unsigned queue_read, queue_write;
static uint32_t last_target;
static uint64_t send_times[4];
static void (*send_hook)(void);

static uint16_t test_sum(const uint8_t *p,size_t n) {
    uint32_t s=0;for(size_t i=0;i<n;i+=2)s+=(unsigned)p[i]*256+p[i+1];
    while(s>65535)s=(s&65535)+(s>>16);return (uint16_t)~s;
}
static void test_ip(uint8_t*p,uint32_t ip){p[0]=ip>>24;p[1]=ip>>16;p[2]=ip>>8;p[3]=ip;}
static void ip_packet(uint8_t ip[44],const uint8_t icmp[24],uint32_t src,uint32_t dst,bool reply) {
    memset(ip,0,44);ip[0]=0x45;ip[3]=44;ip[8]=64;ip[9]=1;
    test_ip(ip+12,src);test_ip(ip+16,dst);memcpy(ip+20,icmp,24);
    if(reply){ip[20]=0;ip[22]=ip[23]=0;uint16_t c=test_sum(ip+20,24);ip[22]=c>>8;ip[23]=c;}
    uint16_t c=test_sum(ip,20);ip[10]=c>>8;ip[11]=c;
}
int test_socket(int family,int type,int protocol){
    CHECK(family==AF_INET&&type==SOCK_RAW&&protocol==IPPROTO_ICMP&&!sockets_open);
    if(fault==1){errno=EMFILE;return -1;}++sockets_open;++sockets_created;
    device_bound=source_bound=false;queue_read=queue_write=0;return 42;
}
int test_setsockopt(int fd,int level,int option,const void*value,socklen_t n){
    CHECK(fd==42&&level==SOL_SOCKET&&option==SO_BINDTODEVICE&&n==sizeof(struct ifreq));
    CHECK(!strcmp(((const struct ifreq*)value)->ifr_name,"en1"));
    if(fault==2){errno=ENODEV;return -1;}device_bound=true;return 0;
}
int test_bind(int fd,const struct sockaddr*sa,socklen_t n){
    const struct sockaddr_in*a=(const struct sockaddr_in*)sa;
    CHECK(fd==42&&n==sizeof(*a)&&a->sin_family==AF_INET&&a->sin_addr.s_addr==fake.ip.ip.addr);
    CHECK(a->sin_addr.s_addr!=lwip_htonl(0xc0a80401));
    if(fault==3){errno=EADDRNOTAVAIL;return -1;}source_bound=true;return 0;
}
int test_sendto(int fd,const void*bytes,size_t n,int flags,const struct sockaddr*sa,socklen_t len){
    CHECK(fd==42&&device_bound&&source_bound&&n==24&&flags==MSG_DONTWAIT&&len==sizeof(struct sockaddr_in));
    if(send_hook){void(*h)(void)=send_hook;send_hook=NULL;h();}
    if(fault==4){errno=EIO;return -1;}
    const struct sockaddr_in*a=(const struct sockaddr_in*)sa;
    last_target=lwip_ntohl(a->sin_addr.s_addr);CHECK((last_target&0xffffff00)!=0xc0a80400);
    uint8_t frame[58]={0};frame[12]=8;
    ip_packet(frame+14,bytes,lwip_ntohl(fake.ip.ip.addr),last_target,false);
    if(transmit(NULL,frame,sizeof(frame))!=ESP_OK){errno=ENETDOWN;return -1;}
    CHECK(ping_sends<4);send_times[ping_sends++]=test_time_us;memcpy(last_request,bytes,24);return 24;
}
int test_recvfrom(int fd,void*bytes,size_t n,int flags,struct sockaddr*sa,socklen_t*len){
    CHECK(fd==42&&n>=44&&flags==MSG_DONTWAIT);++recv_calls;
    if(fault==5){errno=EIO;return -1;}
    struct sockaddr_in*a=(struct sockaddr_in*)sa;memset(a,0,sizeof(*a));a->sin_family=AF_INET;a->sin_addr.s_addr=lwip_htonl(last_target);*len=sizeof(*a);
    if(flood){memset(bytes,0,44);return 44;}
    if(queue_read==queue_write){errno=EAGAIN;return -1;}
    memcpy(bytes,queue[queue_read++%64],44);return 44;
}
int test_close(int fd){CHECK(fd==42&&sockets_open==1);if(fault==6){fault=0;errno=EIO;return -1;}--sockets_open;++sockets_closed;if(fault==7){errno=EBADF;return -1;}return 0;}
static void received(const uint8_t*f,size_t n){
    if(n>=58&&f[12]==8&&!f[13]&&f[23]==1&&sockets_open){CHECK(queue_write-queue_read<64);memcpy(queue[queue_write++%64],f+14,44);}
}
static void reply(unsigned ms,bool stale){
    test_time_us+=(int64_t)ms*1000;
    uint8_t *f=calloc(1,58);f[12]=8;
    uint8_t request[24];memcpy(request,last_request,24);
    if(stale){request[12]^=1;request[2]=request[3]=0;uint16_t c=test_sum(request,24);request[2]=c>>8;request[3]=c;}
    ip_packet(f+14,request,last_target,lwip_ntohl(fake.ip.ip.addr),true);
    ethernet_dispatch(f,58);
}
static void ping_tick(unsigned ms){test_time_us+=(int64_t)ms*1000;worker_step();network_ping_step();}
static void start_ping(bool gateway,uint32_t target){
    ping_sends=0;CHECK(!network_ping_start(gateway,target));network_ping_step();CHECK(ping_sends==1);
    CHECK(ping_reserved&&sockets_open==1);CHECK(network_ping_start(gateway,target)!=NULL);
    CHECK(!dhcp_test_request(true)&&!dhcp_test_request(false)&&!wired_use_dhcp());
    CHECK(wired_manual_apply(&config,dhcp_test_status().generation)!=NULL);
}
static void done(void){CHECK(!network_ping_status().active&&!ping_reserved&&!ping_window&&!sockets_open);CHECK(sockets_created==sockets_closed);}
static void lease(void){start();address(0xc000020a,0xffffff00);step();CHECK(state.mode==WIRED_DHCP);}
static void clear_owner(void){CHECK(wired_use_dhcp());step();passive_check();}
static void codec_tests(void){
    uint8_t request[24],frame[58]={0},copy[58];frame[12]=8;
    ping_packet_make(request,0x1234,2,0x1122334455667788ULL);
    ip_packet(frame+14,request,0xc0000201,0xc000020a,true);
    CHECK(ping_frame_match(frame,58,0xc0000201,0xc000020a,0x1234,2,0x1122334455667788ULL,true));
    const unsigned corrupt[]={12,14,16,20,23,24,26,30,34,35,36,38,40,42,46,54};
    for(unsigned i=0;i<sizeof(corrupt)/sizeof(*corrupt);++i){memcpy(copy,frame,58);copy[corrupt[i]]^=1;CHECK(!ping_frame_match(copy,58,0xc0000201,0xc000020a,0x1234,2,0x1122334455667788ULL,true));}
    for(unsigned n=0;n<58;++n)CHECK(!ping_frame_match(frame,n,0xc0000201,0xc000020a,0x1234,2,0x1122334455667788ULL,true));
    CHECK(!ping_frame_match(frame,58,0xc0000201,0xc000020a,0x1234,3,0x1122334455667788ULL,true));
    CHECK(!ping_frame_match(frame,58,0xc0000201,0xc0a80401,0x1234,2,0x1122334455667788ULL,true));
}
int main(void){
    CHECK(milestone5a_tests()==0);frame_received_hook=received;codec_tests();
    CHECK(network_ping_start(false,0xc0000201)!=NULL&&!sockets_open);
    lease();
    CHECK(ping_target_validate(0xc000020a,0xc000020a,0xffffff00,0xc0000201));
    CHECK(ping_target_validate(0xc0000200,0xc000020a,0xffffff00,0xc0000201));
    CHECK(ping_target_validate(0xc00002ff,0xc000020a,0xffffff00,0xc0000201));
    CHECK(ping_target_validate(0x7f000001,0xc000020a,0xffffff00,0xc0000201));
    CHECK(ping_target_validate(0xe0000001,0xc000020a,0xffffff00,0xc0000201));
    CHECK(ping_target_validate(0xc0a80401,0xc000020a,0xffffff00,0xc0000201));
    CHECK(ping_target_validate(0xcb007101,0xc000020a,0xffffff00,0));
    CHECK(!ping_target_validate(0xcb007101,0xc000020a,0xffffff00,0xc0000201));
    CHECK(network_ping_start(false,0xc000020a)!=NULL&&!ping_reserved);
    // Four full replies, accurate RX RTT despite delayed worker service, no duplicates.
    start_ping(true,0);CHECK(last_target==0xc0000201);
    for(int i=0;i<4;++i){reply(3,false);reply(0,false);ping_tick(47);CHECK(network_ping_status().replies==(unsigned)i+1);if(i<3)ping_tick(950);}
    done();network_ping_status_t p=network_ping_status();CHECK(p.sent==4&&p.replies==4&&p.rtt_sum_us==12000);
    for(int i=1;i<4;++i)CHECK(send_times[i]-send_times[i-1]==1000000);
    // Routed target is still pinned to Ethernet; partial and zero replies remain factual.
    start_ping(false,0xcb007101);
    for(int i=0;i<4;++i){if(i%2==0){reply(10,false);ping_tick(0);if(i<3)ping_tick(990);}else ping_tick(1000);}
    done();p=network_ping_status();CHECK(p.sent==4&&p.replies==2&&p.rtt_sum_us==20000);
    start_ping(false,0xc0000202);flood=true;
    for(int i=0;i<4;++i){int calls=recv_calls;ping_tick(1000);CHECK(recv_calls-calls<=8);}flood=false;
    done();CHECK(!strcmp(network_ping_status().detail,"NO REPLY"));
    // A stale-cookie packet never enters the stack and never contributes a reply.
    start_ping(false,0xc0000202);int rx=receives;reply(10,true);CHECK(receives==rx);ping_tick(0);CHECK(!network_ping_status().replies);
    uint8_t stale_tx[58]={0};stale_tx[12]=8;ip_packet(stale_tx+14,last_request,lwip_ntohl(fake.ip.ip.addr),last_target,false);
    network_ping_cancel();CHECK(transmit(NULL,stale_tx,58)==ESP_ERR_INVALID_STATE);ping_tick(0);done();CHECK(transmit(NULL,stale_tx,58)==ESP_ERR_INVALID_STATE);
    start_ping(false,0xc0000202);CHECK(transmit(NULL,stale_tx,58)==ESP_ERR_INVALID_STATE);network_ping_cancel();ping_tick(0);done();
    // Last-millisecond reply counts by RX time even if dequeued after the deadline.
    start_ping(false,0xc0000202);reply(999,false);ping_tick(51);CHECK(network_ping_status().replies==1);network_ping_cancel();ping_tick(0);done();
    start_ping(false,0xc0000202);receive_fails=true;reply(3,false);receive_fails=false;ping_tick(997);CHECK(!network_ping_status().replies);network_ping_cancel();ping_tick(0);done();
    // New native IP before owner publication, then mode/link generation invalidation.
    start_ping(false,0xc0000202);address(0xc000020b,0xffffff00);network_ping_step();done();step();
    start_ping(false,0xc0000202);dhcp_test_link(false);CHECK(!network_ping_status().valid);ping_tick(0);done();dhcp_test_link(true);step();passive_check();
    // Manual IP with no DHCP or gateway supports local Ping and rejects routed/gateway requests.
    advance(60000);arp_count=0;claim();
    CHECK(network_ping_start(true,0)!=NULL&&!ping_reserved);
    CHECK(network_ping_start(false,0xcb007101)!=NULL&&!ping_reserved);
    start_ping(false,0xc0a80a01);reply(5,false);ping_tick(45);CHECK(network_ping_status().replies==1&&!fake.started);
    owner_arp(false);CHECK(!network_ping_status().valid&&!tx_enabled);ping_tick(0);done();passive_check();
    // All failure paths retire the descriptor; cleanup failure retains it for retry.
    lease();
    for(fault=1;fault<=6;++fault){int f=fault;ping_sends=0;CHECK(!network_ping_start(false,0xc0000202));network_ping_step();
        if(f==5)network_ping_step();if(f==6){network_ping_cancel();network_ping_step();CHECK(sockets_open==1&&ping_reserved);network_ping_step();}
        done();fault=f;}
    fault=7;start_ping(false,0xc0000202);network_ping_cancel();ping_tick(0);done();
    fault=0;interface_name_fails=true;CHECK(!network_ping_start(false,0xc0000202));network_ping_step();done();interface_name_fails=false;
    send_hook=disconnect_hook;ping_sends=0;CHECK(!network_ping_start(false,0xc0000202));network_ping_step();CHECK(!ping_sends);done();step();passive_check();
    // Completed results are invalid after a mode change. Repeated sessions retain no descriptor.
    lease();for(int i=0;i<32;++i){start_ping(false,0xc0000202);network_ping_cancel();ping_tick(0);done();discovery_check();}
    clear_owner();CHECK(!network_ping_status().valid);
    puts("PASS: real Ping/owner: wired binding, DHCP/manual, local/routed targets, 4/partial/zero replies, RX RTT, bounded flood handling, stale cookies/results, cancellation/link/conflict races, API failures and 32 clean session retirements");
    return 0;
}
