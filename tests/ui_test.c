// Actual OLED navigation/layout with hardware/network I/O stubbed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L
#define _Static_assert(condition,message) typedef char host_static_assertion[(condition)?1:-1]
#endif
#include "../main/ui.c"
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static app_status_t fixture;
static neighbor_t fixture_neighbor;
static bool have_neighbor;
static int enables, disables;

static dhcp_status_t dhcp_fixture;
static int starts, stops, cancels;
static int manual_applies, manual_cancels, uses_dhcp;
static network_ping_status_t ping_fixture;
static int ping_starts, ping_cancels;
static const char *ping_start_error;
const char *network_ping_start(bool gateway,uint32_t target){(void)gateway;if(ping_start_error)return ping_start_error;++ping_starts;ping_fixture=(network_ping_status_t){.active=true,.valid=true,.target=target};return NULL;}
void network_ping_cancel(void){++ping_cancels;ping_fixture.active=false;}
network_ping_status_t network_ping_status(void){return ping_fixture;}
dhcp_status_t dhcp_test_status(void){return dhcp_fixture;}
const char *dhcp_state_name(dhcp_state_t s){(void)s;return "DHCP state";}
bool dhcp_test_request(bool start){if(start){++starts;dhcp_fixture.state=DHCP_REQUESTING;}else{++stops;dhcp_fixture.state=DHCP_STOPPED;}return true;}
bool dhcp_test_cancel_requesting(void){if(dhcp_fixture.state!=DHCP_REQUESTING)return false;++cancels;dhcp_fixture.state=DHCP_CANCELLED;return true;}
const char *wired_manual_apply(const manual_ipv4_t*c,unsigned generation){
    if(generation!=dhcp_fixture.generation)return "Changed: review again";
    const char *error=manual_ipv4_validate(c);if(error)return error;
    ++manual_applies;dhcp_fixture.state=MANUAL_PROBING;return NULL;
}
bool wired_manual_cancel(void){if(dhcp_fixture.state!=MANUAL_PROBING)return false;++manual_cancels;dhcp_fixture.state=DHCP_CANCELLED;return true;}
bool wired_use_dhcp(void){++uses_dhcp;dhcp_fixture.mode=WIRED_NO_IP;return true;}
app_status_t status_get(void) { return fixture; }
bool services_request_web(bool enabled) {
    if(enabled)++enables;else ++disables;
    fixture.web.state=enabled?WEB_ON:WEB_OFF;
    fixture.web.http_running=fixture.web.ap_running=enabled;
    return true;
}
discovery_summary_t discovery_summary(void) {
    discovery_summary_t s={0};s.available=true;s.state=DISC_CURRENT;
    s.used[0]=have_neighbor;s.protocols[0]=PROTOCOL_LLDP;return s;
}
bool discovery_record(int index,neighbor_t *out) {
    if(index!=0||!have_neighbor)return false;*out=fixture_neighbor;return true;
}
void display_clear(void) {}
uint8_t display_address(void) { return 0x3c; }
void display_rect(int x,int y,int w,int h,bool filled,bool on) {
    (void)filled;(void)on;CHECK(x>=0&&y>=0&&x+w<=128&&y+h<=64);
}
void display_text(int x,int y,const char *s,int scale,bool on) {
    (void)on;CHECK(x>=0&&y>=0&&y+7*scale<=64);
    CHECK(x+(int)strlen(s)*6*scale-scale<=128);
}

