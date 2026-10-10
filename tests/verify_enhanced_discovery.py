"""Evaluate production vendor/model bodies with normal GCC, without hardware."""
import json, subprocess
from pathlib import Path
R=Path(__file__).resolve().parents[1]
compiler=json.loads((R/'build/project_description.json').read_text())['c_compiler']
source=(R/'main/oui_vendor.c').read_text().replace('#include "oui_vendor.h"','').replace('static const struct','static constexpr struct').replace('bool oui_lookup_vendor(', 'constexpr bool oui_lookup_vendor(')
tests=r'''
constexpr int suite() {
 char out[32]={};uint8_t mac[6]={0,0x1b,0x1b,1,2,3};
 if(!oui_lookup_vendor(mac,out,32)||out[0]!='S')return 1;
 for(uint32_t prefix: {0x00000cU,0x000142U,0x000143U}) {
 mac[0]=prefix>>16;mac[1]=prefix>>8;mac[2]=prefix;
 if(!oui_lookup_vendor(mac,out,32)||out[0]!='C')return 2;
 }
 mac[0]=2;if(oui_lookup_vendor(mac,out,32)||out[0])return 3;
 mac[0]=1;if(oui_lookup_vendor(mac,out,32))return 4;
 for(auto &c:mac)c=255;
 if(oui_lookup_vendor(mac,out,32))return 5;
 for(auto &c:mac)c=0;
 if(oui_lookup_vendor(mac,out,32))return 6;
 mac[0]=0xfc;mac[1]=0xff;mac[2]=0xff;
 if(oui_lookup_vendor(mac,out,32))return 7;
 mac[0]=0;mac[1]=0x1b;mac[2]=0x1b;
 char tiny[3]={'x','x','!'};
 if(!oui_lookup_vendor(mac,tiny,2)||tiny[0]!='S'||tiny[1]||tiny[2]!='!')return 8;
 if(!oui_lookup_vendor(mac,tiny,1)||tiny[0])return 9;
 if(oui_lookup_vendor(nullptr,out,32)||oui_lookup_vendor(mac,nullptr,32)||oui_lookup_vendor(mac,out,0))return 10;
 discovery_device_t table[254]={};uint32_t count=0;
 discovery_observe(table,&count,10,mac);
 mac[5]=99;if(table[0].mac[5]!=0||!table[0].valid)return 11;
 discovery_observe(table,&count,10,mac);if(count!=1||table[0].mac[5]!=99)return 12;
 if(!oui_lookup_vendor(table[0].mac,table[0].vendor,32))return 13;
 table[0].vendor_present=true;mac[0]=2;
 discovery_observe(table,&count,10,mac);
 if(table[0].vendor_present||table[0].vendor[0]||!table[0].locally_administered)return 14;
 discovery_observe(table,&count,20,nullptr);if(count!=2||table[1].mac_valid)return 15;
 for(uint32_t i=30;i<400;++i)discovery_observe(table,&count,i,mac);
 if(count!=254)return 16;
 discovery_observe(table,&count,10,mac);if(count!=254)return 17;
 return 0;
}
static_assert(suite()==0);
static_assert(sizeof(discovery_device_t)==128);
static_assert(sizeof(entries)==144);
'''
out=R/'build/enhanced_vendor_model.cpp'
out.write_text('#include "discovery_logic.h"\n#include <initializer_list>\n#define OUI_VENDOR_LEN 32\n'+source+tests)
subprocess.run([compiler,'-x','c++','-std=gnu++23','-Wall','-Werror','-fconstexpr-ops-limit=200000000','-I',str(R/'main'),'-fsyntax-only',str(out)],check=True)
print('PASS: actual OUI lookup: verified Siemens/Cisco prefixes, unknown/zero/broadcast/multicast/private, truncation/termination/null/zero capacity. Actual model: MAC copy/update, vendor invalidation, absent MAC, duplicate and 254-record bounds. Record 128 bytes; OUI table 144 bytes.')
discovery=(R/'main/discovery.c').read_text()
getter=discovery[discovery.index('bool discovery_device('):discovery.index('bool discovery_hostname_result(')].replace('bool discovery_device(', 'constexpr bool discovery_device(')
lifecycle=r'''
#define portENTER_CRITICAL(x)
#define portEXIT_CRITICAL(x)
struct network_status_t {bool link_up;uint32_t revision;};
struct Fixture {
 bool ready=true;uint32_t requested_revision=7;
 struct {uint32_t found;} state={1};
 discovery_device_t devices[254]={};network_status_t current={true,7};
 constexpr void network_status_get(network_status_t *out){*out=current;}
'''+getter+r'''
};
constexpr bool lifecycle_test(){
 Fixture f={};discovery_device_t out={};
 f.devices[0].ip=10;f.devices[0].mac_valid=true;
 if(!f.discovery_device(0,&out)||out.ip!=10)return false;
 f.current.link_up=false;if(f.discovery_device(0,&out))return false;
 f.current.link_up=true;f.current.revision=8;
 if(f.discovery_device(0,&out))return false;
 f.current.revision=7;
 if(f.discovery_device(1,&out)||f.discovery_device(0,nullptr))return false;
 return true;
}
static_assert(lifecycle_test());
'''
out.write_text('#include "discovery_logic.h"\n'+lifecycle)
subprocess.run([compiler,'-x','c++','-std=gnu++23','-Wall','-Werror','-I',str(R/'main'),'-fsyntax-only',str(out)],check=True)
print('PASS: actual device getter rejects link loss, stale network generation, out-of-range index and null output; retained MAC cannot leak into reconnected context.')
