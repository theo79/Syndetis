/* Included after constexpr-adapted production functions by the Python runner. */
#define EXPECT(condition) do { if (!(condition)) return __LINE__; } while (0)
struct CdpFrame {
    uint8_t bytes[SWITCH_INFO_FRAME_SIZE] = {};
    size_t size = 26;
};
constexpr CdpFrame cdp_start(unsigned version=2, unsigned ttl=180) {
    CdpFrame f;
    const uint8_t header[]={1,0,0x0c,0xcc,0xcc,0xcc,0,1,2,3,4,5,0,0,
                            0xaa,0xaa,3,0,0,0x0c,0x20,0,0,0,0,0};
    test_copy(f.bytes,header,sizeof(header)); f.bytes[22]=version; f.bytes[23]=ttl;
    return f;
}
constexpr void cdp_tlv(CdpFrame &f,unsigned type,const uint8_t *p,size_t size) {
    f.bytes[f.size++]=type>>8; f.bytes[f.size++]=type;
    f.bytes[f.size++]=(size+4)>>8; f.bytes[f.size++]=size+4;
    test_copy(f.bytes+f.size,p,size); f.size+=size;
}
constexpr void cdp_seal(CdpFrame &f) {
    f.bytes[12]=(f.size-14)>>8; f.bytes[13]=f.size-14;
    f.bytes[24]=f.bytes[25]=0;
    /* Independent fixture producer: emulate the byte-padding transformation
     * used by Wireshark, then one's-complement fold. Fixed vectors below pin
     * even, odd low-byte and odd high-byte results independently of production. */
    uint8_t pdu[1502]={}; size_t size=f.size-22;
    test_copy(pdu,f.bytes+22,size);
    if (size%2) {
        pdu[size]=pdu[size-1]; pdu[size-1]=0;
        if (pdu[size]>=128) { --pdu[size]; --pdu[size-1]; }
        ++size;
    }
    uint32_t sum=0;
    for(size_t i=0;i<size;i+=2) sum+=(pdu[i]<<8)|pdu[i+1];
    while(sum>0xffff) sum=(sum&0xffff)+(sum>>16);
    unsigned checksum=(~sum)&0xffff;
    f.bytes[24]=checksum>>8; f.bytes[25]=checksum;
}
constexpr CdpFrame cdp_minimal(unsigned version=2,unsigned ttl=180) {
    CdpFrame f=cdp_start(version,ttl); const uint8_t device[]={'D'},port[]={'P'};
    cdp_tlv(f,1,device,1); cdp_tlv(f,3,port,1); cdp_seal(f); return f;
}
constexpr int cdp_parser_suite() {
    switch_neighbor_t n={}; CdpFrame f=cdp_minimal();
    EXPECT(f.bytes[24]==0xb0 && f.bytes[25]==0xf5);
    EXPECT(switch_info_is_cdp(f.bytes,f.size));
    EXPECT(switch_info_parse_cdp(f.bytes,f.size,&n));
    EXPECT(n.valid && n.protocol==SWITCH_PROTOCOL_CDP && n.ttl==180 && n.cdp_version==2);
    EXPECT(n.device_id[0]=='D' && n.chassis_id[0]=='D' && n.system_name[0]=='D' && n.port_id[0]=='P');
    EXPECT(n.source_mac[5]==5 && !n.capabilities_present && !n.management_ipv4_present);
    for(size_t size=0;size<f.size;++size) EXPECT(!switch_info_parse_cdp(f.bytes,size,&n));
    for(size_t i=14;i<22;++i) { CdpFrame bad=f; bad.bytes[i]^=1; EXPECT(!switch_info_is_cdp(bad.bytes,bad.size)); }
    CdpFrame bad=f; bad.bytes[0]=0; EXPECT(!switch_info_is_cdp(bad.bytes,bad.size));
    bad=f; bad.bytes[12]=0x20; bad.bytes[13]=0; EXPECT(!switch_info_is_cdp(bad.bytes,bad.size));
    bad=f; bad.bytes[12]=0x81; bad.bytes[13]=0; EXPECT(!switch_info_is_cdp(bad.bytes,bad.size));
    bad=f; bad.bytes[12]=0; bad.bytes[13]=11; EXPECT(!switch_info_is_cdp(bad.bytes,bad.size));
    bad=f; bad.bytes[12]=5; bad.bytes[13]=0xdd; EXPECT(!switch_info_is_cdp(bad.bytes,bad.size));
    bad=f; ++bad.bytes[13]; EXPECT(!switch_info_is_cdp(bad.bytes,bad.size));
    bad=f; bad.bytes[23]^=1; EXPECT(!switch_info_parse_cdp(bad.bytes,bad.size,&n) && !n.valid);
    bad=f; bad.bytes[22]=3; cdp_seal(bad); EXPECT(!switch_info_parse_cdp(bad.bytes,bad.size,&n));
    for(size_t i=f.size;i<60;++i) f.bytes[i]=0xff;
    EXPECT(switch_info_parse_cdp(f.bytes,60,&n) && n.ttl==180);
    const uint8_t low[]={1},high[]={0x80};
    f=cdp_minimal(); cdp_tlv(f,0x123,low,1); cdp_seal(f);
    EXPECT(f.bytes[24]==0xaf && f.bytes[25]==0xcc);
    EXPECT(switch_info_parse_cdp(f.bytes,f.size,&n));
    f=cdp_minimal(); cdp_tlv(f,0x123,high,1); cdp_seal(f);
    EXPECT(f.bytes[24]==0xb0 && f.bytes[25]==0x4d);
    EXPECT(switch_info_parse_cdp(f.bytes,f.size,&n));
    bad=f; bad.bytes[24]=0x2f; bad.bytes[25]=0xcd; /* RFC odd-byte padding differs from Cisco. */
    EXPECT(!switch_info_parse_cdp(bad.bytes,bad.size,&n));
    f=cdp_minimal(1,255); EXPECT(switch_info_parse_cdp(f.bytes,f.size,&n) && n.cdp_version==1 && n.ttl==255);
    f=cdp_minimal(2,0); EXPECT(switch_info_parse_cdp(f.bytes,f.size,&n) && !n.valid && n.chassis_raw_len==1);
    f=cdp_minimal(); const uint8_t software[]={'I','O','S','\n',0,'V'},platform[]={'C','9','3','0','0'};
    const uint8_t vlan[]={0,20},duplex[]={1},caps[]={0,0,0,0x29},name[]={'S','W'};
    const uint8_t addresses[]={0,0,0,2,1,1,0xcc,0,4,10,10,10,2,1,1,0xcc,0,4,10,10,10,3};
    cdp_tlv(f,2,addresses,sizeof(addresses)); cdp_tlv(f,4,caps,4); cdp_tlv(f,5,software,sizeof(software));
    cdp_tlv(f,6,platform,sizeof(platform)); cdp_tlv(f,0x0a,vlan,2); cdp_tlv(f,0x0b,duplex,1);
    cdp_tlv(f,0x14,name,2); cdp_seal(f);
    EXPECT(switch_info_parse_cdp(f.bytes,f.size,&n));
    EXPECT(n.management_ipv4_present && n.management_ipv4[0]==10 && n.management_ipv4[3]==2);
    EXPECT(n.cdp_capabilities_present && n.cdp_capabilities==0x29 && !n.capabilities_present);
    EXPECT(n.system_description[3]=='?' && n.system_description[4]=='?' && n.platform[0]=='C');
    EXPECT(n.pvid_present && n.pvid==20 && n.duplex_present && n.full_duplex && n.system_name[0]=='S');
    const uint8_t other_address[]={0,0,0,1,9,2,0xab,0xcd,0,3,1,2,3};
    const uint8_t snap_address[]={0,0,0,1,2,8,0xaa,0xaa,3,0,0,0,8,0,0,4,192,168,1,2};
    f=cdp_minimal(); cdp_tlv(f,2,other_address,sizeof(other_address)); cdp_tlv(f,0x16,snap_address,sizeof(snap_address)); cdp_seal(f);
    EXPECT(switch_info_parse_cdp(f.bytes,f.size,&n) && n.management_ipv4[0]==192);
    f=cdp_minimal(); cdp_tlv(f,2,other_address,sizeof(other_address)); cdp_seal(f);
    EXPECT(switch_info_parse_cdp(f.bytes,f.size,&n) && !n.management_ipv4_present);
    const uint8_t bad_address[]={0,0,0,1,1,1,0xcc,0,3,1,2,3};
    f=cdp_minimal(); cdp_tlv(f,2,bad_address,sizeof(bad_address)); cdp_seal(f);
    EXPECT(!switch_info_parse_cdp(f.bytes,f.size,&n));
    const uint8_t huge_count[]={0xff,0xff,0xff,0xff};
    f=cdp_minimal(); cdp_tlv(f,2,huge_count,4); cdp_seal(f);
    EXPECT(!switch_info_parse_cdp(f.bytes,f.size,&n));
    const uint8_t bad_vlan[]={0x0f,0xff},bad_duplex[]={2};
    f=cdp_minimal(); cdp_tlv(f,0x0a,bad_vlan,2); cdp_seal(f); EXPECT(!switch_info_parse_cdp(f.bytes,f.size,&n));
    f=cdp_minimal(); cdp_tlv(f,0x0b,bad_duplex,1); cdp_seal(f); EXPECT(!switch_info_parse_cdp(f.bytes,f.size,&n));
    f=cdp_minimal(); cdp_tlv(f,4,caps,3); cdp_seal(f); EXPECT(!switch_info_parse_cdp(f.bytes,f.size,&n));
    f=cdp_minimal(); cdp_tlv(f,1,low,1); cdp_seal(f); EXPECT(!switch_info_parse_cdp(f.bytes,f.size,&n));
    f=cdp_minimal(); f.bytes[28]=0; f.bytes[29]=3; cdp_seal(f); EXPECT(!switch_info_parse_cdp(f.bytes,f.size,&n));
    f=cdp_minimal(); f.bytes[28]=0; f.bytes[29]=255; cdp_seal(f); EXPECT(!switch_info_parse_cdp(f.bytes,f.size,&n));
    f=cdp_minimal(); f.bytes[f.size++]=1; cdp_seal(f); EXPECT(!switch_info_parse_cdp(f.bytes,f.size,&n));
    f=cdp_start(); cdp_seal(f); EXPECT(!switch_info_parse_cdp(f.bytes,f.size,&n));
    f=cdp_minimal(); for(unsigned i=0;i<62;++i) cdp_tlv(f,0x123,low,0); cdp_seal(f);
    EXPECT(switch_info_parse_cdp(f.bytes,f.size,&n));
    cdp_tlv(f,0x123,low,0); cdp_seal(f); EXPECT(!switch_info_parse_cdp(f.bytes,f.size,&n));
    uint8_t long_id[257]={}; for(auto &byte:long_id) byte='A';
    f=cdp_start(); cdp_tlv(f,1,long_id,256); cdp_tlv(f,3,long_id,256); cdp_tlv(f,6,long_id,257); cdp_seal(f);
    EXPECT(switch_info_parse_cdp(f.bytes,f.size,&n));
    EXPECT(n.device_id[95]==0 && n.platform[127]==0 && n.chassis_raw_len==256);
    f=cdp_start(); cdp_tlv(f,1,long_id,257); cdp_tlv(f,3,low,1); cdp_seal(f);
    EXPECT(!switch_info_parse_cdp(f.bytes,f.size,&n));
    /* Recomputed checksums exercise structural parser branches, not just early
     * checksum rejection. Every frame is bounded to its exact object size. */
    const CdpFrame seed=cdp_minimal();
    for(size_t offset=0;offset<seed.size;++offset) for(unsigned value=0;value<256;value+=16) {
        CdpFrame mutation=seed; mutation.bytes[offset]=value;
        if (offset>=26) cdp_seal(mutation);
        uint8_t exact[36]={}; test_copy(exact,mutation.bytes,36);
        switch_info_parse_cdp(exact,sizeof(exact),&n);
    }
    return 0;
}
static_assert(cdp_parser_suite()==0,"CDP fixture failed (value is fixture line)");

