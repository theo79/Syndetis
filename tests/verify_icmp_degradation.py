"""Execute production ICMP phase/common finish in target GCC constexpr fixtures."""
import json,subprocess
from pathlib import Path
R=Path(__file__).resolve().parents[1]
compiler=json.loads((R/'build/project_description.json').read_text())['c_compiler']
s=(R/'main/discovery.c').read_text()
phase=s[s.index('    unsigned processed = 0;'):s.index('static void task(')]
phase=phase[:phase.rfind('}')]
phase=phase.replace('.on_ping_success=success, .on_ping_end=ended','')
reap=s[s.index('    bool reaped = false, icmp_busy = false;'):s.index('    arp_job.index=index;')]
finish=s[s.index('static void finish('):s.index('static void scan(')].replace('static void finish(', 'constexpr void finish(')
model=(R/'main/discovery.h').read_text();model=model[model.index('typedef enum'):model.index('esp_err_t discovery_init')]
shim=r'''
#include "discovery_name.h"
#include "discovery_names.h"
#undef NULL
#define NULL 0
#define PROBES 4
#define TIMEOUT_MS 250
#define ESP_OK 0
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define ESP_PING_PROF_REQUEST 1
#define portENTER_CRITICAL(x)
#define portEXIT_CRITICAL(x)
#define ESP_LOGI(...) log(__VA_ARGS__)
#define ESP_LOGW(...) log(__VA_ARGS__)
typedef int esp_err_t;
struct esp_ping_config_t {unsigned count,timeout_ms,interval_ms,data_size,task_stack_size,interface;uint32_t target_addr;};
constexpr esp_ping_config_t ESP_PING_DEFAULT_CONFIG(){return {};}
constexpr uint32_t lwip_htonl(uint32_t x){return x;}
constexpr void ip_addr_set_ip4_u32(uint32_t *p,uint32_t x){*p=x;}
constexpr void log(const char *,const char *,...){}
constexpr void strncpy(char *out,const char *in,size_t n){for(size_t i=0;i<n;++i){out[i]=in[i];if(!in[i])break;}}
'''
fixture=r'''
struct Fixture {
 discovery_status_t state={};discovery_device_t devices[254]={};
 uint32_t run_token=7,requested_revision=8,active=7;
 int64_t now=0;int create_count=0,start_count=0,delete_count=0,stop_count=0,open=0,retiring=0;
 int fail_create=0,fail_start=0,send_fail=0,no_reply=0,late=0;
 bool link=true,names_called=false;int names_count=0,releases=0;
 struct probe {int done,handle;discovery_device_t device;uint32_t revision,token;} probes[4]={};
 struct {bool pending=false;} arp_job;
 struct esp_ping_callbacks_t {probe *cb_args;};
 probe *owners[32]={};bool live[32]={};
 constexpr int64_t esp_timer_get_time(){return now*1000;}
 constexpr int esp_get_free_heap_size(){return 100000;}
 constexpr int esp_get_minimum_free_heap_size(){return 90000;}
 constexpr int uxTaskGetStackHighWaterMark(int){return 2000;}
 constexpr void diagnostic_release(uint32_t token){if(token==active){active=0;++releases;}}
 constexpr bool interrupted(uint32_t revision){return !link||revision!=8;}
 constexpr const char *cancel_reason(){return "ABORTED_LINK_DOWN";}
 constexpr bool pause_checked(uint32_t,unsigned ms){now+=ms;retiring=0;return link;}
 constexpr int xSemaphoreTake(int &done,int wait){if(done){done=0;return pdTRUE;}now+=wait;return 0;}
 constexpr int esp_ping_new_session(const esp_ping_config_t *,const esp_ping_callbacks_t *cb,int *out){
  ++create_count;if(create_count==fail_create)return 1;
  *out=create_count;owners[*out]=cb->cb_args;live[*out]=true;++open;return ESP_OK;
 }
 constexpr int esp_ping_start(int h){
  ++start_count;if(start_count==fail_start)return 1;
  probe *p=owners[h];p->done=h==late?0:1;
  if(h!=send_fail&&h!=no_reply&&h!=late){p->device.icmp_responsive=true;p->device.rtt_ms=2;}
  return ESP_OK;
 }
 constexpr void esp_ping_stop(int h){++stop_count;if(h!=late)owners[h]->done=1;}
 constexpr int esp_ping_get_profile(int h,int,uint32_t *sent,size_t){*sent=h==send_fail?0:1;return ESP_OK;}
 constexpr void esp_ping_delete_session(int h){if(!live[h]){delete_count+=1000;return;}live[h]=false;--open;++delete_count;++retiring;}
 constexpr discovery_hostname_status_t discovery_names_run(uint32_t token,uint32_t revision,unsigned count){
  names_called=true;names_count=count;discovery_hostname_status_t names={};
  for(unsigned i=0;i<count;++i)if(discovery_name_apply(devices,count,devices[i].ip,"host.local",token,active,revision,8,link,true))++names.resolved;
  return names;
 }
'''
tests=r'''
 constexpr void run(){
  state={};state.state=DISC_RUNNING;state.found=5;names_called=false;names_count=0;active=run_token=7;
  for(unsigned i=0;i<5;++i){devices[i]={};devices[i].valid=devices[i].arp_present=devices[i].mac_valid=true;devices[i].ip=10+i;devices[i].mac[5]=i;devices[i].vendor[0]='S';devices[i].vendor_present=true;devices[i].last_seen_ms=1;devices[i].discovery_source=1;}
  struct {uint32_t revision;} n={8};unsigned hosts=5;int index=1;int64_t phase_start=now*1000,scan_deadline=phase_start+120000000;
'''+reap+phase+r'''
 }
};
constexpr bool preserved(Fixture &f){
 if(f.state.state!=DISC_COMPLETE||f.state.found!=5||!f.names_called||f.names_count!=5||f.releases<1||f.active)return false;
 for(unsigned i=0;i<5;++i)if(f.devices[i].ip!=10+i||f.devices[i].mac[5]!=i||!f.devices[i].vendor_present||f.devices[i].vendor[0]!='S'||!f.devices[i].arp_present||!f.devices[i].hostname_present||f.devices[i].last_seen_ms!=1)return false;
 return true;
}
constexpr int suite(){
 Fixture f={};f.fail_create=1;f.run();
 if(!preserved(f)||f.state.icmp_unmeasured!=5||f.state.icmp_setup_failures!=1||f.open||f.delete_count)return 1;
 for(unsigned i=0;i<5;++i)if(f.devices[i].ping_measured||f.devices[i].rtt_ms||f.devices[i].packet_loss||f.devices[i].icmp_responsive)return 2;
 f={};f.fail_create=3;f.run();
 if(!preserved(f)||f.state.icmp_measured!=2||f.state.icmp_unmeasured!=3||f.delete_count!=2||f.open||f.retiring)return 3;
 for(unsigned i=0;i<5;++i)if(f.devices[i].ping_measured!=(i<2)||f.devices[i].rtt_ms!=(i<2?2U:0U))return 4;
 f={};f.no_reply=1;f.run();
 if(!preserved(f)||!f.devices[0].ping_measured||f.devices[0].icmp_responsive||f.devices[0].packet_loss!=100)return 5;
 f={};f.send_fail=1;f.run();
 if(!preserved(f)||f.devices[0].ping_measured||f.devices[0].packet_loss||f.devices[0].rtt_ms)return 6;
 f={};f.fail_start=3;f.run();
 if(!preserved(f)||f.delete_count!=3||f.open||f.retiring||f.state.icmp_measured!=2)return 7;
 // Reuse coordinator and records across success/failure/success/partial/success.
 f={};for(int failure:{0,1,0,3,0}){
 f.create_count=f.start_count=0;f.fail_create=failure;f.run();
 if(!preserved(f)||f.open||f.retiring||f.state.icmp_measured!=(failure==1?0U:failure==3?2U:5U))return 8;
 }
 f={};f.link=false;f.run();if(f.state.state!=DISC_ERROR||f.state.found||f.names_called||f.active)return 9;
 // Missing completion quarantines permanent storage without erasing ARP hosts.
 f={};f.late=1;f.run();if(!preserved(f)||f.open!=1||!f.probes[0].handle||f.stop_count!=1)return 10;
 f.run();if(!preserved(f)||f.state.icmp_measured||f.open!=1||f.create_count!=4)return 11;
 // Once the late completion arrives, next run reaps it before reusing storage.
 f.probes[0].done=1;f.late=0;f.create_count=f.start_count=0;
 f.run();if(!preserved(f)||f.open||f.state.icmp_measured!=5)return 12;
 return 0;
}
static_assert(suite()==0);
'''
out=R/'build/icmp_degradation.cpp';out.write_text('#include <initializer_list>\n'+shim+model+fixture+finish+tests)
subprocess.run([compiler,'-x','c++','-std=gnu++23','-Wall','-Werror','-fconstexpr-ops-limit=300000000','-I',str(R/'main'),'-fsyntax-only',str(out)],check=True)
patch=(R/'patches/ping_sock.c').read_text()
assert patch.index('int socket_errno=errno;') < patch.index('"socket() failed errno=') < patch.index('"create socket failed: %d"')
print('PASS: actual ICMP phase/finish: 5 ARP hosts preserved through total/partial creation failure, start failure, send failure, missing completion; measured timeout differs from unmeasured; completed results survive and PTR executes; cancellation remains fatal; cleanup/retirement and repeated coordinator reuse; errno captured before logging/cleanup.')
