#include "ota_engine.h"
#include <string.h>

static uint32_t le32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static bool version(const char *s, uint32_t out[3])
{
    for (unsigned i=0; i<3; ++i) {
        unsigned digits=0; uint32_t n=0;
        while (*s>='0' && *s<='9') {
            if (++digits>5) return false;
            n=n*10+(unsigned)(*s++-'0');
        }
        if (!digits || n>65535 || (i<2 ? *s++!='.' : *s!='\0')) return false;
        out[i]=n;
    }
    return true;
}
bool ota_version_newer(const char *a, const char *b)
{
    uint32_t x[3], y[3];
    if (!a || !b || !version(a,x) || !version(b,y)) return false;
    for (unsigned i=0;i<3;++i) if(x[i]!=y[i]) return x[i]>y[i];
    return false;
}
const char *ota_state_name(ota_state_t s)
{
    static const char *names[]={"Idle","Authorized","Receiving","Validating","Ready to reboot","Failed","Cancelled"};
    return (unsigned)s<sizeof(names)/sizeof(*names)?names[s]:"Failed";
}
static void stop(ota_engine_t *e, ota_state_t state, const char *reason)
{
    if(e->writer_open) { e->io.abort(e->io.ctx); e->writer_open=false; }
    e->state=state;e->message=reason;
}
void ota_engine_init(ota_engine_t *e,const ota_io_t *io,const char *v)
{
    memset(e,0,sizeof(*e));e->io=*io;e->message="Select Firmware Update";
    if(v && strlen(v)<sizeof(e->current_version)) strcpy(e->current_version,v);
}
bool ota_engine_authorize(ota_engine_t *e,size_t capacity,uint64_t now)
{
    if(e->state==OTA_RECEIVING || e->state==OTA_VALIDATING || e->state==OTA_READY ||
       capacity<OTA_PREFIX_SIZE) return false;
    ota_io_t io=e->io;char v[32];memcpy(v,e->current_version,sizeof(v));
    ota_engine_init(e,&io,v);
    e->capacity=capacity;e->state=OTA_AUTHORIZED;e->deadline=now+OTA_AUTHORIZE_MS;
    e->message="Update mode active";return true;
}
void ota_engine_tick(ota_engine_t *e,uint64_t now)
{
    if((e->state==OTA_AUTHORIZED || e->state==OTA_RECEIVING) &&
       (now>=e->deadline || (e->state==OTA_RECEIVING && now-e->last_rx>=OTA_STALL_MS)))
        stop(e,OTA_FAILED,"Session or upload timed out");
}
bool ota_engine_session_valid(ota_engine_t *e,uint64_t now)
{
    ota_engine_tick(e,now);
    return e->state==OTA_AUTHORIZED || e->state==OTA_RECEIVING ||
           e->state==OTA_VALIDATING || e->state==OTA_READY;
}
bool ota_engine_begin(ota_engine_t *e,size_t total,uint64_t now)
{
    if(e->state!=OTA_AUTHORIZED || !ota_engine_session_valid(e,now)) return false;
    if(total<OTA_PREFIX_SIZE || total>e->capacity) {
        stop(e,OTA_FAILED,"Image is too small or exceeds inactive slot");return false;
    }
    e->image_size=total;e->state=OTA_RECEIVING;
    e->deadline=now+OTA_UPLOAD_MS;e->last_rx=now;e->message="Receiving application";
    return true;
}
static bool prefix_ok(ota_engine_t *e)
{
    const uint8_t *p=e->prefix;
    // ESP32 image header + first segment + esp_app_desc. Full segment bounds,
    // silicon revision and checksum verification remain ESP-IDF's responsibility.
    return p[0]==0xe9 && p[1]>0 && p[1]<=16 && p[12]==0 && p[13]==0 && p[23]==1 &&
        le32(p+28)>=256 && le32(p+32)==0xabcd5432 &&
        !memcmp(p+80,"Syndetis\0",9) &&
        memchr(p+48,0,32) && ota_version_newer((const char *)p+48,e->current_version);
}
bool ota_engine_feed(ota_engine_t *e,const uint8_t *p,size_t n,uint64_t now)
{
    ota_engine_tick(e,now);
    if(e->state!=OTA_RECEIVING) return false;
    if(!p || !n || n>e->image_size-e->received) {
        stop(e,OTA_FAILED,"Unexpected upload length");return false;
    }
    e->last_rx=now;
    while(n) {
        size_t k;
        if(e->prefix_used<OTA_PREFIX_SIZE) {
            k=OTA_PREFIX_SIZE-e->prefix_used;if(k>n)k=n;
            memcpy(e->prefix+e->prefix_used,p,k);e->prefix_used+=k;
            if(e->prefix_used==OTA_PREFIX_SIZE) {
                if(!prefix_ok(e)) {stop(e,OTA_FAILED,"Wrong product, chip, format or non-newer version");return false;}
                if(!e->io.begin(e->io.ctx,e->image_size)) {stop(e,OTA_FAILED,"Cannot open inactive app slot");return false;}
                e->writer_open=true;
                if(!e->io.write(e->io.ctx,e->prefix,OTA_PREFIX_SIZE)) {stop(e,OTA_FAILED,"Flash write failed");return false;}
                e->written=OTA_PREFIX_SIZE;
            }
        } else {
            if(e->written>=e->image_size) {stop(e,OTA_FAILED,"Unexpected upload length");return false;}
            k=e->image_size-e->written;if(k>n)k=n;
            if(!e->io.write(e->io.ctx,p,k)) {stop(e,OTA_FAILED,"Flash write failed");return false;}
            e->written+=k;
        }
        e->received+=k;p+=k;n-=k;
    }
    return true;
}
bool ota_engine_finish(ota_engine_t *e,uint64_t now)
{
    ota_engine_tick(e,now);
    if(e->state!=OTA_RECEIVING) return false;
    if(e->written!=e->image_size) {
        stop(e,OTA_FAILED,"Incomplete upload");return false;
    }
    e->state=OTA_VALIDATING;e->message="Verifying application image";
    if(!e->io.finish(e->io.ctx)) {stop(e,OTA_FAILED,"ESP image validation failed");return false;}
    e->writer_open=false;
    if(!e->io.activate(e->io.ctx)) {stop(e,OTA_FAILED,"Boot selection failed");return false;}
    e->state=OTA_READY;e->message="Verified; rebooting into trial image";
    return true;
}
void ota_engine_cancel(ota_engine_t *e,const char *reason)
{
    // Boot selection is committed at READY. Do not pretend it can be cancelled.
    if(e->state!=OTA_READY) stop(e,OTA_CANCELLED,reason);
}
