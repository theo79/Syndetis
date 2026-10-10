"""GCC constexpr evaluates production protocol and mocked transport logic; no board access."""
import json,re,subprocess
from pathlib import Path
R=Path(__file__).resolve().parents[1]
compiler=json.loads((R/'build/project_description.json').read_text())['c_compiler']
def check(name,source):
 p=R/('build/'+name+'.cpp');p.write_text(source,encoding='utf-8')
 subprocess.run([compiler,'-x','c++','-std=gnu++23','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-fconstexpr-ops-limit=300000000','-I',str(R/'main'),'-fsyntax-only',str(p)],check=True)
 print('PASS: '+name)
check('pn_protocol','#include <initializer_list>\n'+(R/'tests/profinet_dcp_test.cpp').read_text())
sdk=(R/'sdkconfig').read_text()
for setting in ['ETH_TRANSMIT_MUTEX','LWIP_TCPIP_CORE_LOCKING','ESP_NETIF_L2_TAP']:
 assert '# CONFIG_'+setting+' is not set' in sdk
calls=[]
for p in (R/'main').glob('*.c'):
 for match in re.finditer(r'esp_eth_transmit\s*\(',p.read_text()):calls.append((p.name,match.start()))
assert len(calls)==1 and calls[0][0]=='profinet.c'
s=(R/'main/profinet.c').read_text();cb=s[s.index('static void transmit_on_tcpip'):s.index('bool profinet_capture')]
assert 'esp_eth_transmit(' in cb and 'tcpip_try_callback(transmit_on_tcpip,NULL)' in s
assert 'build_identify_all(context.mac,context.sequence,context.job.frame)' in s
assert 'ETH_CMD_ADD_MAC_FILTER' not in s and 'ipv4_ready' not in s
print('PASS: TX source/configuration safety boundary')

