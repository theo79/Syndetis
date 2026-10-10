"""Evaluate the actual parser/test bodies with the configured cross compiler.

The installed ESP compiler has no host execution backend. Following the existing
project tests, C++ constexpr evaluates production bodies at compile time. Only
library calls (byte copy/compare, bounded formatting, structure zeroing) and
constexpr qualifiers are adapted, not protocol logic. The C firmware build
separately checks the unadapted code against the real ESP libc.
"""
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/switch_info_host'
OUT.mkdir(parents=True, exist_ok=True)
compiler=os.environ.get('SWITCH_INFO_TEST_COMPILER') or json.loads((ROOT/'build/project_description.json').read_text())['c_compiler']
model=(ROOT/'main/switch_info_parse.h').read_text()
model=re.sub(r'^bool (switch_info_\w+)\(',r'constexpr bool \1(',model,flags=re.M)
(OUT/'switch_info_parse.h').write_text(model)
(OUT/'switch_info_select.h').write_text((ROOT/'main/switch_info_select.h').read_text())
shim=r'''
#include "switch_info_parse.h"
#include "switch_info_select.h"
template<class A,class B> constexpr void test_copy(A *a,const B *b,size_t n) {
    for(size_t i=0;i<n;++i) a[i]=b[i];
}
template<class A> constexpr void test_set(A *a,int value,size_t n) {
    for(size_t i=0;i<n;++i) a[i]=(A)value;
}
template<class A,class B> constexpr int test_compare(const A *a,const B *b,size_t n) {
    for(size_t i=0;i<n;++i) if(a[i]!=b[i]) return a[i]<b[i] ? -1:1;
    return 0;
}
constexpr size_t test_length(const char *s) { size_t n=0; while(s[n]) ++n; return n; }
constexpr int test_strcmp(const char *a,const char *b) {
    for(size_t i=0;;++i) { if(a[i]!=b[i]) return a[i]<b[i] ? -1:1; if(!a[i]) return 0; }
}
template<class... A> constexpr int test_format(char *out,size_t capacity,const char *fmt,A... args) {
    unsigned values[]={static_cast<unsigned>(args)...};
    size_t used=0, index=0;
    for(size_t i=0;fmt[i];++i) {
        if(fmt[i]!='%') { if(used+1<capacity) out[used]=fmt[i]; ++used; continue; }
        ++i; unsigned width=0;
        if(fmt[i]=='0') { ++i; width=fmt[i++]-'0'; }
        unsigned base=fmt[i]=='u' ? 10:16, v=values[index++];
        const char *digits=fmt[i]=='X' ? "0123456789ABCDEF":"0123456789abcdef";
        char reverse[32]={}; unsigned count=0;
        do { reverse[count++]=digits[v%base]; v/=base; } while(v);
        while(count<width) reverse[count++]='0';
        while(count) { if(used+1<capacity) out[used]=reverse[--count]; else --count; ++used; }
    }
    if(capacity) out[used<capacity ? used:capacity-1]=0;
    return (int)used;
}
constexpr bool test_malformed(switch_neighbor_t *out) { *out={}; return false; }
'''
source=(ROOT/'main/switch_info_parse.c').read_text()
source+='\n'+(ROOT/'main/switch_info_cdp.c').read_text()
source=re.sub(r'^#include .*\n','',source,flags=re.M)
source=source.replace('static const uint8_t', 'static constexpr uint8_t')
source=source.replace('memset(out, 0, sizeof(*out));','*out = {};')
source=source.replace('goto malformed;', 'return test_malformed(out);')
source=source.replace('malformed:\n    *out = {};\n    return false;', '')
source=re.sub(r'^(static )?(bool|void|uint16_t|uint32_t) (\w+)\(',r'\1constexpr \2 \3(',source,flags=re.M)
test=(ROOT/'tests/switch_info_parse_test.c').read_text()
test=re.sub(r'^#include .*\n','',test,flags=re.M)
test=test.replace('static uint8_t frame[SWITCH_INFO_FRAME_SIZE];\nstatic size_t used;','')
test=test.replace('static void tlv(unsigned type,', 'constexpr void tlv(uint8_t *frame, size_t &used, unsigned type,')
test=test.replace('static void start(unsigned ttl)', 'constexpr void start(uint8_t *frame, size_t &used, unsigned ttl)')
test=test.replace('static void end(void)', 'constexpr void end(uint8_t *frame, size_t &used)')
test=re.sub(r'(?<!void )\btlv\(', 'tlv(frame, used, ',test)
test=re.sub(r'(?<!void )\bstart\(', 'start(frame, used, ',test)
test=test.replace('end();','end(frame, used);')
test=test.replace('int switch_info_test_suite(void)\n{','constexpr int switch_info_test_suite(void)\n{\n    uint8_t frame[SWITCH_INFO_FRAME_SIZE]={}; size_t used=0;')
# sizeof(frame) in start would be pointer size after parameter adaptation.
test=test.replace('memset(frame, 0, sizeof(frame));','memset(frame, 0, SWITCH_INFO_FRAME_SIZE);')
fixture=shim+source+test
for orig,replacement in [('memcpy','test_copy'),('memset','test_set'),('memcmp','test_compare'),
                         ('strcmp','test_strcmp'),('strlen','test_length'),('snprintf','test_format')]:
    fixture=re.sub(r'\b'+orig+r'\b',replacement,fixture)
