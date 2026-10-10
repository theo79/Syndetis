#include "discovery_logic.h"
#define EXPECT(x) do { if (!(x)) return __LINE__; } while (0)
constexpr int discovery_tests(void)
{
    discovery_subnet_t s={};
    const unsigned prefixes[]={24,25,26,30,16};
    const uint32_t counts[]={254,126,62,2,65534};
    for (unsigned i=0;i<5;++i) {
        uint32_t mask=UINT32_MAX << (32-prefixes[i]);
        EXPECT(discovery_subnet(0xc0a80101,mask,&s));
        EXPECT(s.network==(prefixes[i]==16 ? 0xc0a80000U : 0xc0a80100U));
        EXPECT(s.broadcast==(0xc0a80101 | ~mask));
        EXPECT(s.first==s.network+1 && s.last==s.broadcast-1);
        EXPECT(s.prefix==prefixes[i] && s.usable==counts[i]);
        EXPECT(s.candidates==counts[i]-1);
        EXPECT(discovery_allowed(&s)==(prefixes[i]>=24));
        EXPECT(!discovery_candidate(&s,s.network));
        EXPECT(!discovery_candidate(&s,s.broadcast));
        EXPECT(!discovery_candidate(&s,s.own));
        EXPECT(discovery_candidate(&s,s.last));
        EXPECT(!discovery_candidate(&s,s.network-1));
    }
    EXPECT(!discovery_subnet(0xc0a80101,0xffff00ff,&s));
    EXPECT(!discovery_subnet(0xc0a80101,0,&s));
    EXPECT(!discovery_subnet(0,0xffffff00,&s));
    EXPECT(!discovery_subnet(0x7f000001,0xffffff00,&s));
    EXPECT(!discovery_subnet(0xe0000001,0xffffff00,&s));
    EXPECT(!discovery_subnet(0xc0a80100,0xffffff00,&s));
    EXPECT(!discovery_subnet(0xc0a801ff,0xffffff00,&s));
    EXPECT(discovery_subnet(0xc0a80101,0xfffffffe,&s));
    EXPECT(s.prefix==31 && s.candidates==0 && !discovery_candidate(&s,s.own));
    EXPECT(discovery_subnet(0xc0a80101,0xffffffff,&s));
    EXPECT(s.prefix==32 && s.candidates==0);
    EXPECT(discovery_present(true,false));
    EXPECT(discovery_present(false,true));
    EXPECT(discovery_present(true,true));
    EXPECT(!discovery_present(false,false));
    EXPECT(discovery_ip_compare(0xc0a80102,0xc0a8010a)<0);
    EXPECT(discovery_ip_compare(0xc0a801ff,0xc0a80200)<0);
    EXPECT(discovery_ip_compare(0xc0a8010a,0xc0a80102)>0);
    EXPECT(discovery_ip_compare(1,1)==0);
    /* Exercise actual numeric sorting through the same comparator. */
    uint32_t ips[]={0xc0a80115,0xc0a80102,0xc0a8010a};
    for(unsigned i=1;i<3;++i) for(unsigned j=i;j && discovery_ip_compare(ips[j-1],ips[j])>0;--j) {
        uint32_t temp=ips[j];ips[j]=ips[j-1];ips[j-1]=temp;
    }
    EXPECT(ips[0]==0xc0a80102 && ips[1]==0xc0a8010a && ips[2]==0xc0a80115);
    return 0;
}

static_assert(discovery_tests()==0, "Discovery logic assertion failed");
