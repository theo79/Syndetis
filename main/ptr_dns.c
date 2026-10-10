#include "ptr_dns.h"
#include "discovery_name.h"
static uint16_t read16(const uint8_t *p) { return ((uint16_t)p[0]<<8)|p[1]; }
static void write16(uint8_t *p, uint16_t n) { p[0]=n>>8; p[1]=n; }
static size_t reverse_name(char out[40], uint32_t ip)
{
    size_t used=0;
    for (unsigned i=0;i<4;++i) {
        unsigned n=(ip>>(i*8))&255;
        if (n>=100) out[used++]='0'+n/100;
        if (n>=10) out[used++]='0'+(n/10)%10;
        out[used++]='0'+n%10; out[used++]='.';
    }
    const char suffix[]="in-addr.arpa";
    for (unsigned i=0;suffix[i];++i) out[used++]=suffix[i];
    out[used]=0;
    return used;
}
size_t ptr_dns_query(uint8_t *out, size_t capacity, uint16_t id, uint32_t ip)
{
    if (!out || capacity<64) return 0;
    for (unsigned i=0;i<12;++i) out[i]=0;
    write16(out,id); out[2]=1; out[5]=1; /* RD, one question */
    char name[40]; reverse_name(name,ip);
    size_t used=12, start=0;
    while (name[start]) {
        size_t end=start; while (name[end] && name[end]!='.') ++end;
        out[used++]=end-start;
        for (size_t i=start;i<end;++i) out[used++]=name[i];
        start=name[end] ? end+1:end;
    }
    out[used++]=0; write16(out+used,12); used+=2;
    write16(out+used,1); return used+2;
}
/* Follow only backward compression pointers, bound traversal/output, and report
 * bytes consumed at the original location separately from expanded length. */
static bool decode_name(const uint8_t *p, size_t n, size_t *cursor, char out[256])
{
    size_t pos=*cursor, used=0, next=0;
    bool jumped=false;
    for (unsigned step=0;step<128 && pos<n;++step) {
        uint8_t label=p[pos++];
        if ((label&0xc0)==0xc0) {
            if (pos>=n) return false;
            size_t offset=((size_t)(label&63)<<8)|p[pos++];
            if (offset>=pos-2 || offset<12) return false;
            if (!jumped) next=pos;
            jumped=true; pos=offset; continue;
        }
        if (label&0xc0) return false;
        if (!label) { out[used]=0; *cursor=jumped ? next:pos; return true; }
        if (pos+label>n || used+label+(used ? 1:0)>253) return false;
        if (used) out[used++]='.';
        for (unsigned i=0;i<label;++i) {
            uint8_t c=p[pos++];
            /* Dot is a separator, not legal as a byte inside a host label. */
            if (c<=32 || c>=127 || c=='.') return false;
            out[used++]=(char)c;
        }
    }
    return false;
}
static bool equal_name(const char *a,const char *b)
{
    for (unsigned i=0;i<256;++i) {
        char x=a[i],y=b[i];
        if (x>='A' && x<='Z') x+=32;
        if (y>='A' && y<='Z') y+=32;
        if (x!=y) return false;
        if (!x) return true;
    }
    return false;
}
ptr_dns_result_t ptr_dns_response(const uint8_t *p, size_t n,
                                uint16_t id, uint32_t ip, char hostname[64])
{
    if (!hostname) return PTR_IGNORE;
    hostname[0]=0;
    if (!p || n<12 || n>PTR_DNS_PACKET_SIZE || read16(p)!=id) return PTR_IGNORE;
    uint16_t flags=read16(p+2);
    if (!(flags&0x8000) || (flags&0x7800) || read16(p+4)!=1) return PTR_IGNORE;
    char expected[40], name[256]; reverse_name(expected,ip);
    size_t cursor=12;
    if (!decode_name(p,n,&cursor,name) || !equal_name(name,expected) || cursor+4>n ||
        read16(p+cursor)!=12 || read16(p+cursor+2)!=1) return PTR_IGNORE;
    cursor+=4;
    if ((flags&0x0200) || (flags&15)) return PTR_UNRESOLVED; /* no TCP fallback */
    unsigned answers=read16(p+6);
    if (answers>32) return PTR_UNRESOLVED;
    for (unsigned i=0;i<answers;++i) {
        if (!decode_name(p,n,&cursor,name) || cursor+10>n) return PTR_UNRESOLVED;
        uint16_t type=read16(p+cursor),cls=read16(p+cursor+2),len=read16(p+cursor+8);
        cursor+=10;
        if (len>n-cursor) return PTR_UNRESOLVED;
        if (type==12 && cls==1 && equal_name(name,expected)) {
            size_t end=cursor;
            if (!decode_name(p,n,&end,name) || end!=cursor+len ||
                !discovery_name_valid(name,hostname)) return PTR_UNRESOLVED;
            return PTR_FOUND;
        }
        cursor+=len;
    }
    return PTR_UNRESOLVED;
}