fixture+='\nconstexpr int suite_result=switch_info_test_suite();\nstatic_assert(suite_result==0,"Parser test failed; reduced expression is the C test line");\n'
fixture+=r'''
constexpr bool mutation_bounds() {
    const uint8_t seed[]={1,0x80,0xc2,0,0,0x0e,0,1,2,3,4,5,0x88,0xcc,
        2,7,4,0,1,2,3,4,5,4,3,5,'p','0',6,2,0,120,0,0};
    for(size_t offset=0;offset<sizeof(seed);++offset) for(unsigned value=0;value<256;value+=16) {
        uint8_t input[sizeof(seed)]={}; test_copy(input,seed,sizeof(seed)); input[offset]=value;
        switch_neighbor_t result={};
        switch_info_parse_lldp(input,sizeof(input),&result);
    }
    return true; /* Invalid object access cannot be a constant expression. */
}
static_assert(mutation_bounds(),"Parser mutation accessed outside a C object");
'''
path=OUT/'parser_constexpr.cpp'; path.write_text(fixture)
extra=['-fconstexpr-steps=100000000','--target=xtensa-esp32-elf',
       '--sysroot=C:/Espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/xtensa-esp-elf'] if 'clang' in compiler else ['-fconstexpr-ops-limit=100000000']
subprocess.run([compiler,'-x','c++','-std=gnu++23','-Wall','-Wextra','-Werror',
    *extra,'-I',str(OUT),'-fsyntax-only',str(path)],check=True)
print('PASS: compile-time evaluation of actual C parser/classifier test bodies; library calls adapted, protocol logic unchanged.')

