#include "switch_info_parse.h"
#include <string.h>
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static uint8_t frame[SWITCH_INFO_FRAME_SIZE];
static size_t used;
static void tlv(unsigned type, const uint8_t *p, size_t n)
{
    unsigned h = (type << 9) | (unsigned)n;
    frame[used++] = (uint8_t)(h >> 8); frame[used++] = (uint8_t)h;
    if (n) memcpy(frame+used, p, n);
    used += n;
}
static void start(unsigned ttl)
{
    memset(frame, 0, sizeof(frame));
    const uint8_t header[] = {1,0x80,0xc2,0,0,0x0e, 0,0x11,0x22,0x33,0x44,0x55, 0x88,0xcc};
    memcpy(frame, header, sizeof(header)); used = sizeof(header);
    const uint8_t chassis[] = {4,0,0x11,0x22,0x33,0x44,0x55};
    const uint8_t port[] = {5,'G','i','1','/','0','/','2','4'};
    uint8_t lifetime[] = {(uint8_t)(ttl >> 8),(uint8_t)ttl};
    tlv(1,chassis,sizeof(chassis)); tlv(2,port,sizeof(port)); tlv(3,lifetime,2);
}
static void end(void) { tlv(0,0,0); }

/* Exported by the Windows runner; also ordinary portable C for gcc/clang. */
int switch_info_test_suite(void)
{
    switch_neighbor_t n, saved;
    start(120); end();
    CHECK(switch_info_parse_lldp(frame,used,&n));
    CHECK(n.valid && n.ttl == 120 && n.protocol == SWITCH_PROTOCOL_LLDP);
    CHECK(strcmp(n.chassis_id,"00:11:22:33:44:55") == 0);
    CHECK(strcmp(n.port_id,"Gi1/0/24") == 0);
    CHECK(!n.system_name[0] && !n.pvid_present && !n.management_ipv4_present);
    CHECK(!memcmp(n.source_mac,frame+6,6));
    saved=n;
    CHECK(switch_info_same_neighbor(&n,&saved));
    n.link_generation=4; n.expiration_us=1000;
    CHECK(switch_info_neighbor_current(&n,true,4,999));
    CHECK(!switch_info_neighbor_current(&n,true,4,1000));
    CHECK(!switch_info_neighbor_current(&n,false,4,900));
    CHECK(!switch_info_neighbor_current(&n,true,5,900));
    start(0); end(); CHECK(switch_info_parse_lldp(frame,used,&n) && !n.valid && !n.ttl);
    CHECK(switch_info_same_neighbor(&saved,&n));
    n.port_raw[n.port_raw_len-1]='5'; CHECK(!switch_info_same_neighbor(&saved,&n));

    start(65535);
    const uint8_t text[] = {'S','W','"','\\',0,0x1b,0xff};
    tlv(4,text,sizeof(text)); tlv(5,text,sizeof(text)); tlv(6,text,sizeof(text));
    const uint8_t caps[] = {0,0x14,0,4}; tlv(7,caps,4);
    const uint8_t mgmt[] = {5,1,192,168,1,2,2,0,0,0,1,0}; tlv(8,mgmt,sizeof(mgmt));
    const uint8_t pvid[] = {0,0x80,0xc2,1,0,100}; tlv(127,pvid,6); end();
    CHECK(switch_info_parse_lldp(frame,used,&n));
    CHECK(strcmp(n.system_name,"SW\"\\???") == 0 && strcmp(n.port_description,n.system_name)==0);
    CHECK(n.capabilities_present && n.supported_capabilities==0x14 && n.enabled_capabilities==4);
    CHECK(n.management_ipv4_present && n.management_ipv4[0]==192 && n.management_ipv4[3]==2);
    CHECK(n.pvid_present && n.pvid==100 && n.ttl==65535);
    /* Padding is ignored even when nonzero. */
    memset(frame+used,0xff,20); CHECK(switch_info_parse_lldp(frame,used+20,&n));
    /* Every prefix before End is a truncated advertisement. */
    for(size_t i=0;i<used;++i) { CHECK(!switch_info_parse_lldp(frame,i,&n)); CHECK(!n.valid && !n.chassis_id[0]); }

    start(120); uint8_t long_text[255]; memset(long_text,'X',sizeof(long_text));
    tlv(5,long_text,255); end(); CHECK(switch_info_parse_lldp(frame,used,&n));
    CHECK(strlen(n.system_name)==SWITCH_INFO_TEXT_SIZE-1);
    start(120); uint8_t family[] = {2,99,0x12,1,0,0,0,0,0}; tlv(8,family,sizeof(family)); end();
    CHECK(switch_info_parse_lldp(frame,used,&n) && !n.management_ipv4_present);
    start(120); uint8_t ipv6[24]={17,2}; ipv6[18]=1; tlv(8,ipv6,sizeof(ipv6)); end();
    CHECK(switch_info_parse_lldp(frame,used,&n));
    start(120); tlv(8,mgmt,sizeof(mgmt)-1); end(); CHECK(!switch_info_parse_lldp(frame,used,&n));
    start(120); tlv(127,pvid,3); end(); CHECK(!switch_info_parse_lldp(frame,used,&n));
    start(120); tlv(127,pvid,5); end(); CHECK(!switch_info_parse_lldp(frame,used,&n));
    start(120); uint8_t unknown[]={1,2,3,99}; tlv(127,unknown,4); end(); CHECK(switch_info_parse_lldp(frame,used,&n));
    start(120); tlv(0,text,1); CHECK(!switch_info_parse_lldp(frame,used,&n));
    start(120); end(); frame[14]=0xff; frame[15]=0xff; CHECK(!switch_info_parse_lldp(frame,used,&n));
    start(120); tlv(3,caps,2); end(); CHECK(!switch_info_parse_lldp(frame,used,&n));
    start(120); uint8_t bad_caps[]={0,1,0,2}; tlv(7,bad_caps,4); end(); CHECK(!switch_info_parse_lldp(frame,used,&n));
    start(120); for(unsigned i=0;i<SWITCH_INFO_MAX_TLVS;++i) tlv(9,0,0); end();
    CHECK(!switch_info_parse_lldp(frame,used,&n));

    /* Text chassis/local, network chassis IPv4/IPv6, all port subtypes. */
    for(unsigned subtype=1;subtype<=7;++subtype) {
        start(30); used=14;
        const uint8_t chassis_text[]={7,'l','o','c','a','l'};
        tlv(1,chassis_text,sizeof(chassis_text));
        uint8_t port_id[]={ (uint8_t)subtype,'p','o','r','t'};
        uint8_t mac_id[]={3,0,1,2,3,4,5};
        uint8_t ip_id[]={4,1,10,0,0,1};
        if(subtype==3) tlv(2,mac_id,sizeof(mac_id));
        else if(subtype==4) tlv(2,ip_id,sizeof(ip_id));
        else tlv(2,port_id,sizeof(port_id));
        uint8_t ttl[]={0,30}; tlv(3,ttl,2); end();
        CHECK(switch_info_parse_lldp(frame,used,&n) && strcmp(n.chassis_id,"local")==0);
    }
    start(30); used=14;
    uint8_t chassis_ip[]={5,1,10,2,3,4}; tlv(1,chassis_ip,sizeof(chassis_ip));
    uint8_t port_text[]={5,'e','t','h','0'}; tlv(2,port_text,sizeof(port_text));
    uint8_t ttl[]={0,30}; tlv(3,ttl,2); end();
    CHECK(switch_info_parse_lldp(frame,used,&n) && strcmp(n.chassis_id,"10.2.3.4")==0);
    start(30); used=14; uint8_t chassis_v6[18]={5,2}; chassis_v6[17]=1;
    tlv(1,chassis_v6,sizeof(chassis_v6)); tlv(2,port_text,sizeof(port_text)); tlv(3,ttl,2); end();
    CHECK(switch_info_parse_lldp(frame,used,&n) && strcmp(n.chassis_id,"0:0:0:0:0:0:0:1")==0);
    start(30); used=14; uint8_t chassis_name[]={6,'e','t','h','0'};
    tlv(1,chassis_name,sizeof(chassis_name)); tlv(2,port_text,sizeof(port_text)); tlv(3,ttl,2); end();
    CHECK(switch_info_parse_lldp(frame,used,&n) && strcmp(n.chassis_id,"eth0")==0);

    start(30); end(); CHECK(switch_info_is_lldp(frame,used));
    CHECK(!switch_info_is_lldp(frame,13) && !switch_info_is_lldp(0,100));
    const unsigned types[]={0x0800,0x86dd,0x0806,0x8100,0x88a8,0x2000,100,0xffff};
    for(unsigned i=0;i<sizeof(types)/sizeof(types[0]);++i) {
        frame[12]=(uint8_t)(types[i]>>8); frame[13]=(uint8_t)types[i];
        CHECK(!switch_info_is_lldp(frame,used));
    }
    frame[12]=0x88; frame[13]=0xcc;
    frame[5]=3; CHECK(!switch_info_is_lldp(frame,used));
    memset(frame,0xff,6); CHECK(!switch_info_is_lldp(frame,used));
    start(30); end(); CHECK(!switch_info_parse_lldp(frame,sizeof(frame)+1,&n));
    CHECK(!switch_info_parse_lldp(frame,used,0));
    /* Raw identity equality does not use truncated/sanitized printable strings. */
    saved=n; saved.protocol=SWITCH_PROTOCOL_LLDP;
    saved.chassis_raw_len=200; saved.port_raw_len=2;
    memset(saved.chassis_raw,'X',200); saved.port_raw[0]=5; saved.port_raw[1]='a';
    n=saved; n.chassis_raw[199]='Y'; CHECK(!switch_info_same_neighbor(&saved,&n));
    return 0;
}
#ifdef SWITCH_INFO_TEST_MAIN
int main(void) { return switch_info_test_suite(); }
#endif
