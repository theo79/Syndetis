// Actual shared owner/RX dispatcher; the existing DHCP suite runs first.
#define main original_dhcp_tests
#include "dhcp_test.c"
#undef main

static manual_ipv4_t config = {0xc0a80afa, 0, 24};
static uint32_t arp_ip(const uint8_t *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static void advance(unsigned ms) { test_time_us += (int64_t)ms * 1000; step(); }
static void apply(void) {
    CHECK(wired_manual_apply(&config, dhcp_test_status().generation)==NULL);
    CHECK(!state.active&&state.state==MANUAL_PROBING);step();
}
static void claim(void) {
    int before=arp_count;
    apply();CHECK(!fake.up&&!fake.started&&!fake.ip.ip.addr);
    advance(499);CHECK(arp_count==before);
    advance(1);CHECK(arp_count==before+1);
    advance(1499);CHECK(arp_count==before+1);
    advance(1);advance(1500);CHECK(arp_count==before+3&&!fake.ip.ip.addr);
    advance(1999);CHECK(!state.active&&!fake.up);
    advance(1);CHECK(state.active&&state.mode==WIRED_MANUAL&&state.prefix==24);
    CHECK(!strcmp(state.ip,"192.168.10.250")&&!state.gateway[0]&&!fake.started&&fake.up);
    CHECK(arp_count==before+4&&rx_enabled&&tx_enabled&&manual_watch&&!watch_probing);
    advance(1999);CHECK(arp_count==before+4);advance(1);CHECK(arp_count==before+5);
    for(int i=0;i<5;++i){
        const uint8_t*f=sent_arp[before+i];
        CHECK(!memcmp(f,"\xff\xff\xff\xff\xff\xff",6)&&f[12]==8&&f[13]==6&&f[21]==1);
        CHECK(arp_ip(f+38)==config.ip&&arp_ip(f+28)==(i<3?0:config.ip));
        CHECK(!memcmp(f+32,"\0\0\0\0\0\0",6));
    }
    CHECK(arp_times[before+1]-arp_times[before]>=1000000&&arp_times[before+1]-arp_times[before]<=2000000);
    CHECK(arp_times[before+3]-arp_times[before+2]>=2000000);
    CHECK(arp_times[before+4]-arp_times[before+3]>=2000000);
}
static void owner_arp(bool probe) {
    uint8_t *f=malloc(60);uint8_t peer[6]={4,5,6,7,8,9};
    manual_ipv4_arp(f,peer,config.ip,!probe);
    ethernet_dispatch(f,60);
}
static void conflict_hook(void) { owner_arp(false); }
static void disconnect_hook(void) { dhcp_test_link(false);dhcp_test_link(true); }
static void apply_during_read(void) { CHECK(wired_manual_apply(&config,dhcp_test_status().generation)==NULL); }
static void reset_passive(void) {
    CHECK(wired_use_dhcp());step();passive_check();arp_count=0;
}
static void validation(void) {
    test_random_value=0;CHECK(random_delay(1001)==0);
    test_random_value=UINT32_MAX;CHECK(random_delay(1001)==1000);
    test_random_value=0x80000000u;
    manual_ipv4_t c=config;
    CHECK(!manual_ipv4_validate(&c));CHECK(manual_ipv4_mask(1)==0x80000000&&manual_ipv4_mask(30)==0xfffffffc);
    for(unsigned p=0;p<=32;++p){c=config;c.prefix=(uint8_t)p;
        CHECK((manual_ipv4_validate(&c)==NULL)==(p>=21&&p<=30));
    }
    c=config;c.ip=0;CHECK(manual_ipv4_validate(&c));c.ip=0x7f000001;CHECK(manual_ipv4_validate(&c));
    c.ip=0xe0000001;CHECK(manual_ipv4_validate(&c));c.ip=0xc0a80a00;CHECK(manual_ipv4_validate(&c));
    c.ip=0xc0a80aff;CHECK(manual_ipv4_validate(&c));
    c=config;c.gateway=0xc0a80a01;CHECK(!manual_ipv4_validate(&c));
    c.gateway=c.ip;CHECK(manual_ipv4_validate(&c));c.gateway=0xc0a80b01;CHECK(manual_ipv4_validate(&c));
    c.gateway=0xc0a80aff;CHECK(manual_ipv4_validate(&c));c.gateway=0x7f000001;CHECK(manual_ipv4_validate(&c));
    c=config;c.ip=0xc0a804fe;CHECK(manual_ipv4_validate(&c));c.ip=0xc0a801fe;c.prefix=16;CHECK(manual_ipv4_validate(&c));
    // Valid /1-/8 configurations exist on the opposite side of the AP's prefix.
    for(unsigned p=1;p<=8;++p){c=(manual_ipv4_t){0x01020304,0,(uint8_t)p};CHECK(!manual_ipv4_validate(&c));}
    uint8_t f[60],mac[6]={2},other[6]={4};
    manual_ipv4_arp(f,other,config.ip,false);CHECK(manual_ipv4_conflict(f,60,mac,config.ip,true));
    CHECK(!manual_ipv4_conflict(f,60,mac,config.ip,false));CHECK(!manual_ipv4_conflict(f,41,mac,config.ip,true));
    f[21]=2;CHECK(!manual_ipv4_conflict(f,60,mac,config.ip,true));
    manual_ipv4_arp(f,other,config.ip,true);f[21]=2;CHECK(manual_ipv4_conflict(f,60,mac,config.ip,false));
    memcpy(f+22,mac,6);CHECK(!manual_ipv4_conflict(f,60,mac,config.ip,true));
    memcpy(f+22,other,6);f[18]=5;CHECK(!manual_ipv4_conflict(f,60,mac,config.ip,true));
    f[18]=6;f[12]=0x81;CHECK(!manual_ipv4_conflict(f,60,mac,config.ip,true));
}
#ifndef MANUAL_TEST_ENTRY
#define MANUAL_TEST_ENTRY main
#endif
int MANUAL_TEST_ENTRY(void) {
    CHECK(original_dhcp_tests()==0);validation();
    unsigned stale=dhcp_test_status().generation;dhcp_test_link(false);dhcp_test_link(true);
    CHECK(wired_manual_apply(&config,stale)!=NULL);
    claim();CHECK(garps_suppressed==1);
    CHECK(!dhcp_test_request(true)&&!dhcp_test_request(false)&&!wired_manual_cancel());
    int seen=observations,recv=receives,freed=frees;
    ethernet_dispatch(packet(0x0800),64); // A late DHCP ACK is observed then freed, never reaches the stack.
    CHECK(receives==recv&&frees==freed+1&&observations==seen+1);
    owner_arp(true);CHECK(state.active&&receives==recv+1); // Stack may answer ordinary competing probes once active.
    discovery_check();
    owner_arp(false);CHECK(!state.active&&!rx_enabled&&!tx_enabled&&pending==CMD_CONFLICT);
    step();CHECK(state.state==MANUAL_CONFLICT&&manual_conflicts==1);passive_check();arp_count=0;
    // Cancellation before and during probing must not assign anything.
    CHECK(wired_manual_apply(&config,dhcp_test_status().generation)==NULL);CHECK(wired_manual_cancel());step();passive_check();
    apply();advance(500);CHECK(wired_manual_cancel());step();passive_check();arp_count=0;
    // DHCP acquired while the editor is open invalidates its old review token.
    start();stale=dhcp_test_status().generation;address(0xc000020a,0xffffff00);step();CHECK(state.mode==WIRED_DHCP);
    CHECK(wired_manual_apply(&config,stale)!=NULL&&state.mode==WIRED_DHCP);
    claim();CHECK(!fake.started&&state.mode==WIRED_MANUAL);reset_passive();
    // An old DHCP poll completing after APPLY cannot publish a lease over Manual.
    start();address(0xc000020a,0xffffff00);dhcp_read_hook=apply_during_read;step();
    CHECK(state.state==MANUAL_PROBING&&!state.active&&pending==CMD_MANUAL);step();
    advance(500);advance(1500);advance(1500);advance(2000);CHECK(state.mode==WIRED_MANUAL);reset_passive();
    start();address(0xc000020a,0xffffff00);step();stale=dhcp_test_status().generation;
    address(0xc000020a,0xffffff80);step();CHECK(dhcp_test_status().generation!=stale);CHECK(dhcp_test_request(false));step();passive_check();
    // Link changes in initial wait, inter-probe and active states never auto-reclaim.
    for(int phase=0;phase<3;++phase){
        apply();if(phase==1)advance(500);if(phase==2){advance(500);advance(1500);advance(1500);advance(2000);CHECK(state.active);}
        disconnect_hook();step();CHECK(state.state==DHCP_LINK_LOST);passive_check();int count=arp_count;advance(9000);CHECK(arp_count==count);arp_count=0;
    }
    // Race hooks run during actual SDK setter/start/send calls, before publication.
    for(int hook=0;hook<3;++hook){
        apply();advance(500);advance(1500);advance(1500);
        if(hook==0)manual_set_hook=disconnect_hook;
        if(hook==1)manual_start_hook=conflict_hook;
        if(hook==2)manual_tx_hook=conflict_hook;
        advance(2000);CHECK(!state.active);step();passive_check();arp_count=0;
    }
    // Transmission failure, cleanup failure and resolver restoration remain visible/retryable.
    apply();arp_error=ESP_FAIL;advance(500);CHECK(state.state==DHCP_ERROR&&!state.active);arp_error=0;passive_check();arp_count=0;
    claim();restore_fails=true;CHECK(wired_use_dhcp());step();CHECK(state.cleanup_error&&!state.active&&dns_borrowed);
    CHECK(wired_manual_apply(&config,dhcp_test_status().generation)!=NULL);restore_fails=false;
    CHECK(wired_use_dhcp());step();passive_check();arp_count=0;
    // Persistent conflict throttling cannot be bypassed by editing, DHCP or reconnect.
    while(manual_conflicts<10){apply();owner_arp(true);step();CHECK(state.state==MANUAL_CONFLICT);passive_check();}
    CHECK(wired_manual_apply(&config,dhcp_test_status().generation)!=NULL);
    disconnect_hook();CHECK(wired_manual_apply(&config,dhcp_test_status().generation)!=NULL);
    advance(59999);CHECK(wired_manual_apply(&config,dhcp_test_status().generation)!=NULL);
    advance(1);apply();CHECK(wired_manual_cancel());step();passive_check();
    // Repeat successful full mode transitions; no extra netif/task/socket/cache allocation exists.
    for(int i=0;i<32;++i){
        advance(60000);arp_count=0;claim();discovery_check();reset_passive();
        start();address(0xc000020a,0xffffff00);step();CHECK(state.active&&state.mode==WIRED_DHCP);
        advance(60000);claim();reset_passive();
    }
    puts("PASS: validation, three timed probes/two announcements, competing/late conflicts, rate limit, explicit generation review, exclusive DHCP/manual ownership, RX/DNS/AP isolation, SDK-call races, 32 repeated transitions");
    return 0;
}