rx=(ROOT/'main/switch_info.c').read_text()
rx_body=rx[rx.index('bool switch_info_capture('):rx.index('static bool slot_current_locked')]
declarations=rx[rx.index('typedef struct {'):rx.index('static void increment(')]
declarations=declarations[:declarations.index('static capture_context_t context;')]
declarations=declarations.replace('bool attempted;', 'bool attempted; unsigned frees;')
model_api=(ROOT/'main/switch_info.h').read_text()
model_api=model_api[model_api.index('#define SWITCH_INFO_CAPTURE_SLOTS'):model_api.index('/* Call once')]
rx_shim=r'''
#include <limits.h>
typedef int esp_eth_handle_t;
typedef int esp_err_t;
typedef int QueueHandle_t;
typedef int TaskHandle_t;
struct esp_netif_t { unsigned forwards; uint8_t *last; uint32_t length; };
struct network_status_t { bool link_up; uint32_t link_generation; };
#define portENTER_CRITICAL(x)
#define portEXIT_CRITICAL(x)
#define ESP_OK 0
#define pdTRUE 1
constexpr int esp_netif_receive(esp_netif_t *n,uint8_t *p,uint32_t length,void *) {
    ++n->forwards; n->last=p; n->length=length; return 42;
}
constexpr int xQueueSend(int queue, uint8_t *,int wait) { return queue && wait==0; }
constexpr void increment(uint32_t *n) { if(*n!=UINT32_MAX) ++*n; }
'''
rx_body=rx_body.replace('bool switch_info_capture(uint8_t *buffer, uint32_t length)', 'constexpr bool capture_fixture(uint8_t *buffer, uint32_t length, capture_context_t &context, network_status_t test_network)')
router=(ROOT/'main/ethernet_rx.c').read_text(); router=router[router.index('static esp_err_t rx_input('):router.index('esp_err_t ethernet_rx_init(')]
router=router.replace('static esp_err_t rx_input','constexpr esp_err_t rx_input').replace('void *priv,void *info','capture_context_t *priv,void *info,network_status_t test_network').replace('switch_info_capture(buffer,length)','capture_fixture(buffer,length,*priv,test_network)').replace('profinet_capture(buffer,length)','false').replace('(esp_netif_t *)priv','priv->netif')
rx_body += router
rx_body=rx_body.replace('network_status_get(&network);','network = test_network;')
rx_body=rx_body.replace('esp_timer_get_time()','1000')
rx_body=rx_body.replace('free(buffer);','++c->frees;')
rx_body=rx_body.replace('memcpy(', 'test_copy(')
rx_tests=r'''
constexpr int routing_suite() {
    uint8_t frame[1601]={1,0x80,0xc2,0,0,0x0e}; frame[12]=0x88; frame[13]=0xcc;
    esp_netif_t netif={}; capture_context_t c={}; c.netif=&netif; c.queue=1; c.epoch=7; c.status.available=true;
    network_status_t network={true,9};
    const unsigned types[]={0x0800,0x86dd,0x0806,0x8100,0x88a8,0x2000,0xffff,100};
    for(unsigned type: types) {
        frame[12]=type>>8; frame[13]=type;
        if(rx_input(0,frame,60,&c,nullptr,network)!=42 || c.frees || netif.last!=frame || netif.length!=60) return 1;
    }
    frame[12]=0x88; frame[13]=0xcc; frame[0]=0xff;
    if(rx_input(0,frame,60,&c,nullptr,network)!=42 || c.frees) return 2;
    frame[0]=1;
    for(unsigned i=0;i<4;++i) {
        if(rx_input(0,frame,60,&c,nullptr,network)!=0 || c.frees!=i+1) return 3;
        if(!c.slots[i].used || c.slots[i].generation!=9 || c.slots[i].epoch!=7 ||
            !c.slots[i].link_up || c.slots[i].received_us!=1000 || c.slots[i].protocol!=SWITCH_PROTOCOL_LLDP || test_compare(c.slots[i].frame,frame,60)) return 4;
    }
    if(rx_input(0,frame,60,&c,nullptr,network)!=0 || c.frees!=5 || c.status.counters.pool_full!=1) return 5;
    if(rx_input(0,frame,1601,&c,nullptr,network)!=0 || c.frees!=6 || c.status.counters.oversize!=1) return 6;
    if(c.status.counters.candidates!=6 || c.status.counters.queued!=4) return 7;
    for(auto &slot:c.slots) slot.used=false;
    c.queue=0;
    if(rx_input(0,frame,60,&c,nullptr,network)!=0 || c.frees!=7 || c.status.counters.queue_full!=1 || c.slots[0].used) return 8;
    c.status.counters.candidates=UINT32_MAX;
    rx_input(0,frame,60,&c,nullptr,network);
    if(c.status.counters.candidates!=UINT32_MAX) return 9;
    capture_context_t d={}; d.netif=&netif; d.queue=1; d.epoch=8; d.status.available=true;
    uint8_t cdp[1601]={1,0,0x0c,0xcc,0xcc,0xcc,0,1,2,3,4,5,0,12,
                       0xaa,0xaa,3,0,0,0x0c,0x20,0,2,180,0,0};
    if(rx_input(0,cdp,26,&d,nullptr,network)!=0 || d.frees!=1 || d.status.counters.cdp_candidates!=1 ||
       d.slots[0].protocol!=SWITCH_PROTOCOL_CDP || d.slots[0].epoch!=8 || d.slots[0].generation!=9) return 10;
    cdp[21]=1;
    if(rx_input(0,cdp,26,&d,nullptr,network)!=42 || d.frees!=1) return 11;
    cdp[21]=0;
    if(rx_input(0,cdp,1601,&d,nullptr,network)!=0 || d.frees!=2 || d.status.counters.oversize!=1) return 12;
    d.slots[0].used=false; d.queue=0;
    if(rx_input(0,cdp,26,&d,nullptr,network)!=0 || d.frees!=3 || d.status.counters.queue_full!=1) return 13;
    d.status.counters.cdp_candidates=UINT32_MAX;
    rx_input(0,cdp,26,&d,nullptr,network);
    if(d.status.counters.cdp_candidates!=UINT32_MAX) return 14;
    return 0;
}
static_assert(routing_suite()==0,"Actual RX routing/pool ownership test failed");
'''
rx_path=OUT/'rx_constexpr.cpp'
rx_fixture=shim+source+rx_shim+model_api+declarations+rx_body+rx_tests
for orig,replacement in [('memcpy','test_copy'),('memcmp','test_compare'),('snprintf','test_format')]:
    rx_fixture=re.sub(r'\b'+orig+r'\b',replacement,rx_fixture)
rx_path.write_text(rx_fixture)
subprocess.run([compiler,'-x','c++','-std=gnu++23','-Wall','-Wextra','-Werror',
    *extra,'-I',str(OUT),'-fsyntax-only',str(rx_path)],check=True)
print('PASS: actual RX callback body with mocked SDK/free: normal forwarding including error return, 4 slots, oversize/pool/queue drops, one free per consumed frame, metadata, saturating counters.')

# Actual initialization with deterministic SDK outcomes; no live worker/network.
init_body=rx[rx.index('esp_err_t switch_info_init('):]
init_body=init_body.replace('esp_err_t switch_info_init(esp_eth_handle_t driver, esp_netif_t *netif)',
                           'constexpr esp_err_t init_fixture(esp_eth_handle_t driver, esp_netif_t *netif, capture_context_t &context)')