# Compile the actual scanner as a constexpr fixture object. SDK/RTOS calls are
# deterministic methods; production state transitions, RX and callback bodies stay intact.
src=(R/'main/profinet.c').read_text()
src=re.sub(r'^#include.*\n','',src,flags=re.M)
src=re.sub(r'#if CONFIG_ETH_TRANSMIT_MUTEX.*?#endif\n','',src,flags=re.S)
src=src.replace('static struct {','struct {').replace('} context;','} context={};').replace('static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;','')
src=src.replace('static profinet_device_t parsed;','profinet_device_t parsed={};').replace('static int64_t next_log;', 'int64_t next_log=0;')
src=src.replace('static ', '')
src=re.sub(r'^(void|bool|pn_state_t|esp_err_t|pn_start_t) (\w+)\(',r'constexpr \1 \2(',src,flags=re.M)
src=src.replace('const char *profinet_state_name','constexpr const char *profinet_state_name')
src=src.replace('memcpy(', 'test_copy(').replace('memset(', 'test_zero(').replace('free(buffer);','++frees;')
src=src.replace('tcpip_try_callback(transmit_on_tcpip,NULL)','tcpip_try_callback(nullptr,nullptr)').replace('xTaskCreate(worker,','xTaskCreate(nullptr,')
api=(R/'main/profinet.h').read_text();api=api[api.index('typedef enum'):api.index('esp_err_t profinet_init')]
shim=r"""
#include <initializer_list>
#include <stddef.h>
#include <stdint.h>
#include "profinet_dcp.h"
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_NO_MEM 3
#define ETH_CMD_G_MAC_ADDR 1
#define OTA_RECEIVING 1
#define OTA_SUCCESS 2
#define pdTRUE 1
#define pdPASS 1
#define ERR_OK 0
#define portENTER_CRITICAL(x)
#define portEXIT_CRITICAL(x)
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGI(...) ((void)0)
typedef int esp_err_t;
typedef int esp_eth_handle_t;
typedef int *QueueHandle_t;
typedef int *TaskHandle_t;
struct network_status_t {bool link_up=true;uint32_t physical_epoch=1,revision=1;bool ipv4_ready=false;};
struct ota_status_t {int phase=0;};
template<class A,class B> constexpr void test_copy(A *a,const B *b,size_t n){for(size_t i=0;i<n;++i)a[i]=b[i];}
template<class A> constexpr void test_zero(A *a,int,size_t){*a={};}
"""
fixture=r"""
struct ScannerFixture {
 int handle=1;int64_t clock=1;network_status_t network={};int ota=0;
 unsigned tx_calls=0,releases=0,posts=0,frees=0;uint32_t token=0;
 bool fail_post=false,delay_callback=false,fail_tx=false,fail_queue=false,notify=false,in_callback=false;
 unsigned change_kind=0;int64_t change_at=0;
 uint8_t queue_items[8]={};unsigned queued=0;
 constexpr int64_t esp_timer_get_time(){return clock;}
 constexpr void network_status_get(network_status_t *n){*n=network;}
 constexpr void firmware_update_status(ota_status_t *o){o->phase=ota;}
 constexpr bool diagnostic_current(uint32_t t){return token==t&&t;}
 constexpr uint32_t diagnostic_acquire(){if(token)return 0;token=5;return token;}
 constexpr void diagnostic_release(uint32_t t){if(t==token){token=0;++releases;}}
 constexpr uint32_t esp_random(){return 9;}
 constexpr unsigned esp_get_free_heap_size(){return 100000;}
 constexpr unsigned esp_get_minimum_free_heap_size(){return 90000;}
 constexpr unsigned uxTaskGetStackHighWaterMark(void *){return 2000;}
 constexpr int esp_eth_ioctl(int,int,uint8_t *m){for(unsigned i=0;i<6;++i)m[i]=local_mac[i];return 0;}
 constexpr int *xQueueCreate(unsigned,unsigned){return &handle;}
 constexpr int xTaskCreate(void *,const char *,unsigned stack,void *,unsigned priority,int **out){if(stack!=4096||priority!=1)return 0;*out=&handle;return 1;}
 constexpr void vQueueDelete(int *){}
 constexpr void xTaskNotifyGive(int *){notify=true;}
 constexpr unsigned ulTaskNotifyTake(int,unsigned){return 0;}
 constexpr int xQueueSend(int *,uint8_t *index,int wait){if(fail_queue||queued==8||wait)return 0;queue_items[queued++]=*index;return 1;}
 constexpr int xQueueReceive(int *,uint8_t *index,unsigned wait){if(!queued){vTaskDelay(wait);return 0;}*index=queue_items[0];for(unsigned i=1;i<queued;++i)queue_items[i-1]=queue_items[i];--queued;return 1;}
 constexpr void vTaskDelay(unsigned ms){clock+=ms*1000;
   if(change_at&&clock>=change_at){change_at=0;if(change_kind==1)network.link_up=false;if(change_kind==2)++network.physical_epoch;if(change_kind==3)++network.revision;if(change_kind==4)ota=OTA_RECEIVING;if(change_kind==5)profinet_cancel();}
 }
 constexpr int tcpip_try_callback(void *,void *){++posts;if(fail_post)return 1;if(!delay_callback){in_callback=true;transmit_on_tcpip(nullptr);in_callback=false;}return 0;}
 constexpr int esp_eth_transmit(int,uint8_t *,size_t n){if(!in_callback||n!=60)return 7;++tx_calls;return fail_tx?3:0;}
"""
checks=r"""
};
constexpr bool lifecycle_suite(){
 ScannerFixture f;if(f.profinet_init(1)!=0||f.profinet_start()!=PN_START_OK)return false;
 if(f.network.ipv4_ready||f.profinet_start()!=PN_START_BUSY)return false;
 f.scan();if(f.tx_calls!=1||f.releases!=1||f.context.status.state!=PN_COMPLETE||f.context.status.packets_sent!=1||f.clock<3000000)return false;
 if(f.profinet_start()!=PN_START_COOLDOWN){return false;}f.clock=10000002;if(f.profinet_start()!=PN_START_OK)return false;
 f.profinet_cancel();f.scan();if(f.tx_calls!=1||f.releases!=2||f.context.status.state!=PN_CANCELLED)return false;
 for(unsigned kind:{1u,2u,3u,4u,5u}){ScannerFixture g;g.profinet_init(1);g.profinet_start();g.change_kind=kind;g.change_at=500000;g.scan();if(g.tx_calls!=1||g.releases!=1||g.context.status.state==PN_COMPLETE)return false;}
 ScannerFixture a;a.profinet_init(1);a.network.link_up=false;if(a.profinet_start()!=PN_START_LINK_DOWN||a.posts)return false;
 ScannerFixture b;b.profinet_init(1);b.fail_post=true;b.profinet_start();b.scan();if(b.tx_calls||b.releases!=1||b.context.job.pending||b.context.status.state!=PN_TX_ERROR)return false;
 ScannerFixture c;c.profinet_init(1);c.delay_callback=true;c.profinet_start();c.scan();if(c.tx_calls||c.releases!=1||!c.context.job.pending||c.context.status.counters.tx_callback_timeout!=1)return false;
 c.clock=11000000;if(c.profinet_start()!=PN_START_BUSY)return false;c.in_callback=true;c.transmit_on_tcpip(nullptr);c.in_callback=false;
 if(c.tx_calls||c.context.job.pending||c.profinet_start()!=PN_START_OK)return false;
 ScannerFixture d;d.profinet_init(1);d.fail_tx=true;d.profinet_start();d.scan();if(d.tx_calls!=1||d.releases!=1||d.context.status.packets_sent||d.context.status.state!=PN_TX_ERROR)return false;
 return true;
}
static_assert(lifecycle_suite());
constexpr bool ownership_suite(){
 ScannerFixture f;f.profinet_init(1);f.profinet_start();f.in_callback=true;f.transmit_on_tcpip(nullptr);f.in_callback=false;
 uint8_t wire[1601]={};unsigned n=response(wire);wire[21]=f.context.status.xid;
 for(unsigned i=0;i<8;++i)if(!f.profinet_capture(wire,n))return false;
 if(f.frees!=8||f.queued!=8||f.context.status.counters.accepted!=8)return false;
 if(!f.profinet_capture(wire,n)||f.frees!=9||f.context.status.counters.pool_full!=1)return false;
 f.process_slot(0);f.process_slot(1);if(f.context.status.count!=1||f.context.status.counters.parsed!=2)return false;
 f.context.slots[2].token=99;f.process_slot(2);if(f.context.status.counters.stale!=1)return false;
 f.fail_queue=true;if(!f.profinet_capture(wire,n)||f.context.status.counters.queue_full!=1||f.context.slots[0].used)return false;
 f.fail_queue=false;if(!f.profinet_capture(wire,1601)||f.context.status.counters.oversize!=1)return false;
 f.context.status.counters.candidates=128;if(!f.profinet_capture(wire,n)||f.context.status.counters.candidate_limit!=1||!f.context.status.partial)return false;
 wire[21]^=1;unsigned frees=f.frees;if(f.profinet_capture(wire,n)||f.frees!=frees||f.context.status.counters.wrong_xid!=1)return false;wire[21]^=1;
 f.clock=f.context.close_us;if(f.profinet_capture(wire,n))return false;
 f.clock=5;++f.network.revision;if(f.profinet_capture(wire,n))return false;
 return true;
}
static_assert(ownership_suite());
"""
protocol=(R/'tests/profinet_dcp_test.cpp').read_text()
protocol=protocol[protocol.index('constexpr uint8_t local_mac'):protocol.index('constexpr int protocol_suite')]
check('pn_transport',shim+api+protocol+fixture+src+checks)
# Actual neutral router is evaluated with independent capture doubles.
router=(R/'main/ethernet_rx.c').read_text();router=router[router.index('static esp_err_t rx_input'):]
router=router.replace('static esp_err_t rx_input','constexpr esp_err_t rx_input').replace('esp_err_t ethernet_rx_init','constexpr esp_err_t ethernet_rx_init')
router=router.replace('(esp_netif_t *)priv','priv')
router=router.replace('void *priv,void *info','esp_netif_t *priv,void *info')
router_shim=r"""
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;typedef int esp_eth_handle_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 2
struct esp_netif_t{};
constexpr bool switch_info_capture(uint8_t *p,uint32_t){return p[0]==1;}
constexpr bool profinet_capture(uint8_t *p,uint32_t){return p[0]==2;}
constexpr int esp_netif_receive(esp_netif_t *,uint8_t *,uint32_t,void *){return 42;}
template<class F> constexpr int esp_eth_update_input_path_info(int h,F,esp_netif_t *){return h==4?7:0;}
"""
router_tests=r"""
constexpr bool router_suite(){esp_netif_t n;uint8_t b[1]={};if(rx_input(1,b,1,&n,nullptr)!=42)return false;b[0]=1;if(rx_input(1,b,1,&n,nullptr)!=0)return false;b[0]=2;if(rx_input(1,b,1,&n,nullptr)!=0)return false;return ethernet_rx_init(4,&n)==7&&ethernet_rx_init(1,&n)==0&&ethernet_rx_init(0,&n)==2;}
static_assert(router_suite());
"""
check('pn_router',router_shim+router+router_tests)

