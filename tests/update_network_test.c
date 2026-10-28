#define main baseline_main
#include "dhcp_test.c"
#undef main
static void putip(uint8_t *p,uint32_t ip){p[0]=ip>>24;p[1]=ip>>16;p[2]=ip>>8;p[3]=ip;}
static uint8_t *tcp(bool incoming,unsigned port,uint32_t local,uint32_t peer)
{
 uint8_t *p=calloc(1,64);p[12]=8;p[14]=0x45;p[17]=40;p[23]=6;
 putip(p+26,incoming?peer:local);putip(p+30,incoming?local:peer);
 p[incoming?36:34]=port>>8;p[incoming?37:35]=port;p[46]=0x50;return p;
}
int main(void)
{
 CHECK(baseline_main()==0);unsigned gen;
 CHECK(wired_update_reserve(&gen));CHECK(wired_update_valid(gen));
 CHECK(!dhcp_test_request(true)&&!wired_use_dhcp());dhcp_status_t snap;
 CHECK(!wired_ping_reserve(&snap));wired_update_release();CHECK(!wired_update_valid(gen));
 start();address(0xc000020a,0xffffff00);step();CHECK(state.active);
 CHECK(wired_update_reserve(&gen));
 CHECK(wired_manual_apply(&(manual_ipv4_t){.ip=0xc000020b,.prefix=24},address_generation)!=NULL);
 CHECK(!dhcp_test_request(false));CHECK(!wired_ping_reserve(&snap));
 int recv=receives,free_count=frees;
 ethernet_dispatch(tcp(true,443,0xc000020a,0xc000020b),64);CHECK(receives==recv);
 free_count=frees;
 ethernet_dispatch(tcp(true,80,0xc000020a,0xc000020b),64);CHECK(receives==recv&&frees==free_count+1);
 ethernet_dispatch(tcp(true,8080,0xc0a80401,0xc000020b),64);CHECK(receives==recv);
 ethernet_dispatch(tcp(true,8080,0xc000020a,0xc633640b),64);CHECK(receives==recv);
 ethernet_dispatch(tcp(true,8080,0xc000020a,0xc00002ff),64);CHECK(receives==recv);
 ethernet_dispatch(tcp(true,8080,0xc000020a,0xc000020b),64);CHECK(receives==recv+1);
 uint8_t *p=tcp(false,8080,0xc000020a,0xc000020b);CHECK(transmit(NULL,p,64)==ESP_OK);tracked_free(p);
 p=tcp(false,80,0xc000020a,0xc000020b);CHECK(transmit(NULL,p,64)==ESP_ERR_INVALID_STATE);tracked_free(p);
 p=tcp(true,8080,0xc000020a,0xc000020b);p[20]=0x20;CHECK(!dhcp_test_receive(p,64));tracked_free(p);
 p=tcp(true,8080,0xc000020a,0xc000020b);p[46]=0xf0;CHECK(!dhcp_test_receive(p,64));tracked_free(p);
 p=tcp(true,8080,0xc000020a,0xc000020b);p[17]=60;CHECK(!dhcp_test_receive(p,54));tracked_free(p);
 test_time_us+=179000000;CHECK(wired_update_valid(gen));wired_update_extend();
 test_time_us+=599000000;CHECK(wired_update_valid(gen));test_time_us+=1000000;CHECK(!wired_update_valid(gen));
 recv=receives;ethernet_dispatch(tcp(true,8080,0xc000020a,0xc000020b),64);CHECK(receives==recv);
 wired_update_release();CHECK(dhcp_test_request(false));step();
 start();address(0xc000020a,0xffffff00);step();CHECK(wired_update_reserve(&gen));dhcp_test_link(false);CHECK(!wired_update_valid(gen));
 wired_update_release();step();CHECK(!state.active);dhcp_test_link(true);
 CHECK(wired_update_reserve(&gen));CHECK(wired_update_valid(gen));
 // A link transition invalidates even a Wi-Fi-only maintenance reservation.
 dhcp_test_link(false);CHECK(!wired_update_valid(gen));wired_update_release();
 puts("PASS: actual wired owner and RX/TX: physical-session HTTP:8080 only, local/subnet/port/length/fragment gates, diagnostics exclusion, expiry, extension and link invalidation");
}
