
#include "profinet_dcp.h"
constexpr uint8_t local_mac[6]={2,1,2,3,4,5};
constexpr void put16(uint8_t *f,unsigned o,unsigned v){f[o]=v>>8;f[o+1]=v;}
constexpr unsigned response(uint8_t *f){
 for(unsigned i=0;i<1601;++i)f[i]=0;
 pn_copy(f,local_mac,6);f[6]=2;f[11]=9;put16(f,12,0x8892);put16(f,14,0xfeff);f[16]=5;f[17]=1;f[21]=7;return 26;
}
constexpr unsigned block(uint8_t *f,unsigned n,unsigned opt,unsigned sub,const uint8_t *v,unsigned count,unsigned info=0){
 f[n]=opt;f[n+1]=sub;put16(f,n+2,count+2);put16(f,n+4,info);pn_copy(f+n+6,v,count);n+=6+count;
 if(count&1){f[n++]=0;}put16(f,24,n-26);return n;
}
constexpr int protocol_suite(){
 uint8_t f[1601]={};profinet_device_t d={};pn_envelope_t e={};uint32_t dup=0;
 if(!build_identify_all(local_mac,0x12345678,f))return 1;
 const uint8_t exact[]={1,14,207,0,0,0,2,1,2,3,4,5,0x88,0x92,0xfe,0xfe,5,0,0x12,0x34,0x56,0x78,0,0x80,0,4,255,255,0,0};
 if(pn_compare(f,exact,30)){return 2;}for(unsigned i=30;i<60;++i)if(f[i])return 3;
 uint8_t bad[6]={};if(build_identify_all(bad,7,f)||build_identify_all(local_mac,0,f))return 4;
 unsigned n=response(f);if(!pn_parse(f,n,local_mac,7,&d,&dup)||!(d.flags&PN_VALID))return 5;
 uint8_t ip[]={192,168,10,20,255,255,255,0,192,168,10,1},ids[]={0,42,4,4},role[]={15,0},inst[]={1,2},options[]={1,2,2,3};
 uint8_t name[]={'p','l','c'},vendor[]={'x',1,255},alias[]={'p','o','r','t'};
 n=block(f,n,2,7,inst,2);n=block(f,n,2,2,name,3);n=block(f,n,1,2,ip,12,0x82);
 n=block(f,n,2,3,ids,4);n=block(f,n,2,4,role,2);n=block(f,n,2,1,vendor,3);n=block(f,n,2,6,alias,4);n=block(f,n,2,5,options,4);
 if(!pn_parse(f,n,local_mac,7,&d,&dup)||d.vendor_id!=42||d.device_id!=1028||d.device_instance!=258||d.device_role!=15||d.station_name[2]!='c'||d.vendor_value[1]!='?'||!(d.flags&PN_VENDOR_SAN)||!(d.flags&PN_IP_CONFLICT)||d.gateway[3]!=1)return 6;
 n=block(f,n,2,2,alias,4);if(!pn_parse(f,n,local_mac,7,&d,&dup)||dup!=1||d.station_name[0]!='p'||d.station_name[1]!='l')return 7;
 // Unknown does not need BlockInfo; zero value, then opaque OEM block.
 f[n]=99;f[n+1]=99;put16(f,n+2,0);n+=4;put16(f,24,n-26);n=block(f,n,2,8,ids,4);
 if(!pn_parse(f,n+10,local_mac,7,&d,&dup)||d.vendor_id!=42)return 8;
 unsigned valid=n;for(unsigned i=0;i<valid;++i)if(pn_parse(f,i,local_mac,7,&d,&dup))return 9;
 // All fixed header mismatches, wrong XID, destination, invalid source.
 for(unsigned o: {0u,6u,12u,14u,16u,17u,21u}){uint8_t old=f[o];f[o]^=1;if(pn_parse(f,n,local_mac,7,&d,&dup))return 10;f[o]=old;}
 for(unsigned id: {0xfefcu,0xfefdu,0xfefeu,0x8000u,0xfc01u,0xff00u}){put16(f,14,id);if(pn_parse(f,n,local_mac,7,&d,&dup))return 11;}put16(f,14,0xfeff);
 for(unsigned svc: {3u,4u,6u}){f[16]=svc;if(pn_parse(f,n,local_mac,7,&d,&dup))return 12;}f[16]=5;
 f[17]=5;if(pn_parse(f,n,local_mac,7,&d,&dup))return 13;f[17]=1;
 put16(f,24,0xffff);if(pn_parse(f,n,local_mac,7,&d,&dup))return 14;
 n=response(f);n=block(f,n,2,2,name,3);f[n-1]=1;if(pn_parse(f,n,local_mac,7,&d,&dup))return 15;f[n-1]=0;
 if(pn_parse(f,n-1,local_mac,7,&d,&dup))return 16;
 // Name empty, exact 240, over-limit flagged; vendor/alias truncation.
 uint8_t longtext[241]={};for(unsigned i=0;i<241;++i)longtext[i]='x';
 n=response(f);n=block(f,n,2,2,longtext,0);if(!pn_parse(f,n,local_mac,7,&d,&dup)||!(d.flags&PN_NAME)||d.station_name[0])return 17;
 n=response(f);n=block(f,n,2,2,longtext,240);if(!pn_parse(f,n,local_mac,7,&d,&dup)||(d.flags&PN_NAME_TRUNC)||d.station_name[240])return 18;
 n=response(f);n=block(f,n,2,2,longtext,241);n=block(f,n,2,1,longtext,100);n=block(f,n,2,6,longtext,100);
 if(!pn_parse(f,n,local_mac,7,&d,&dup)||!(d.flags&PN_NAME_TRUNC)||!(d.flags&PN_VENDOR_TRUNC)||!(d.flags&PN_ALIAS_TRUNC))return 19;
 // Single tag preserves VID/PCP; second/provider tag rejected.
 for(unsigned i=n;i>12;--i){f[i+3]=f[i-1];}put16(f,12,0x8100);put16(f,14,0xa123);n+=4;
 if(!pn_envelope(f,n,local_mac,&e)||!e.tagged||e.vid!=0x123||e.pcp!=5||!pn_parse(f,n,local_mac,7,&d,&dup))return 20;
 put16(f,14,0xc000);if(!pn_envelope(f,n,local_mac,&e)||e.vid||e.pcp!=6)return 21;
 put16(f,16,0x8100);if(pn_envelope(f,n,local_mac,&e))return 22;put16(f,16,0x8892);put16(f,12,0x88a8);if(pn_envelope(f,n,local_mac,&e))return 23;
 n=response(f);n=block(f,n,1,2,ip,11);if(pn_parse(f,n,local_mac,7,&d,&dup))return 24;
 n=response(f);n=block(f,n,2,5,options,3);if(pn_parse(f,n,local_mac,7,&d,&dup))return 25;
 for(unsigned info:{0u,1u,2u,0x80u,0x81u,0x82u,0x83u}){n=response(f);n=block(f,n,1,2,ip,12,info);if(!pn_parse(f,n,local_mac,7,&d,&dup)||bool(d.flags&PN_IP_CONFLICT)!=(info==0x80||info==0x81||info==0x82))return 26;}
 n=response(f);n=block(f,n,1,1,local_mac,6);if(!pn_parse(f,n,local_mac,7,&d,&dup)||!(d.flags&PN_MAC_MISMATCH))return 27;
 if(pn_parse(f,1601,local_mac,7,&d,&dup))return 28;
 // Structural field mutation is evaluated by GCC: an out-of-object access fails constexpr.
 n=response(f);n=block(f,n,2,2,name,3);for(unsigned o=0;o<n;++o)for(unsigned v=0;v<256;v+=17){uint8_t old=f[o];f[o]=v;pn_parse(f,n,local_mac,7,&d,&dup);f[o]=old;}
 return 0;
}
static_assert(protocol_suite()==0);
constexpr bool model_suite(){
 profinet_device_t table[32]={},b={};unsigned count=0;b.flags=PN_VALID|PN_NAME;b.station_name[0]='x';b.mac[0]=2;
 for(unsigned i=0;i<32;++i){b.mac[5]=31-i;if(!pn_observe(table,&count,&b))return false;}
 b.mac[5]=99;if(pn_observe(table,&count,&b)||count!=32)return false;
 b.mac[5]=1;b.flags=PN_VALID|PN_IDS;b.vendor_id=42;if(!pn_observe(table,&count,&b)||count!=32)return false;
 pn_sort(table,count);if(table[1].vendor_id!=42||table[1].station_name[0]!='x')return false;
 for(unsigned i=0;i<32;++i)if(table[i].mac[5]!=i)return false;
 return pn_context_current(1,1,2,2,3,3,true,false,false)&&!pn_context_current(1,2,2,2,3,3,true,false,false)&&!pn_context_current(1,1,2,4,3,3,true,false,false)&&!pn_context_current(1,1,2,2,3,4,true,false,false)&&!pn_context_current(1,1,2,2,3,3,false,false,false)&&!pn_context_current(1,1,2,2,3,3,true,true,false)&&!pn_context_current(1,1,2,2,3,3,true,false,true);
}
static_assert(model_suite());