# Real API formatting/escaping, adapted only at the SDK snapshot/send boundary.
import runpy
api_test=runpy.run_path(str(R/'tests/verify_switch_info_api.py'))
web=(R/'main/web_server.c').read_text()
escape=web[web.index('static void json_escape('):web.index('static esp_err_t status_json(')].replace('static void','constexpr void')
handler=web[web.index('static esp_err_t pn_json_text'):web.index('esp_err_t web_server_start(')]
handler=handler.replace('static esp_err_t','constexpr esp_err_t')
handler=handler.replace('profinet_json(httpd_req_t *req)','profinet_json(httpd_req_t *req,const profinet_status_t &input,const profinet_device_t *records)')
handler=handler.replace('profinet_snapshot(&p,pn_http_snapshot);','p=input;profinet_device_t pn_http_snapshot[PN_MAX_DEVICES]={};for(unsigned i=0;i<p.count;++i)pn_http_snapshot[i]=records[i];')
state=s[s.index('const char *profinet_state_name'):].replace('const char *profinet_state_name','constexpr const char *profinet_state_name')
shim=api_test['shim'].replace('char json[16384]','char json[131072]')+'\n#define ESP_FAIL 1\n'
source=escape+state+handler
source=re.sub(r'\bsnprintf\b','test_format',source);source=re.sub(r'\bstrlen\b','test_length',source)
api_cases=r"""
constexpr bool pn_api_suite(){
 profinet_status_t p={};p.state=PN_COMPLETE;p.count=32;p.partial=true;p.counters.candidates=UINT32_MAX;
 profinet_device_t d[32]={};for(auto &v:d){v.flags=PN_VALID|PN_NAME|PN_IDS|PN_IP|PN_VENDOR|PN_NAME_TRUNC;v.mac[0]=2;v.vendor_id=42;v.device_id=1028;v.ip_block_info=0x82;v.flags|=PN_IP_CONFLICT;for(unsigned i=0;i<240;++i)v.station_name[i]='"';v.vendor_value[0]='\\';}
 httpd_req_t req={};if(profinet_json(&req,p,d)||req.failed||!contains(req.json,"\\u0022")||!contains(req.json,"\\u005c")||!contains(req.json,"\"count\":32")||!contains(req.json,"\"vendor_id\":42")||!contains(req.json,"\"device_role\":null")||!contains(req.json,"\"ip_conflict\":true")||!contains(req.json,"\"station_name_truncated\":true"))return false;
 p.count=1;d[0].flags=PN_VALID;req={};if(profinet_json(&req,p,d)||!contains(req.json,"\"station_name\":null")||!contains(req.json,"\"ip\":null"))return false;
 p.count=0;p.state=PN_IDLE;req={};return !profinet_json(&req,p,d)&&contains(req.json,"\"devices\":[]");
}
static_assert(pn_api_suite());
"""
check('pn_api','#include "profinet_dcp.h"\n'+api+shim+source+api_cases)
# Production OLED formatter; formatting adapter additionally honors bounded %.*s.
formatter=api_test['shim']
formatter=formatter.replace("while(fmt[i]=='l' || fmt[i]=='z')", "unsigned precision=100000;if(fmt[i]=='.'){++i;precision=0;while(fmt[i]>='0'&&fmt[i]<='9')precision=precision*10+(fmt[i++]-'0');}while(fmt[i]=='l' || fmt[i]=='z')")
formatter=formatter.replace('arg.text[j];++j','arg.text[j]&&j<precision;++j')
ui=(R/'main/ui_profinet.c').read_text();ui=re.sub(r'^#include.*\n','',ui,flags=re.M)
ui=ui.replace('static void','constexpr void').replace('void ui_profinet_render','constexpr void ui_profinet_render')
ui=ui.replace('memset(rows,0,8*22);','{for(unsigned r=0;r<8;++r){for(unsigned c=0;c<22;++c){rows[r][c]=0;}}}')
ui=ui.replace('snprintf(','test_format(')
ui_cases=r"""
constexpr bool oled_suite(){char rows[8][22]={};profinet_device_t d={};d.flags=PN_VALID|PN_NAME|PN_IP|PN_IDS|PN_ROLE|PN_INSTANCE|PN_VENDOR|PN_ALIAS;d.vendor_id=42;d.device_id=1028;d.device_role=15;for(unsigned i=0;i<240;++i)d.station_name[i]='x';
 for(unsigned page=0;page<3;++page){ui_profinet_render(rows,page,0,32,&d);for(auto &row:rows)if(row[21])return false;}
 ui_profinet_render(rows,0,0,32,&d);if(!contains(rows[2],"IP not set")||!contains(rows[3],"002A"))return false;
 d.flags=PN_VALID;ui_profinet_render(rows,0,0,1,&d);return contains(rows[1],"Name not advertised")&&contains(rows[2],"IP not advertised");}
static_assert(oled_suite());
"""
check('pn_oled','#include "profinet_dcp.h"\n'+formatter+ui+ui_cases)

