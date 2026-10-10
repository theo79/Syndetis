#include "oui_vendor.h"
/* Verified primary-source evidence and exact prefixes: tests/oui_sources.json.
 * Deliberately incomplete MA-L table. OUI ownership does not establish role. */
static const struct oui_entry { uint32_t prefix; char name[OUI_VENDOR_LEN]; } entries[] = {
    {0x00000C, "Cisco"},
    {0x000142, "Cisco"},
    {0x000143, "Cisco"},
    {0x001B1B, "Siemens"},
};
bool oui_lookup_vendor(const uint8_t mac[6], char *out, size_t out_len)
{
    if (!out || !out_len) return false;
    out[0]=0;
    if (!mac || (mac[0] & 3)) return false;
    bool nonzero=false;
    for (unsigned i=0;i<6;++i) nonzero=nonzero || mac[i]!=0;
    if (!nonzero) return false;
    uint32_t prefix=((uint32_t)mac[0]<<16)|((uint32_t)mac[1]<<8)|mac[2];
    size_t low=0, high=sizeof(entries)/sizeof(entries[0]);
    while (low<high) {
        size_t middle=low+(high-low)/2;
        if (prefix<entries[middle].prefix) high=middle;
        else if (prefix>entries[middle].prefix) low=middle+1;
        else {
            size_t i=0;
            while (i+1<out_len && entries[middle].name[i]) { out[i]=entries[middle].name[i]; ++i; }
            out[i]=0;
            return true;
        }
    }
    return false;
}