# constexpr cannot execute goto. Turn the actual common cleanup body into a
# local lambda and return it at each original failure jump; do not copy logic.
init_prefix, init_cleanup = init_body.split('failed:\n',1)
init_cleanup=init_cleanup.rsplit('}',1)[0]
init_cleanup=init_cleanup.replace('context.worker = NULL','context.worker = 0').replace('context.queue = NULL','context.queue = 0')
init_body=init_prefix.replace('    context.queue = xQueueCreate',
                             '    auto failed_cleanup = [&]() constexpr -> esp_err_t {\n'+init_cleanup+'    };\n    context.queue = xQueueCreate')
init_body=init_body.replace('goto failed;', 'return failed_cleanup();')+'}\n'
init_shim=r'''
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NO_MEM 0x101
#define ESP_EVENT_ANY_ID 0
#define ETH_EVENT 0
#define ETH_CMD_ADD_MAC_FILTER 1
#define ETH_CMD_DEL_MAC_FILTER 2
#define pdPASS 1
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
constexpr int worker=1,on_eth=1;
constexpr int xQueueCreate(unsigned,unsigned) { return 1; }
constexpr int xTaskCreate(int,const char *,unsigned,void *,unsigned,int *out) { *out=1; return 1; }
constexpr int esp_event_handler_register(int,int,int,void *) { return 0; }
constexpr int esp_event_handler_unregister(int,int,int) { return 0; }
constexpr int esp_eth_ioctl(int driver,int command,uint8_t *address) {
    if(command==ETH_CMD_ADD_MAC_FILTER && ((driver==2 && address[1]==0) || (driver==3 && address[1]==0x80))) return 1;
    return 0;
}
template<class F> constexpr int esp_eth_update_input_path_info(int driver,F,capture_context_t *) { return driver==4 ? 7:0; }
constexpr void xTaskNotifyGive(int) {}
constexpr void vTaskDelete(int) {}
constexpr void vQueueDelete(int) {}
'''
init_tests=r'''
constexpr int setup_suite() {
    esp_netif_t netif={}; capture_context_t c={};
    if(init_fixture(1,&netif,c)!=0 || !c.status.available || !c.status.cdp_available || c.status.cdp_setup_error) return 1;
    capture_context_t degraded={};
    if(init_fixture(2,&netif,degraded)!=0 || !degraded.status.available || degraded.status.cdp_available ||
       degraded.status.cdp_setup_error!=1 || degraded.status.counters.filter_failures!=1 || !degraded.queue || !degraded.worker) return 2;
    capture_context_t lldp_failed={};
    if(init_fixture(3,&netif,lldp_failed)!=1 || lldp_failed.status.available || lldp_failed.queue || lldp_failed.worker) return 3;
    capture_context_t callback_failed={};
    if(init_fixture(4,&netif,callback_failed)!=0 || !callback_failed.status.available || !callback_failed.queue || !callback_failed.worker) return 4; /* Router failure tested separately; Switch Info no longer installs callbacks. */
    if(init_fixture(1,&netif,c)!=ESP_ERR_INVALID_STATE) return 5;
    return 0;
}
static_assert(setup_suite()==0,"Actual optional setup/degraded filter fixture failed");
'''
init_path=OUT/'init_constexpr.cpp'
init_path.write_text(rx_fixture+init_shim+init_body+init_tests)
subprocess.run([compiler,'-x','c++','-std=gnu++23','-Wall','-Wextra','-Werror',
    *extra,'-I',str(OUT),'-fsyntax-only',str(init_path)],check=True)
print('PASS: actual initialization: both filters, CDP filter failure retains LLDP/callback/worker, LLDP failure cleanup; router ownership separate, repeated-init guard.')

# Verify approved startup/routing constraints without touching a board.
app=(ROOT/'main/app_main.c').read_text()
assert app.index('esp_netif_attach(')<app.index('switch_info_init(')<app.index('esp_eth_start(')
body=rx[rx.index('bool switch_info_capture('):rx.index('static bool slot_current_locked')]
assert 'return esp_netif_receive((esp_netif_t *)priv,buffer,length,NULL);' in (ROOT/'main/ethernet_rx.c').read_text()
assert body.count('free(buffer);')==1 and 'xQueueSend(c->queue, &index, 0)' in body
assert 'malloc(' not in body and 'ESP_LOG' not in body and 'parse_lldp(' not in body
assert 'ETH_CMD_S_PROMISCUOUS' not in rx and 'ETH_CMD_S_ALL_MULTICAST' not in rx
assert '# CONFIG_ESP_NETIF_L2_TAP is not set' in (ROOT/'sdkconfig').read_text()
print('PASS: startup order, forwarding ownership source invariants, zero-wait capture, TAP/promiscuous/all-multicast disabled.')