# Evaluate the production local menu branches, including explicit cancellation.
menu=(R/'main/ui_menu.c').read_text()
nav=menu[menu.index('} else if (screen == INDUSTRIAL'):menu.index('} else if (screen == SWITCH_INFO')]
nav=nav.replace('} else if','if',1)+'}'
nav_fixture=r'''
enum screen_t {MAIN,INDUSTRIAL,PN_MENU,PN_SCAN,PN_RESULTS,PN_DEVICE};
enum ui_button_t {UI_BUTTON_UP,UI_BUTTON_OK,UI_BUTTON_DOWN};
struct Menu {
 screen_t screen=INDUSTRIAL;unsigned industrial_selection=0,pn_selection=0,pn_device_index=0,pn_page=0;
 pn_start_t pn_start_result=PN_START_OK;bool pn_back_pending=false,cancelled=false;
 profinet_status_t status={};
 constexpr pn_start_t profinet_start(){return pn_start_result;}
 constexpr void profinet_cancel(){cancelled=true;}
 constexpr void profinet_get(profinet_status_t *out){*out=status;}
 constexpr void press(ui_button_t button){
'''+nav+r'''
 }
};
constexpr bool menu_suite(){Menu m;m.press(UI_BUTTON_OK);if(m.screen!=PN_MENU)return false;
 m.press(UI_BUTTON_UP);if(m.pn_selection!=2)return false;m.press(UI_BUTTON_DOWN);
 m.press(UI_BUTTON_OK);if(m.screen!=PN_SCAN)return false;m.press(UI_BUTTON_OK);
 if(!m.cancelled||!m.pn_back_pending||m.screen!=PN_SCAN)return false;
 m.screen=PN_RESULTS;m.status.state=PN_COMPLETE;m.status.count=32;m.press(UI_BUTTON_OK);
 if(m.screen!=PN_DEVICE)return false;m.press(UI_BUTTON_UP);
 if(m.pn_device_index!=31||m.pn_page!=2)return false;m.press(UI_BUTTON_DOWN);
 if(m.pn_device_index||m.pn_page)return false;m.press(UI_BUTTON_OK);
 return m.screen==PN_RESULTS;}
static_assert(menu_suite());
'''
nav_fixture=nav_fixture.replace('return false;m.', 'return false;\n m.')
check('pn_menu','#include "profinet_dcp.h"\n'+api+nav_fixture)
assert 'if(screen==PN_SCAN&&pn.state!=PN_RUNNING)' in menu
assert 'OTA_RECEIVING || ota->phase == OTA_SUCCESS' in menu
