"""Target GCC evaluates production PTR codec and name publication, no hardware."""
import json,re,subprocess
from pathlib import Path
R=Path(__file__).resolve().parents[1]
compiler=json.loads((R/'build/project_description.json').read_text())['c_compiler']
header=(R/'main/ptr_dns.h').read_text().replace('#pragma once','').replace('size_t ptr_dns_query(', 'constexpr size_t ptr_dns_query(').replace('ptr_dns_result_t ptr_dns_response(', 'constexpr ptr_dns_result_t ptr_dns_response(')
codec=(R/'main/ptr_dns.c').read_text().replace('#include "ptr_dns.h"',header)
codec=re.sub(r'^(static )?(uint16_t|void|size_t|bool|ptr_dns_result_t) (\w+)\(',r'\1constexpr \2 \3(',codec,flags=re.M)
def check(name,source):
 out=R/('build/'+name+'.cpp');out.write_text(source)
 subprocess.run([compiler,'-x','c++','-std=gnu++23','-Wall','-Werror','-fconstexpr-ops-limit=300000000','-I',str(R/'main'),'-fsyntax-only',str(out)],check=True)
 print('PASS: '+name)
tests=r'''
constexpr bool equal(const char *a,const char *b){for(unsigned i=0;i<64;++i){if(a[i]!=b[i])return false;if(!a[i])return true;}return false;}
constexpr size_t response(uint8_t *p,const char *host){
 size_t n=ptr_dns_query(p,512,123,0xc0a80132);p[2]=0x81;p[3]=0x80;p[7]=1;
 p[n++]=0xc0;p[n++]=12;write16(p+n,12);n+=2;write16(p+n,1);n+=2;
 for(unsigned i=0;i<4;++i)p[n++]=0;
 size_t len=n;n+=2;size_t start=0;
 while(host[start]){size_t end=start;while(host[end]&&host[end]!='.')++end;p[n++]=end-start;for(size_t i=start;i<end;++i)p[n++]=host[i];start=host[end]?end+1:end;}
 p[n++]=0;write16(p+len,n-len-2);return n;
}
constexpr int codec_suite(){
 uint8_t p[512]={};char out[64]={};
 size_t n=response(p,"plc-Line-01.localdomain");
 if(ptr_dns_response(p,n,123,0xc0a80132,out)!=PTR_FOUND||!equal(out,"plc-Line-01.localdomain"))return 1;
 if(ptr_dns_response(p,n,124,0xc0a80132,out)!=PTR_IGNORE||ptr_dns_response(p,n,123,0xc0a80133,out)!=PTR_IGNORE)return 2;
 for(size_t i=0;i<n;++i)if(ptr_dns_response(p,i,123,0xc0a80132,out)==PTR_FOUND)return 3;
 n=ptr_dns_query(p,512,123,0xc0a80132);p[2]=0x81;p[3]=0x83;
 if(ptr_dns_response(p,n,123,0xc0a80132,out)!=PTR_UNRESOLVED)return 4;
 p[3]=0x80;if(ptr_dns_response(p,n,123,0xc0a80132,out)!=PTR_UNRESOLVED)return 5;
 p[2]|=2;if(ptr_dns_response(p,n,123,0xc0a80132,out)!=PTR_UNRESOLVED)return 6;
 char max[65]={};for(unsigned i=0;i<63;++i)max[i]='a';
 n=response(p,max);if(ptr_dns_response(p,n,123,0xc0a80132,out)!=PTR_FOUND||out[63])return 7;
 max[63]='a';n=response(p,max);if(ptr_dns_response(p,n,123,0xc0a80132,out)==PTR_FOUND)return 8;
 n=response(p,"");if(ptr_dns_response(p,n,123,0xc0a80132,out)==PTR_FOUND)return 9;
 n=response(p,"bad\001name");if(ptr_dns_response(p,n,123,0xc0a80132,out)==PTR_FOUND)return 10;
 n=response(p,"<script>");if(ptr_dns_response(p,n,123,0xc0a80132,out)==PTR_FOUND)return 11;
 // Owner compression self-reference/forward reference must not loop.
 size_t q=ptr_dns_query(p,512,123,0xc0a80132);n=response(p,"good.local");
 p[q+1]=q;if(ptr_dns_response(p,n,123,0xc0a80132,out)==PTR_FOUND)return 12;
 // Compressed PTR target with a legitimate backward suffix reference (arpa).
 n=response(p,"plc.local");size_t data=q+12;
 p[data]=3;p[data+1]='p';p[data+2]='l';p[data+3]='c';p[data+4]=0xc0;p[data+5]=31;
 // For 50.1.168.192.in-addr.arpa, arpa label starts at byte 33.
 p[data+5]=33;write16(p+q+10,6);n=data+6;
 if(ptr_dns_response(p,n,123,0xc0a80132,out)!=PTR_FOUND||!equal(out,"plc.arpa"))return 13;
 if(ptr_dns_query(nullptr,512,1,1)||ptr_dns_query(p,1,1,1))return 14;
 if(ptr_dns_response(nullptr,0,1,1,out)!=PTR_IGNORE||ptr_dns_response(p,n,123,0xc0a80132,nullptr)!=PTR_IGNORE)return 15;
 return 0;
}
static_assert(codec_suite()==0);
constexpr int name_suite(){
 char out[64]={};if(!discovery_name_valid("plc-Line-01.local.",out)||!equal(out,"plc-Line-01.local"))return 1;
 for(const char *bad:{"",".","a..b","-a","a-","a b","a\n","a\177","a/b","a\\b","a\"b","<script>"})if(discovery_name_valid(bad,out))return 2;
 char max[66]={};for(unsigned i=0;i<63;++i)max[i]='x';max[63]='.';
 if(!discovery_name_valid(max,out)||out[63])return 3;
 max[63]='x';if(discovery_name_valid(max,out))return 4;
 if(discovery_name_valid(nullptr,out)||discovery_name_valid("x",nullptr))return 5;
 discovery_device_t devices[3]={};for(unsigned i=0;i<3;++i){devices[i].valid=true;devices[i].ip=i+10;devices[i].mac[0]=2;devices[i].rtt_ms=7;devices[i].vendor_present=true;}
 // Results arrive in reverse order; no duplicate/incorrect-IP publication.
 for(int i=2;i>=0;--i)if(!discovery_name_apply(devices,3,i+10,"host.local",7,7,8,8,true,true))return 6;
 if(discovery_name_apply(devices,3,10,"replacement",7,7,8,8,true,true))return 7;
 devices[0].hostname_present=false;
 if(discovery_name_apply(devices,3,10,"stale",7,9,8,8,true,true)||discovery_name_apply(devices,3,10,"stale",7,7,8,9,true,true))return 8;
 if(discovery_name_apply(devices,3,10,"stale",7,7,8,8,false,true)||discovery_name_apply(devices,3,10,"stale",7,7,8,8,true,false))return 9;
 if(discovery_name_apply(devices,3,99,"missing",7,7,8,8,true,true)||discovery_name_apply(devices,0,10,"missing",7,7,8,8,true,true))return 10;
 devices[0].valid=false;if(discovery_name_apply(devices,3,10,"missing",7,7,8,8,true,true))return 11;
 if(devices[1].mac[0]!=2||devices[1].rtt_ms!=7||!devices[1].vendor_present)return 12;
 return 0;
}
static_assert(name_suite()==0);
'''
check('hostname_codec_model','#include <initializer_list>\n'+codec+tests)
transport=(R/'main/discovery_names.c').read_text()
transport=transport[transport.index('typedef struct { int fd;'):]
transport=transport.replace('static bool name_current(', 'constexpr bool name_current(').replace('discovery_hostname_status_t discovery_names_run(', 'constexpr discovery_hostname_status_t discovery_names_run(')
transport=transport.replace('(struct sockaddr *)&local','&local').replace('(struct sockaddr *)&remote','&remote')
shim=r'''
#define AF_INET 2
#define SOCK_DGRAM 2
#define IPPROTO_UDP 17
#define MSG_DONTWAIT 1
#define FIONBIO 1
#define EAGAIN 11
#define EWOULDBLOCK 11
#define OTA_RECEIVING 1
#define OTA_SUCCESS 2
#define pdMS_TO_TICKS(x) (x)
struct addr_t {uint32_t s_addr;};
struct sockaddr_in {int sin_family;uint16_t sin_port;addr_t sin_addr;};
struct network_status_t {bool link_up=true,ipv4_ready=true;uint32_t revision=8;struct {struct {uint32_t addr;} ip;} ipv4={{1}};struct {uint32_t addr;} dns[2]={{0x08080808},{0}};};
struct ota_status_t {int phase=0;};
constexpr uint32_t lwip_ntohl(uint32_t x){return x;}
constexpr uint32_t lwip_htonl(uint32_t x){return x;}
constexpr uint16_t htons(uint16_t x){return x;}
struct Fixture {
 int64_t now=0;int errno=0,nextfd=0,open=0,maxopen=0,close_count=0,mode=0;
 int cancel_at=-1;bool no_dns=false,fail_socket=false,fail_ioctl=false;
 unsigned sends=0,random_id=100,published=0;uint32_t published_ip[16]={};
 uint8_t packet[512]={};
 struct {bool live;uint8_t query[64];size_t n;int64_t sent;} sockets[32]={};
 constexpr int64_t esp_timer_get_time(){return now*1000;}
 constexpr void vTaskDelay(int delay){now+=delay;}
 constexpr void network_status_get(network_status_t *n){*n={};if(cancel_at>=0&&now>=cancel_at){n->link_up=false;n->revision=9;}}
 constexpr void network_status_inspect(network_status_t *n){network_status_get(n);if(no_dns)n->dns[0].addr=0;}
 constexpr void firmware_update_status(ota_status_t *o){*o={};}
 constexpr bool diagnostic_current(uint32_t token){return token==7;}
 constexpr bool discovery_device(unsigned index,discovery_device_t *h){*h={};h->ip=0xc0a80100+index+2;return true;}
 constexpr bool discovery_hostname_result(uint32_t ip,const char *name,uint32_t token,uint32_t revision){
  if(!name_current(token,revision)||name[0]!='h'||published>=16)return false;
  published_ip[published++]=ip;return true;
 }
 constexpr uint32_t esp_random(){return random_id++;}
 constexpr int socket(int,int,int){if(fail_socket)return -1;int fd=nextfd++;sockets[fd].live=true;++open;if(open>maxopen)maxopen=open;return fd;}
 constexpr int ioctl(int,int,int *){return fail_ioctl?-1:0;}
 constexpr int bind(int,const sockaddr_in *,size_t){return 0;}
 constexpr int connect(int,const sockaddr_in *,size_t){return 0;}
 constexpr int send(int fd,const uint8_t *p,size_t n,int){for(size_t i=0;i<n;++i)sockets[fd].query[i]=p[i];sockets[fd].n=n;sockets[fd].sent=now;++sends;return n;}
 constexpr void close(int fd){if(sockets[fd].live){sockets[fd].live=false;--open;++close_count;}}
 constexpr int recv(int fd,uint8_t *p,size_t,int){
  // No answer; or out-of-order answers: first slot responds after 200ms, others 10ms.
  if(!mode || now-sockets[fd].sent<(fd==0?200:10)){errno=EAGAIN;return -1;}
  size_t n=sockets[fd].n;for(size_t i=0;i<n;++i)p[i]=sockets[fd].query[i];p[2]=0x81;p[3]=mode==2?0x83:0x80;
  if(mode==2)return n;
  p[7]=1;p[n++]=0xc0;p[n++]=12;write16(p+n,12);n+=2;write16(p+n,1);n+=2;for(unsigned i=0;i<4;++i)p[n++]=0;
  write16(p+n,8);n+=2;p[n++]=1;p[n++]='h';p[n++]=4;p[n++]='t';p[n++]='e';p[n++]='s';p[n++]='t';p[n++]=0;
  return n;
 }
'''
transport_tests=r'''
};
constexpr int transport_suite(){
 Fixture f={};auto m=f.discovery_names_run(7,8,5);
 if(f.maxopen!=2||f.open||m.resolved||!m.timeout||m.duration_ms>2500||f.sends>5)return 1;
 f={};m=f.discovery_names_run(7,8,254);
 if(m.duration_ms!=2500||f.open||m.timeout!=m.attempted||f.sends>16)return 11;
 f={};f.mode=1;m=f.discovery_names_run(7,8,5);
 if(m.resolved!=5||m.attempted!=5||f.maxopen>2||f.open||m.duration_ms>2500)return 2;
 if(f.published_ip[0]!=0xc0a80103)return 3; // second request finishes first
 f={};f.mode=2;m=f.discovery_names_run(7,8,254);
 if(m.attempted!=16||m.resolved||m.failed!=16||f.sends!=16||f.open||f.maxopen>2)return 4;
 f={};f.fail_socket=true;m=f.discovery_names_run(7,8,5);
 if(m.attempted!=5||m.failed!=5||f.sends||f.open||m.duration_ms>2500)return 5;
 f={};f.fail_ioctl=true;m=f.discovery_names_run(7,8,5);
 if(m.failed!=5||f.open||f.close_count!=5)return 6;
 f={};f.cancel_at=60;m=f.discovery_names_run(7,8,5);
 if(!m.cancelled||f.open||f.published||m.duration_ms>70)return 7;
 // After cancellation sockets/receive queues are gone before a new run.
 f.cancel_at=-1;f.mode=1;m=f.discovery_names_run(7,8,1);
 if(m.resolved!=1||f.open)return 8;
 f={};f.no_dns=true;m=f.discovery_names_run(7,8,254);
 if(m.attempted||f.sends||f.open||m.duration_ms)return 9;
 f={};m=f.discovery_names_run(9,8,5);if(!m.cancelled||f.sends)return 10;
 return 0;
}
static_assert(transport_suite()==0);
static_assert(sizeof(discovery_hostname_status_t)==28);
static_assert(sizeof(Fixture::name_request_t)==24);
'''
check('hostname_transport',codec+'\n#include "discovery_names.h"\n'+shim+transport+transport_tests)
print('PASS: production worker loop: 2 concurrent sockets, 2500ms global cap, no retries, 16-query cap, timeout/no PTR/no DNS, setup failure, out-of-order completion, cancellation/link loss, cleanup before next scan. No hardware execution.')