int main(void)
{
    fixture.ethernet.state=ETH_READY;fixture.web.state=WEB_OFF;
    ui_init();selection=4;ui_input(BUTTON_BACK);CHECK(page==MENU&&selection==4);
    ui_input(BUTTON_OK);CHECK(page==WEB_MENU);
    ui_input(BUTTON_BACK);CHECK(page==MENU&&enables==0&&disables==0);
    ui_input(BUTTON_OK);ui_input(BUTTON_OK);CHECK(enables==1&&fixture.web.state==WEB_ON);
    ui_input(BUTTON_BACK);CHECK(page==MENU&&fixture.web.state==WEB_ON);
    ui_input(BUTTON_OK);ui_input(BUTTON_OK);CHECK(page==WEB_DETAILS);
    ui_input(BUTTON_BACK);CHECK(page==WEB_MENU&&enables==1&&disables==0);
    web_selection=1;ui_input(BUTTON_BACK);CHECK(page==MENU&&disables==0);
    ui_input(BUTTON_OK);web_selection=1;ui_input(BUTTON_OK);CHECK(disables==1&&fixture.web.state==WEB_OFF);
    ui_input(BUTTON_BACK);CHECK(page==MENU);
    selection=1;ui_input(BUTTON_OK);CHECK(page==SWITCH_MENU);
    ui_input(BUTTON_OK);CHECK(page==SWITCH_STATUS);ui_input(BUTTON_BACK);CHECK(page==SWITCH_MENU);
    have_neighbor=true;fixture_neighbor.used=true;fixture_neighbor.protocol=PROTOCOL_LLDP;
    fixture_neighbor.fields=63;fixture_neighbor.ttl=120;fixture_neighbor.received_ms=1000;
    switch_selection=1;ui_input(BUTTON_OK);CHECK(page==SWITCH_NEIGHBOR);
    memset(fixture_neighbor.name,'N',255);strcpy(fixture_neighbor.name+248,"NAMEEND");
    memset(fixture_neighbor.chassis,'C',511);strcpy(fixture_neighbor.chassis+501,"CHASSISEND");
    memset(fixture_neighbor.port,'P',511);strcpy(fixture_neighbor.port+504,"PORTEND");
    memset(fixture_neighbor.description,'D',255);strcpy(fixture_neighbor.description+248,"DESCEND");
    strcpy(fixture_neighbor.management,"2001:db8:0:0:0:0:0:1");
    switch_lines();
    CHECK(line_count<128);
    char full[2561]={0};for(int i=0;i<line_count;++i)strcat(full,lines[i]);
    CHECK(strstr(full,"NAMEEND")&&strstr(full,"CHASSISEND")&&strstr(full,"PORTEND")&&strstr(full,"DESCEND"));
    for(int i=0;i<line_count;++i){top=i;ui_draw();} // Every full-text row stays within display bounds.
    have_neighbor=false;ui_draw();CHECK(!strcmp(lines[0],"Neighbor cleared"));
    ui_input(BUTTON_BACK);CHECK(page==SWITCH_MENU);ui_input(BUTTON_BACK);CHECK(page==MENU);
    for(selection=0;selection<6;++selection)ui_draw();selection=0;
    ui_draw_splash(0);ui_draw_splash(900);ui_draw_splash(1400);
    CHECK(enables==1&&disables==1);
    page=MENU;selection=2;ui_input(BUTTON_OK);CHECK(page==TESTS_MENU);
    ui_input(BUTTON_OK);CHECK(page==DHCP_MENU);ui_input(BUTTON_OK);CHECK(page==DHCP_RESULTS&&starts==1);
    ui_input(BUTTON_BACK);CHECK(page==DHCP_MENU&&cancels==1);
    dhcp_fixture.state=DHCP_ACQUIRED;dhcp_fixture.active=true;
    ui_input(BUTTON_BACK);CHECK(page==TESTS_MENU&&dhcp_fixture.active&&cancels==1);
    page=DHCP_MENU;dhcp_selection=2;ui_input(BUTTON_OK);CHECK(stops==1);
    dhcp_fixture.active=false;page=DHCP_RESULTS;dhcp_fixture.state=DHCP_REQUESTING;
    ui_input(BUTTON_OK);CHECK(page==DHCP_MENU&&cancels==2);
    for(int p=TESTS_MENU;p<=DHCP_RESULTS;++p){page=p;top=0;ui_draw();}
    dhcp_fixture.state=DHCP_TIMEOUT;dhcp_fixture.cleanup_error=true;dhcp_fixture.cleanup_code=0x5001;
    strcpy(dhcp_fixture.cleanup_step,"restore DNS");dhcp_lines(true);
    char result[2561]={0};for(int i=0;i<line_count;++i)strcat(result,lines[i]);
    CHECK(strstr(result,"Cleanup:")&&strstr(result,"restore DNS")&&strstr(result,"5001")&&strstr(result,"Retry Stop"));
    // Entry/edit/review never changes the active lease. Only explicit APPLY may submit.
    ui_init();page=TESTS_MENU;tests_selection=1;ui_input(BUTTON_OK);CHECK(page==MANUAL_MENU);
    CHECK(draft.prefix==24&&!draft.gateway&&!manual_applies);
    ui_input(BUTTON_OK);CHECK(ui_editing()&&page==MANUAL_IP_EDIT);
    uint32_t before=draft.ip;ui_input(BUTTON_UP);ui_input(BUTTON_DOWN_REPEAT);CHECK(draft.ip==before);
    draft.ip=0xff000000;ui_input(BUTTON_UP_REPEAT);CHECK(draft.ip==0xff000000);
    draft.ip=0;ui_input(BUTTON_DOWN_REPEAT);CHECK(!draft.ip);
    draft.ip=before;ui_input(BUTTON_BACK);CHECK(page==MANUAL_MENU&&!manual_applies);
    manual_selection=1;ui_input(BUTTON_OK);CHECK(page==MANUAL_PREFIX_EDIT);
    draft.prefix=1;ui_input(BUTTON_DOWN_REPEAT);CHECK(draft.prefix==1);
    draft.prefix=30;ui_input(BUTTON_UP_REPEAT);CHECK(draft.prefix==30);
    draft.prefix=24;ui_input(BUTTON_OK);CHECK(page==MANUAL_MENU);
    manual_selection=2;ui_input(BUTTON_OK);gateway_selection=1;ui_input(BUTTON_OK);CHECK(page==MANUAL_GATEWAY_EDIT);
    for(int i=0;i<4;++i)ui_input(BUTTON_OK);CHECK(page==MANUAL_MENU&&!draft.gateway);
    manual_selection=3;dhcp_fixture.mode=WIRED_DHCP;dhcp_fixture.active=true;ui_input(BUTTON_OK);
    CHECK(page==MANUAL_REVIEW&&review_mode==WIRED_DHCP&&!manual_applies);ui_draw();
    ++dhcp_fixture.generation;ui_input(BUTTON_OK);CHECK(page==MANUAL_REVIEW&&!manual_applies&&manual_notice);
    ui_input(BUTTON_OK);CHECK(page==MANUAL_RESULTS&&manual_applies==1);
    ui_input(BUTTON_BACK);CHECK(page==MANUAL_MENU&&manual_cancels==1);
    dhcp_fixture.state=MANUAL_ACTIVE;dhcp_fixture.mode=WIRED_MANUAL;page=MANUAL_RESULTS;
    ui_input(BUTTON_BACK);CHECK(page==MANUAL_MENU&&manual_cancels==1&&dhcp_fixture.mode==WIRED_MANUAL);
    manual_selection=5;ui_input(BUTTON_OK);CHECK(page==DHCP_MENU&&uses_dhcp==1&&starts==1);
    for(int p=MANUAL_MENU;p<=MANUAL_RESULTS;++p){page=p;edit_octet=0;top=0;ui_draw();}
    page=MANUAL_REVIEW;review_selection=2;ui_input(BUTTON_OK);CHECK(page==MANUAL_MENU&&manual_applies==1);
    page=MANUAL_REVIEW;ui_input(BUTTON_BACK);CHECK(page==MANUAL_MENU&&manual_applies==1);
    // Link loss never applies an editor draft; the owner rejects a later stale APPLY.
    page=MANUAL_IP_EDIT;ui_input(BUTTON_BACK);CHECK(manual_applies==1);
    page=TESTS_MENU;tests_selection=2;dhcp_fixture.active=false;ui_input(BUTTON_OK);CHECK(page==PING_NO_IP);
    ui_input(BUTTON_OK);CHECK(page==MANUAL_MENU&&ping_starts==0);
    page=PING_NO_IP;ping_selection=1;ui_input(BUTTON_OK);CHECK(page==DHCP_MENU&&starts==1);
    page=TESTS_MENU;tests_selection=2;dhcp_fixture.active=true;ui_input(BUTTON_OK);CHECK(page==PING_MENU);
    ping_selection=1;ui_input(BUTTON_OK);CHECK(page==PING_CUSTOM&&ui_editing());
    uint32_t target_before=ping_custom;ui_input(BUTTON_UP_REPEAT);ui_input(BUTTON_DOWN_REPEAT);CHECK(ping_custom==target_before);
    for(int i=0;i<4;++i)ui_input(BUTTON_OK);CHECK(page==PING_REVIEW&&!ping_starts);
    ui_input(BUTTON_OK);CHECK(page==PING_RESULTS&&ping_starts==1);
    ui_input(BUTTON_BACK);CHECK(page==PING_MENU&&ping_cancels==1);
    for(int p=PING_MENU;p<=PING_RESULTS;++p){page=p;edit_octet=0;ui_draw();}
    page=PING_RESULTS;ping_fixture=(network_ping_status_t){.valid=true,.sent=4,.replies=4,.completed=4,.target=0xc0a80a01,.rtt_sum_us=12000,.detail="Ping complete"};ui_draw();
    ping_result_selection=0;ui_input(BUTTON_OK);CHECK(ping_starts==2);
    ping_fixture.active=false;ping_gateway=false;ping_start_error="Gateway required";ui_input(BUTTON_OK);
    CHECK(page==PING_REVIEW&&!strcmp(ping_notice,"Gateway required"));ping_start_error=NULL;
    page=PING_MENU;dhcp_fixture.active=false;ui_draw();CHECK(page==PING_NO_IP);
    puts("PASS: actual UI Web/Manual/Ping navigation, explicit Start, no-IP setup choices, shared accelerated editor, cancel/Again and display bounds");
    return 0;
}
