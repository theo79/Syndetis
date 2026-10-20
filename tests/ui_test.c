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
    ui_init();selection=3;ui_input(BUTTON_BACK);CHECK(page==MENU&&selection==3);
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
    for(selection=0;selection<5;++selection)ui_draw();selection=0;
    ui_draw_splash(0);ui_draw_splash(900);ui_draw_splash(1400);
    CHECK(enables==1&&disables==1);
    puts("PASS: actual UI Back never activates Web Access, parent levels, five-item scrolling and full supported long text bounds");
    return 0;
}
