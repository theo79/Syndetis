"""GCC constexpr fixture for actual Switch Info JSON handler/escaping.
Only SDK send/get calls and libc formatting are adapted; no hardware is used.
"""
import json
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/switch_info_api_test.cpp'
compiler=json.loads((ROOT/'build/project_description.json').read_text())['c_compiler']
web=(ROOT/'main/web_server.c').read_text()
escape=web[web.index('static void json_escape('):web.index('static esp_err_t status_json(')]
handler=web[web.index('static esp_err_t switch_info_json('):web.index('/* Single HTTP task owns this bounded snapshot')]
handler=handler[:handler.rfind('}')+1]
handler=handler.replace('static esp_err_t switch_info_json(httpd_req_t *req)',
                       'constexpr esp_err_t api_fixture(httpd_req_t *req, const switch_info_status_t &input)')
handler=handler.replace('static switch_info_status_t status;', 'switch_info_status_t status=input;')
handler=handler.replace('switch_info_get(&status);','')
escape=escape.replace('static void json_escape','constexpr void json_escape')
source=escape+handler
source=re.sub(r'\bsnprintf\b','test_format',source)
source=re.sub(r'\bstrlen\b','test_length',source)
model=(ROOT/'main/switch_info.h').read_text()
model=model[model.index('#define SWITCH_INFO_CAPTURE_SLOTS'):model.index('/* Call once')]
shim=r'''
#include "switch_info_parse.h"
#include <type_traits>
#define PRIu32 "u"
#define PRId64 "lld"
#define ESP_OK 0
#define HTTPD_500_INTERNAL_SERVER_ERROR "500"
typedef int esp_err_t;
struct httpd_req_t { char json[16384]={}; size_t used=0; bool failed=false; };
constexpr size_t test_length(const char *p) { size_t n=0; while(p[n]) ++n; return n; }
struct FormatArg { const char *text; uint64_t number; bool is_signed; };
template<class A> constexpr FormatArg format_arg(A value) {
    if constexpr(std::is_pointer_v<A>) return {value,0,false};
    else return {nullptr,(uint64_t)value,std::is_signed_v<A>};
}
template<class... A> constexpr int test_format(char *out,size_t capacity,const char *fmt,A... values) {
    FormatArg args[]={format_arg(values)...}; size_t used=0,index=0;
    auto append=[&](char c) constexpr { if(used+1<capacity) out[used]=c; ++used; };
    for(size_t i=0;fmt[i];++i) {
        if(fmt[i]!='%') { append(fmt[i]); continue; }
        ++i; unsigned width=0;
        while(fmt[i]>='0' && fmt[i]<='9') width=width*10+(fmt[i++]-'0');
        while(fmt[i]=='l' || fmt[i]=='z') ++i;
        FormatArg arg=args[index++];
        if(fmt[i]=='s') { for(size_t j=0;arg.text[j];++j) append(arg.text[j]); continue; }
        unsigned base=fmt[i]=='x' || fmt[i]=='X' ? 16:10;
        uint64_t number=arg.number;
        if(fmt[i]=='d' && arg.is_signed && (number>>63)) { append('-'); number=0-number; }
        const char *digits=fmt[i]=='X' ? "0123456789ABCDEF":"0123456789abcdef";
        char reverse[32]={}; unsigned count=0;
        do { reverse[count++]=digits[number%base]; number/=base; } while(number);
        while(count<width) reverse[count++]='0';
        while(count) append(reverse[--count]);
    }
    if(capacity) out[used<capacity ? used:capacity-1]=0;
    return (int)used;
}
constexpr int httpd_resp_set_type(httpd_req_t *,const char *) { return 0; }
constexpr int httpd_resp_set_hdr(httpd_req_t *,const char *,const char *) { return 0; }
constexpr int httpd_resp_send_err(httpd_req_t *req,const char *,const char *) { req->failed=true; return 1; }
constexpr int httpd_resp_send_chunk(httpd_req_t *req,const char *text,size_t size) {
    if(req->used+size>=sizeof(req->json)) return 1;
    for(size_t i=0;i<size;++i) req->json[req->used++]=text[i];
    req->json[req->used]=0; return 0;
}
constexpr bool contains(const char *text,const char *search) {
    for(size_t i=0;text[i];++i) {
        size_t j=0; while(search[j] && text[i+j] && search[j]==text[i+j]) ++j;
        if(!search[j]) return true;
    }
    return false;
}
'''
tests=r'''
constexpr int api_suite() {
    switch_info_status_t s={}; s.available=true; s.cdp_available=true;
    auto &n=s.neighbor; n.valid=true; n.protocol=SWITCH_PROTOCOL_CDP; n.cdp_version=2; n.ttl=180;
    n.device_id[0]='D'; n.system_name[0]='D'; n.port_id[0]='P'; n.platform[0]='"'; n.platform[1]='C';
    n.duplex_present=true; n.full_duplex=true; n.cdp_capabilities_present=true; n.cdp_capabilities=0xffffffff;
    n.pvid_present=true; n.pvid=20; n.management_ipv4_present=true;
    n.management_ipv4[0]=10; n.management_ipv4[3]=2;
    s.counters.candidates=s.counters.queued=s.counters.parsed=s.counters.malformed=0xffffffff;
    s.counters.oversize=s.counters.pool_full=s.counters.queue_full=s.counters.stale_generation=0xffffffff;
    s.counters.expired=s.counters.filter_failures=0xffffffff;
    s.counters.cdp_candidates=s.counters.cdp_parsed=s.counters.cdp_malformed=0xffffffff;
    s.free_heap=s.minimum_free_heap=s.worker_stack_free_bytes=0xffffffff;
    httpd_req_t req={};
    if(api_fixture(&req,s)!=0 || req.failed || req.json[0]!='{' || req.json[req.used-1]!='}') return 1;
    if(!contains(req.json,"\"protocol\":\"CDP\"") || !contains(req.json,"\"device_id\":\"D\"")) return 2;
    if(!contains(req.json,"\"management_ipv4\":\"10.0.0.2\"") || !contains(req.json,"\"pvid\":20")) return 3;
    if(!contains(req.json,"\"platform\":\"\\u0022C\"") || !contains(req.json,"\"duplex\":\"full\"")) return 4;
    if(!contains(req.json,"\"cdp_capabilities\":4294967295") || !contains(req.json,"\"cdp_version\":2")) return 5;
    if(contains(req.json,"supported_capabilities") || contains(req.json,"enabled_capabilities")) return 6;
    n.protocol=SWITCH_PROTOCOL_LLDP; n.cdp_capabilities_present=false; n.duplex_present=false;
    n.platform[0]=0; n.device_id[0]=0; n.capabilities_present=true; n.supported_capabilities=20; n.enabled_capabilities=4;
    req={}; if(api_fixture(&req,s)!=0 || !contains(req.json,"\"protocol\":\"LLDP\"")) return 7;
    if(!contains(req.json,"\"supported_capabilities\":20,\"enabled_capabilities\":4") || contains(req.json,"\"cdp_version\"")) return 8;
    /* Maximum escaped text capacities still fit every chunk and response. */
    for(unsigned i=0;i<sizeof(n.system_description)-1;++i) n.system_description[i]='"';
    req={}; if(api_fixture(&req,s)!=0 || req.failed) return 9;
    n.valid=false; s.cdp_available=false; s.cdp_setup_error=1;
    req={}; if(api_fixture(&req,s)!=0 || !contains(req.json,"\"protocol\":null")) return 10;
    if(contains(req.json,"\"port_id\"") || contains(req.json,"\"system_description\"") || contains(req.json,"\"ttl\"")) return 11;
    if(!contains(req.json,"\"available\":true") || !contains(req.json,"\"cdp_available\":false,\"cdp_setup_error\":1")) return 12;
    return 0;
}
static_assert(api_suite()==0,"Actual Switch Info JSON handler fixture failed");
'''
OUT.write_text(shim+model+source+tests)
subprocess.run([compiler,'-x','c++','-std=gnu++23','-Wall','-Wextra','-Werror','-Wno-sign-compare',
                '-fconstexpr-ops-limit=100000000','-I',str(ROOT/'main'),'-fsyntax-only',str(OUT)],check=True)
print('PASS: actual API handler/escaping: CDP fields/raw caps/platform/duplex, LLDP compatibility, omitted absent/invalid fields, partial filter failure, maximum counters and escaped text bounds.')
