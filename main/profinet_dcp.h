#pragma once
/* Internal portable Identify-only implementation. No command/service parameters. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
#define PN_INLINE static inline constexpr
#else
#define PN_INLINE static inline
#endif
#define PN_MAX_DEVICES 32
#define PN_FRAME_SIZE 1600
#define PN_CAPTURE_SLOTS 8
#define PN_CANDIDATE_LIMIT 128
#define PN_WINDOW_US 3000000
#define PN_DRAIN_US 250000
#define PN_TX_WAIT_US 1000000
#define PN_COOLDOWN_US 10000000
enum {
    PN_VALID=1u, PN_IP=2u, PN_NAME=4u, PN_IDS=8u, PN_ROLE=16u,
    PN_VENDOR=32u, PN_ALIAS=64u, PN_INSTANCE=128u, PN_OPTIONS=256u,
    PN_NAME_TRUNC=512u, PN_VENDOR_TRUNC=1024u, PN_ALIAS_TRUNC=2048u,
    PN_NAME_SAN=4096u, PN_VENDOR_SAN=8192u, PN_ALIAS_SAN=16384u,
    PN_MAC_MISMATCH=32768u, PN_IP_CONFLICT=65536u
};
typedef struct {
    uint32_t flags, last_seen_ms;
    uint8_t ip[4], subnet[4], gateway[4];
    uint16_t vendor_id, device_id, device_instance, ip_block_info;
    uint8_t mac[6], device_role, reserved;
    char station_name[241], vendor_value[64], alias_name[64];
} profinet_device_t;
#ifdef __cplusplus
static_assert(sizeof(profinet_device_t)==408);
#else
_Static_assert(sizeof(profinet_device_t)==408, "DCP record budget changed");
#endif
typedef struct { size_t data_offset, data_length; uint32_t xid; uint16_t vid; uint8_t pcp; bool tagged; } pn_envelope_t;
PN_INLINE uint16_t pn_be16(const uint8_t *p) { return (uint16_t)((p[0]<<8)|p[1]); }
PN_INLINE uint32_t pn_be32(const uint8_t *p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
PN_INLINE void pn_copy(uint8_t *a,const uint8_t *b,size_t n) { for(size_t i=0;i<n;++i)a[i]=b[i]; }
PN_INLINE int pn_compare(const uint8_t *a,const uint8_t *b,size_t n) { for(size_t i=0;i<n;++i)if(a[i]!=b[i])return a[i]<b[i]?-1:1; return 0; }
PN_INLINE bool pn_mac_valid(const uint8_t *m) { if(!m||(m[0]&1))return false; unsigned v=0;for(unsigned i=0;i<6;++i)v|=m[i];return v!=0; }
/* Private fixed builder: only source MAC and transaction vary. No Set/Get path. */
PN_INLINE bool build_identify_all(const uint8_t *mac,uint32_t xid,uint8_t *f) {
    if(!f||!pn_mac_valid(mac)||!xid)return false;
    for(unsigned i=0;i<60;++i)f[i]=0;
    f[0]=1;f[1]=0x0e;f[2]=0xcf;pn_copy(f+6,mac,6);
    f[12]=0x88;f[13]=0x92;f[14]=0xfe;f[15]=0xfe;f[16]=5;
    f[18]=(uint8_t)(xid>>24);f[19]=(uint8_t)(xid>>16);f[20]=(uint8_t)(xid>>8);f[21]=(uint8_t)xid;
    f[23]=0x80;f[25]=4;f[26]=0xff;f[27]=0xff;return true;
}
/* Fixed signature and complete data envelope only; session matching is separate. */
PN_INLINE bool pn_envelope(const uint8_t *f,size_t n,const uint8_t *mac,pn_envelope_t *out) {
    if(!f||!mac||!out||n<14||pn_compare(f,mac,6)||!pn_mac_valid(f+6))return false;
    size_t o=14;uint16_t type=pn_be16(f+12),vid=0;uint8_t pcp=0;bool tagged=false;
    if(type==0x8100){if(n<18)return false;uint16_t t=pn_be16(f+14);vid=t&4095;pcp=(uint8_t)(t>>13);type=pn_be16(f+16);o=18;tagged=true;}
    if(type!=0x8892||n-o<12||pn_be16(f+o)!=0xfeff||f[o+2]!=5||f[o+3]!=1)return false;
    size_t len=pn_be16(f+o+10);if(len>n-o-12)return false;
    *out=(pn_envelope_t){o+12,len,pn_be32(f+o+4),vid,pcp,tagged};return true;
}
PN_INLINE uint32_t pn_text(char *out,size_t cap,const uint8_t *p,size_t n,uint32_t trunc,uint32_t san) {
    uint32_t flags=n>=cap?trunc:0;size_t count=n<cap-1?n:cap-1;
    for(size_t i=0;i<count;++i){bool visible=p[i]>=32&&p[i]<=126;out[i]=visible?(char)p[i]:'?';if(!visible)flags|=san;}
    for(size_t i=count;i<n;++i)if(p[i]<32||p[i]>126)flags|=san;
    out[count]=0;return flags;
}
/* Output is scratch only. On false it is never eligible for publication. */
PN_INLINE bool pn_parse(const uint8_t *f,size_t n,const uint8_t *mac,uint32_t xid,profinet_device_t *out,uint32_t *duplicates) {
    pn_envelope_t e={0};if(!out||n>PN_FRAME_SIZE||!pn_envelope(f,n,mac,&e)||e.xid!=xid)return false;
    *out=(profinet_device_t){0};pn_copy(out->mac,f+6,6);uint32_t seen=0,dup=0;
    size_t pos=e.data_offset,end=pos+e.data_length;
    while(pos<end){
        if(end-pos<4)return false;
        unsigned option=f[pos],sub=f[pos+1];size_t len=pn_be16(f+pos+2);pos+=4;
        if(len>end-pos)return false;
        const uint8_t *p=f+pos;pos+=len;
        if(len&1){if(pos==end||f[pos++]!=0)return false;}
        unsigned bit=0;
        if(option==1&&(sub==1||sub==2))bit=sub==1?1:2;
        if(option==2&&sub>=1&&sub<=7)bit=sub+2;
        if(!bit)continue;
        if(len<2)return false;
        uint16_t info=pn_be16(p);p+=2;size_t value=len-2;
        if((option==1&&sub==1&&len!=8)||(option==1&&sub==2&&len!=14)||
           (option==2&&sub==3&&len!=6)||(option==2&&(sub==4||sub==7)&&len!=4)||
           (option==2&&sub==5&&(value&1)))return false;
        if(seen&(1u<<bit)){++dup;continue;}seen|=1u<<bit;
        if(option==1&&sub==1){if(pn_compare(p,out->mac,6))out->flags|=PN_MAC_MISMATCH;}
        else if(option==1){out->flags|=PN_IP;out->ip_block_info=info;pn_copy(out->ip,p,4);pn_copy(out->subnet,p+4,4);pn_copy(out->gateway,p+8,4);if(info==0x80||info==0x81||info==0x82)out->flags|=PN_IP_CONFLICT;}
        else switch(sub){
        case 1:out->flags|=PN_VENDOR|pn_text(out->vendor_value,64,p,value,PN_VENDOR_TRUNC,PN_VENDOR_SAN);break;
        case 2:out->flags|=PN_NAME|pn_text(out->station_name,241,p,value,PN_NAME_TRUNC,PN_NAME_SAN);break;
        case 3:out->flags|=PN_IDS;out->vendor_id=pn_be16(p);out->device_id=pn_be16(p+2);break;
        case 4:out->flags|=PN_ROLE;out->device_role=p[0];out->reserved=p[1];break;
        case 5:out->flags|=PN_OPTIONS;break;
        case 6:out->flags|=PN_ALIAS|pn_text(out->alias_name,64,p,value,PN_ALIAS_TRUNC,PN_ALIAS_SAN);break;
        case 7:out->flags|=PN_INSTANCE;out->device_instance=pn_be16(p);break;
        }
    }
    out->flags|=PN_VALID;if(duplicates)*duplicates=dup;return true;
}
PN_INLINE void pn_merge(profinet_device_t *a,const profinet_device_t *b){
    if(b->flags&PN_IP){pn_copy(a->ip,b->ip,4);pn_copy(a->subnet,b->subnet,4);pn_copy(a->gateway,b->gateway,4);a->ip_block_info=b->ip_block_info;a->flags&=~PN_IP_CONFLICT;}
    if(b->flags&PN_IDS){a->vendor_id=b->vendor_id;a->device_id=b->device_id;}
    if(b->flags&PN_ROLE){a->device_role=b->device_role;a->reserved=b->reserved;}
    if(b->flags&PN_INSTANCE)a->device_instance=b->device_instance;
    if(b->flags&PN_NAME){for(unsigned i=0;i<241;++i)a->station_name[i]=b->station_name[i];a->flags&=~(PN_NAME_TRUNC|PN_NAME_SAN);}
    if(b->flags&PN_VENDOR){for(unsigned i=0;i<64;++i)a->vendor_value[i]=b->vendor_value[i];a->flags&=~(PN_VENDOR_TRUNC|PN_VENDOR_SAN);}
    if(b->flags&PN_ALIAS){for(unsigned i=0;i<64;++i)a->alias_name[i]=b->alias_name[i];a->flags&=~(PN_ALIAS_TRUNC|PN_ALIAS_SAN);}
    a->flags|=b->flags;a->last_seen_ms=b->last_seen_ms;
}
PN_INLINE bool pn_observe(profinet_device_t *table,unsigned *count,const profinet_device_t *b){
    for(unsigned i=0;i<*count;++i)if(!pn_compare(table[i].mac,b->mac,6)){pn_merge(table+i,b);return true;}
    if(*count==PN_MAX_DEVICES)return false;
    table[(*count)++]=*b;return true;
}
PN_INLINE void pn_sort(profinet_device_t *table,unsigned count){
    for(unsigned i=1;i<count;++i)for(unsigned j=i;j&&pn_compare(table[j-1].mac,table[j].mac,6)>0;--j){profinet_device_t t=table[j-1];table[j-1]=table[j];table[j]=t;}
}
PN_INLINE bool pn_context_current(uint32_t token,uint32_t active,uint32_t epoch,uint32_t current_epoch,uint32_t revision,uint32_t current_revision,bool link,bool cancel,bool ota){
    return token&&token==active&&epoch==current_epoch&&revision==current_revision&&link&&!cancel&&!ota;
}
