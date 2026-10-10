"""Evaluate actual discovery JSON, OLED and navigation branches with GCC."""
import json, re, subprocess
from pathlib import Path
R=Path(__file__).resolve().parents[1]
compiler=json.loads((R/'build/project_description.json').read_text())['c_compiler']
base=(R/'tests/verify_switch_info_api.py').read_text()
shim=base.split("shim=r'''",1)[1].split("'''",1)[0].replace('char json[16384]', 'char json[300000]')
web=(R/'main/web_server.c').read_text()
escape=web[web.index('static void json_escape('):web.index('static esp_err_t status_json(')].replace('static void','constexpr void').replace('snprintf','test_format')
handler=web[web.index('static esp_err_t discovery_json('):web.index('/* One HTTP task owns scratch;')]
handler=handler.replace('static esp_err_t discovery_json(httpd_req_t *req)','constexpr esp_err_t api_fixture(httpd_req_t *req, discovery_status_t d, const discovery_device_t *snapshot)')
handler=handler.replace('    static discovery_device_t snapshot[DISCOVERY_MAX_HOSTS];','').replace('    discovery_status_t d;','').replace('    discovery_snapshot(&d, snapshot);','').replace('discovery_device_t *host=', 'const discovery_device_t *host=').replace('snprintf','test_format')
model=(R/'main/discovery.h').read_text();model=model[model.index('typedef enum'):model.index('esp_err_t discovery_init')]
extra=r'''
#include "discovery_logic.h"
#include "discovery_name.h"
#include "discovery_names.h"
#define ESP_FAIL 1
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(p) ((p)->addr>>24), (((p)->addr>>16)&255), (((p)->addr>>8)&255), ((p)->addr&255)
struct esp_ip4_addr_t {uint32_t addr;};
constexpr uint32_t esp_netif_htonl(uint32_t x){return x;}
'''
tests=r'''
constexpr int suite(){
 discovery_status_t d={};discovery_device_t hosts[254]={};httpd_req_t req={};
 if(api_fixture(&req,d,hosts)||!contains(req.json,"\"devices\":[]"))return 1;
 d.found=1;hosts[0].ip=0xc0a80132;hosts[0].mac_valid=true;hosts[0].mac[5]=0xab;
 hosts[0].arp_present=true;hosts[0].icmp_responsive=true;hosts[0].ping_measured=true;hosts[0].rtt_ms=1;
 hosts[0].vendor_present=true;hosts[0].vendor[0]='"';hosts[0].vendor[1]='\\';
 req={};if(api_fixture(&req,d,hosts))return 2;
 if(!contains(req.json,"\"mac\":\"00:00:00:00:00:AB\"")||!contains(req.json,"\"vendor\":\"\\u0022\\u005c\""))return 3;
 if(!contains(req.json,"\"reachable\":true")||!contains(req.json,"\"latency_ms\":1")||contains(req.json,"\"hostname\":"))return 4;
 hosts[0].locally_administered=true;req={};if(api_fixture(&req,d,hosts)||!contains(req.json,"Local/Private"))return 5;
 hosts[0]={};req={};if(api_fixture(&req,d,hosts)||!contains(req.json,"\"mac\":null")||!contains(req.json,"\"vendor\":null"))return 6;
 d.found=254;for(auto &h:hosts){h.hostname_present=h.vendor_present=true;for(unsigned j=0;j<63;++j)h.hostname[j]='"';for(unsigned j=0;j<31;++j)h.vendor[j]='"';}
 req={};if(api_fixture(&req,d,hosts)||req.failed||req.json[req.used-1]!='}')return 7;
 hosts[0]={};hosts[0].hostname_present=true;hosts[0].hostname[0]='P';hosts[0].hostname[1]='"';hosts[0].hostname[2]='\\';
 d.found=1;req={};if(api_fixture(&req,d,hosts)||!contains(req.json,"\"hostname\":\"P\\u0022\\u005c\""))return 8;
 hosts[0]={};hosts[0].arp_present=true;req={};
 if(api_fixture(&req,d,hosts)||!contains(req.json,"\"ping_response\":null")||!contains(req.json,"\"reachable\":true")||!contains(req.json,"\"latency_ms\":null"))return 10;
 hosts[0].ping_measured=true;req={};
 if(api_fixture(&req,d,hosts)||!contains(req.json,"\"ping_response\":false")||!contains(req.json,"\"latency_ms\":null"))return 11;
 d.found=0;for(unsigned i=0;i<79;++i)d.error[i]='"';
 d.hostname={true,true,0xffffffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff};
 req={};if(api_fixture(&req,d,hosts)||req.failed)return 9;
 return 0;
}
static_assert(suite()==0);
'''
def check(name,source):
 out=R/('build/'+name+'.cpp');out.write_text(source)
 subprocess.run([compiler,'-x','c++','-std=gnu++23','-Wall','-Werror','-Wno-sign-compare','-fconstexpr-ops-limit=300000000','-I',str(R/'main'),'-fsyntax-only',str(out)],check=True)
 print('PASS: '+name)