constexpr switch_neighbor_t observation(switch_protocol_t protocol,int64_t receive,unsigned ttl,uint32_t gen) {
    switch_neighbor_t n={}; n.protocol=protocol; n.ttl=ttl; n.valid=ttl!=0;
    n.receive_timestamp_us=receive; n.expiration_us=receive+(int64_t)ttl*1000000; n.link_generation=gen;
    n.chassis_raw_len=n.port_raw_len=1; n.chassis_raw[0]='D'; n.port_raw[0]='P'; return n;
}
constexpr int selection_suite() {
    switch_info_observations_t state={};
    auto lldp=observation(SWITCH_PROTOCOL_LLDP,100,1,7);
    auto cdp=observation(SWITCH_PROTOCOL_CDP,100,2,7);
    EXPECT(switch_info_selected(&state)==nullptr);
    EXPECT(switch_info_observe(&state,&cdp,200)); EXPECT(switch_info_selected(&state)->protocol==SWITCH_PROTOCOL_CDP);
    EXPECT(switch_info_observe(&state,&lldp,200)); EXPECT(switch_info_selected(&state)->protocol==SWITCH_PROTOCOL_LLDP);
    EXPECT(switch_info_observe(&state,&cdp,300)); EXPECT(switch_info_selected(&state)->protocol==SWITCH_PROTOCOL_LLDP);
    EXPECT(switch_info_expire(&state,true,7,1000100)==1);
    EXPECT(switch_info_selected(&state)->protocol==SWITCH_PROTOCOL_CDP);
    EXPECT(switch_info_expire(&state,true,7,2000100)==1 && switch_info_selected(&state)==nullptr);
    lldp=observation(SWITCH_PROTOCOL_LLDP,100,2,7); cdp=observation(SWITCH_PROTOCOL_CDP,100,1,7);
    EXPECT(switch_info_observe(&state,&lldp,200) && switch_info_observe(&state,&cdp,200));
    EXPECT(switch_info_expire(&state,true,7,1000100)==1 && switch_info_selected(&state)->protocol==SWITCH_PROTOCOL_LLDP);
    EXPECT(switch_info_expire(&state,false,7,1000101)==0 && switch_info_selected(&state)==nullptr);
    EXPECT(switch_info_observe(&state,&lldp,200));
    EXPECT(switch_info_expire(&state,true,8,300)==0 && switch_info_selected(&state)==nullptr);
    EXPECT(!switch_info_observe(&state,&lldp,lldp.expiration_us)); /* Queue delay never extends TTL. */
    EXPECT(switch_info_observe(&state,&cdp,200));
    auto withdrawal=observation(SWITCH_PROTOCOL_CDP,200,0,7);
    withdrawal.port_raw[0]='X'; EXPECT(!switch_info_observe(&state,&withdrawal,200));
    withdrawal.port_raw[0]='P'; EXPECT(switch_info_observe(&state,&withdrawal,200) && !state.cdp.valid);
    EXPECT(switch_info_observe(&state,&cdp,200) && switch_info_observe(&state,&lldp,200));
    withdrawal=observation(SWITCH_PROTOCOL_LLDP,200,0,7);
    EXPECT(switch_info_observe(&state,&withdrawal,200) && switch_info_selected(&state)->protocol==SWITCH_PROTOCOL_CDP);
    for(auto protocol:{SWITCH_PROTOCOL_LLDP,SWITCH_PROTOCOL_CDP}) {
        (void)protocol;
        EXPECT(switch_info_capture_current(true,7,4,true,7,4));
        EXPECT(!switch_info_capture_current(true,7,4,true,8,4));
        EXPECT(!switch_info_capture_current(true,7,4,true,7,5));
        EXPECT(!switch_info_capture_current(false,7,4,true,7,4));
        EXPECT(!switch_info_capture_current(true,7,4,false,7,4));
    }
    return 0;
}
static_assert(selection_suite()==0,"Dual protocol selection/lifecycle fixture failed");