check('enhanced_api',extra+shim+model+'constexpr const char *discovery_state_name(discovery_state_t){return "complete";}\n'+escape+handler+tests)
menu=(R/'main/ui_menu.c').read_text()
render=menu[menu.index('    case DISC_DEVICE:'):menu.index('    case MAIN:',menu.index('    case DISC_DEVICE:'))]
render=render.replace('snprintf','test_format').replace('%.21s','%s')
# Formatter shim supports precision for the real bounded vendor row below.
shim_ui=shim.replace("++i; unsigned width=0;", "++i; unsigned precision=0; bool limited=false; if(fmt[i]=='.') { limited=true; ++i; while(fmt[i]>='0' && fmt[i]<='9') precision=precision*10+(fmt[i++]-'0'); } unsigned width=0;").replace('arg.text[j];++j','arg.text[j] && (!limited || j<precision);++j')
render=render.replace('"%s",d.locally_administered','"%.21s",d.locally_administered').replace('"%s",name','"%.21s",name')
nav=menu[menu.index('    } else if (screen == DISC_DEVICE) {'):menu.index('    } else if (screen == TEST_READY)')]
nav=nav.replace('    } else if (screen == DISC_DEVICE) {','    if (screen == DISC_DEVICE) {')+'    }\n'
fixture=r'''
enum {DISC_DEVICE,DISC_LIST};enum {UI_BUTTON_UP,UI_BUTTON_DOWN,UI_BUTTON_OK};
struct Fixture {
 discovery_status_t status={}; discovery_device_t host={}; unsigned device_selection=0;int screen=DISC_DEVICE;
 constexpr void discovery_get(discovery_status_t *d){*d=status;}
 constexpr bool discovery_device(unsigned i,discovery_device_t *d){*d=host;return i<status.found;}
 constexpr void render(char rows[8][22]){switch(screen){
'''+render+r'''
}}
constexpr void navigate(int button){
'''+nav+r'''
}
};
constexpr int ui_suite(){
 Fixture f={};char rows[8][22]={};f.status.found=1;f.host.ip=0xc0a80132;
 f.render(rows);if(!contains(rows[0],"DEVICE 1/1")||!contains(rows[3],"Unknown")||!contains(rows[4],"Unknown"))return 1;
 f.host.mac_valid=true;f.host.mac[5]=0xab;f.host.vendor_present=true;
 for(unsigned j=0;j<31;++j)f.host.vendor[j]='X';
 f.render(rows);if(rows[4][21]||test_length(rows[4])!=21||!contains(rows[3],"00:00:00:00:00:AB"))return 2;
 f.host.locally_administered=true;f.render(rows);if(!contains(rows[4],"Local/Private"))return 3;
 f.navigate(UI_BUTTON_DOWN);if(f.device_selection)return 4;
 f.host.ping_measured=true;f.status.found=3;f.navigate(UI_BUTTON_UP);if(f.device_selection!=2)return 5;
 f.navigate(UI_BUTTON_DOWN);if(f.device_selection)return 6;
 f.navigate(UI_BUTTON_OK);if(f.screen!=DISC_LIST)return 7;
 f.screen=DISC_DEVICE;f.status.found=0;f.navigate(UI_BUTTON_UP);if(f.screen!=DISC_LIST)return 8;
 f.screen=DISC_DEVICE;f.status.found=1;f.host.hostname_present=true;f.host.locally_administered=false;
 for(unsigned j=0;j<63;++j)f.host.hostname[j]='h';
 f.render(rows);if(test_length(rows[1])!=21||rows[1][21]||!contains(rows[2],"192.168.1.50")||test_length(rows[4])!=21||!contains(rows[5],"ICMP: NO REPLY"))return 9;
 f.host.hostname[3]='\n';f.render(rows);if(!contains(rows[1],"192.168.1.50")||!contains(rows[2],"MAC:"))return 10;
 f.host.ping_measured=false;f.render(rows);if(!contains(rows[5],"ICMP: N/A"))return 11;
 f.host.ping_measured=true;f.render(rows);if(!contains(rows[5],"ICMP: NO REPLY"))return 12;
 f.host.icmp_responsive=true;f.host.rtt_ms=2;f.render(rows);if(!contains(rows[5],"RTT: 2 ms"))return 13;
 return 0;
}
static_assert(ui_suite()==0);
'''
check('enhanced_oled',extra+shim_ui+model+fixture)
