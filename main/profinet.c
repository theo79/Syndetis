#include "profinet.h"
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "lwip/tcpip.h"
#include "network_status.h"
#include "firmware_update.h"
#include "diagnostic.h"
#if CONFIG_ETH_TRANSMIT_MUTEX || CONFIG_LWIP_TCPIP_CORE_LOCKING || CONFIG_ESP_NETIF_L2_TAP
#error "Review PROFINET TCP/IP-task TX serialization before changing these settings"
#endif
typedef struct {
    uint8_t frame[PN_FRAME_SIZE];
    uint32_t length,xid,token,epoch,revision;
    int64_t received_us;
    uint16_t vid;
    uint8_t pcp;
    bool tagged,used;
} pn_slot_t;
typedef struct {
    uint8_t frame[60];
    uint32_t xid,token,epoch,revision;
    int64_t deadline;
    bool pending;
    esp_err_t result;
} pn_tx_job_t;
static struct {
    esp_eth_handle_t driver;
    uint8_t mac[6];
    QueueHandle_t queue;
    TaskHandle_t worker;
    profinet_status_t status;
    profinet_device_t devices[PN_MAX_DEVICES];
    pn_slot_t slots[PN_CAPTURE_SLOTS];
    pn_tx_job_t job;
    uint32_t sequence;
    int64_t next_start,started_us,tx_us,close_us;
    bool active,cancel,collecting,attempted;
} context;
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static void increment(uint32_t *v){if(*v!=UINT32_MAX)++*v;}
static bool ota_active(void){ota_status_t o;firmware_update_status(&o);return o.phase==OTA_RECEIVING||o.phase==OTA_SUCCESS;}
/* Caller holds scanner lock. Neither network nor diagnostic getters acquire it. */
static bool current_locked(const network_status_t *n,bool ota){
    return context.active&&diagnostic_current(context.status.token)&&pn_context_current(context.status.token,context.status.token,
        context.status.physical_epoch,n->physical_epoch,context.status.revision,n->revision,n->link_up,context.cancel,ota);
}
static pn_state_t interrupted(void){
    network_status_t n;network_status_get(&n);bool ota=ota_active();
    portENTER_CRITICAL(&lock);
    bool current=current_locked(&n,ota);
    pn_state_t result=n.physical_epoch!=context.status.physical_epoch||n.revision!=context.status.revision||!n.link_up?PN_NETWORK_CHANGED:PN_CANCELLED;
    portEXIT_CRITICAL(&lock);return current?PN_RUNNING:result;
}
/* The ONLY application raw Ethernet TX call. Executed by tcpip_thread.
 * Permanent job stays quarantined until pending becomes false, even after timeout. */
static void transmit_on_tcpip(void *arg){
    (void)arg;network_status_t n;network_status_get(&n);bool ota=ota_active();int64_t now=esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    bool send=context.job.pending&&current_locked(&n,ota)&&now<context.job.deadline&&
        context.job.token==context.status.token&&context.job.xid==context.status.xid&&
        context.job.epoch==n.physical_epoch&&context.job.revision==n.revision;
    portEXIT_CRITICAL(&lock);
    esp_err_t result=send?esp_eth_transmit(context.driver,context.job.frame,sizeof(context.job.frame)):ESP_ERR_INVALID_STATE;
    now=esp_timer_get_time();
    network_status_get(&n);ota=ota_active();
    portENTER_CRITICAL(&lock);
    context.job.result=result;
    if(send&&result==ESP_OK){
        increment(&context.status.counters.tx_accepted);context.status.packets_sent=1;
        context.tx_us=now;context.close_us=now+PN_WINDOW_US;
        context.collecting=current_locked(&n,ota);
    }else increment(&context.status.counters.tx_failed);
    context.job.pending=false; /* Last access; worker may now retire/reuse. */
    portEXIT_CRITICAL(&lock);
}
bool profinet_capture(uint8_t *buffer,uint32_t length){
    pn_envelope_t e={0};if(!pn_envelope(buffer,length,context.mac,&e))return false;
    network_status_t n;network_status_get(&n);bool ota=ota_active();int64_t now=esp_timer_get_time();unsigned index=PN_CAPTURE_SLOTS;
    portENTER_CRITICAL(&lock);
    if(!context.collecting||!current_locked(&n,ota)||now<context.tx_us||now>=context.close_us){portEXIT_CRITICAL(&lock);return false;}
    if(e.xid!=context.status.xid){increment(&context.status.counters.wrong_xid);portEXIT_CRITICAL(&lock);return false;}
    increment(&context.status.counters.candidates);
    if(context.status.counters.candidates>PN_CANDIDATE_LIMIT){increment(&context.status.counters.candidate_limit);context.status.partial=true;}
    else if(length>PN_FRAME_SIZE){increment(&context.status.counters.oversize);context.status.partial=true;}
    else {
        for(unsigned i=0;i<PN_CAPTURE_SLOTS;++i)if(!context.slots[i].used){index=i;context.slots[i].used=true;break;}
        if(index==PN_CAPTURE_SLOTS){increment(&context.status.counters.pool_full);context.status.partial=true;}
        else{
            pn_slot_t *s=&context.slots[index];s->length=length;s->xid=e.xid;s->token=context.status.token;
            s->epoch=n.physical_epoch;s->revision=n.revision;s->received_us=now;s->vid=e.vid;s->pcp=e.pcp;s->tagged=e.tagged;
        }
    }
    portEXIT_CRITICAL(&lock);
    if(index<PN_CAPTURE_SLOTS){
        memcpy(context.slots[index].frame,buffer,length);uint8_t queued_index=(uint8_t)index;
        bool queued=xQueueSend(context.queue,&queued_index,0)==pdTRUE;
        portENTER_CRITICAL(&lock);
        if(queued)increment(&context.status.counters.accepted);
        else{context.slots[index].used=false;increment(&context.status.counters.queue_full);context.status.partial=true;}
        portEXIT_CRITICAL(&lock);
    }
    free(buffer);return true;
}
static void process_slot(uint8_t index){
    static profinet_device_t parsed;pn_slot_t *s=&context.slots[index];
    network_status_t n;network_status_get(&n);bool ota=ota_active();
    portENTER_CRITICAL(&lock);
    bool current=current_locked(&n,ota)&&s->token==context.status.token&&s->xid==context.status.xid&&
        s->epoch==n.physical_epoch&&s->revision==n.revision&&s->received_us>=context.tx_us&&s->received_us<context.close_us;
    portEXIT_CRITICAL(&lock);
    uint32_t dup=0;bool ok=current&&pn_parse(s->frame,s->length,context.mac,s->xid,&parsed,&dup);
    network_status_get(&n);ota=ota_active();bool updated=false;
    portENTER_CRITICAL(&lock);
    if(!current||!current_locked(&n,ota))increment(&context.status.counters.stale);
    else if(!ok)increment(&context.status.counters.malformed);
    else{
        increment(&context.status.counters.parsed);
        while(dup--)increment(&context.status.counters.duplicate_blocks);
        parsed.last_seen_ms=(uint32_t)(s->received_us/1000);
        updated=pn_observe(context.devices,&context.status.count,&parsed);
        if(!updated){increment(&context.status.counters.device_limit);context.status.partial=true;}
    }
    s->used=false;portEXIT_CRITICAL(&lock);
    /* No RX logging; at most 32 first records, not per duplicate response. */
    if(updated){
        static int64_t next_log;
        int64_t now=esp_timer_get_time();
        if(now>=next_log){next_log=now+500000;ESP_LOGI("PROFINET","response mac=%02X:%02X:%02X:%02X:%02X:%02X station=%.63s",parsed.mac[0],parsed.mac[1],parsed.mac[2],parsed.mac[3],parsed.mac[4],parsed.mac[5],parsed.flags&PN_NAME?parsed.station_name:"(absent)");}
    }
}
static void finish(pn_state_t final){
    uint32_t token;
    /* Worker alone mutates records; sort while unpublished, outside RX lock. */
    if(final==PN_COMPLETE)pn_sort(context.devices,context.status.count);
    portENTER_CRITICAL(&lock);
    context.collecting=false;context.active=false;context.cancel=true;token=context.status.token;
    if(final!=PN_COMPLETE)context.status.count=0;
    context.status.duration_ms=(uint32_t)((esp_timer_get_time()-context.started_us)/1000);
    context.status.tx_error=context.job.result;
    portEXIT_CRITICAL(&lock);
    /* Retain running state until coordinator release: UI Back waits. */
    diagnostic_release(token);
    portENTER_CRITICAL(&lock);context.status.state=final;portEXIT_CRITICAL(&lock);
    ESP_LOGI("PROFINET","%s devices=%u partial=%u",profinet_state_name(final),context.status.count,context.status.partial);
    ESP_LOGI("PROFINET","drops pool=%" PRIu32 " queue=%" PRIu32 " candidate=%" PRIu32 " device=%" PRIu32 " heap=%" PRIu32 " min=%" PRIu32 " stack=%u",context.status.counters.pool_full,context.status.counters.queue_full,context.status.counters.candidate_limit,context.status.counters.device_limit,(uint32_t)esp_get_free_heap_size(),(uint32_t)esp_get_minimum_free_heap_size(),(unsigned)uxTaskGetStackHighWaterMark(NULL));
}
static void scan(void){
    int64_t deadline;
    portENTER_CRITICAL(&lock);deadline=context.job.deadline;portEXIT_CRITICAL(&lock);
    ESP_LOGI("PROFINET","Identify started xid=%08" PRIx32,context.status.xid);
    if(tcpip_try_callback(transmit_on_tcpip,NULL)!=ERR_OK){
        portENTER_CRITICAL(&lock);context.job.pending=false;context.job.result=ESP_ERR_NO_MEM;increment(&context.status.counters.tx_failed);portEXIT_CRITICAL(&lock);
        finish(PN_TX_ERROR);return;
    }
    portENTER_CRITICAL(&lock);increment(&context.status.counters.tx_posted);portEXIT_CRITICAL(&lock);
    bool pending=true;
    while(pending&&esp_timer_get_time()<deadline&&interrupted()==PN_RUNNING){
        vTaskDelay(pdMS_TO_TICKS(10));portENTER_CRITICAL(&lock);pending=context.job.pending;portEXIT_CRITICAL(&lock);
    }
    pn_state_t state=interrupted();
    portENTER_CRITICAL(&lock);pending=context.job.pending;esp_err_t result=context.job.result;
    if(pending){context.cancel=true;if(state==PN_RUNNING)increment(&context.status.counters.tx_callback_timeout);}
    portEXIT_CRITICAL(&lock);
    if(state!=PN_RUNNING){finish(state);return;}
    if(pending||result!=ESP_OK){finish(PN_TX_ERROR);return;}
    ESP_LOGI("PROFINET","TX accepted");
    for(;;){
        state=interrupted();if(state!=PN_RUNNING){finish(state);return;}
        int64_t now=esp_timer_get_time();portENTER_CRITICAL(&lock);int64_t close=context.close_us;
        if(now>=close){context.collecting=false;}
        portEXIT_CRITICAL(&lock);
        if(now>=close+PN_DRAIN_US)break;
        uint8_t index;
        if(xQueueReceive(context.queue,&index,pdMS_TO_TICKS(20))==pdTRUE){process_slot(index);vTaskDelay(1);}
        else if(now>=close)break;
    }
    state=interrupted();finish(state==PN_RUNNING?PN_COMPLETE:state);
}
static void worker(void *arg){
    (void)arg;
    for(;;){
        if(ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(20)))scan();
        /* Drain late producer slots outside a scan. Never reset live slot storage. */
        uint8_t index;while(xQueueReceive(context.queue,&index,0)==pdTRUE){portENTER_CRITICAL(&lock);context.slots[index].used=false;portEXIT_CRITICAL(&lock);}
        portENTER_CRITICAL(&lock);context.status.worker_stack_free_bytes=(uint32_t)uxTaskGetStackHighWaterMark(NULL);portEXIT_CRITICAL(&lock);
    }
}
esp_err_t profinet_init(esp_eth_handle_t driver){
    if(context.attempted||!driver)return ESP_ERR_INVALID_STATE;
    context.attempted=true;context.driver=driver;
    esp_err_t err=esp_eth_ioctl(driver,ETH_CMD_G_MAC_ADDR,context.mac);
    if(err!=ESP_OK)return err;
    if(!pn_mac_valid(context.mac))return ESP_ERR_INVALID_ARG;
    context.sequence=esp_random();context.queue=xQueueCreate(PN_CAPTURE_SLOTS,sizeof(uint8_t));
    if(!context.queue)return ESP_ERR_NO_MEM;
    if(xTaskCreate(worker,"profinet",4096,NULL,1,&context.worker)!=pdPASS){vQueueDelete(context.queue);context.queue=NULL;return ESP_ERR_NO_MEM;}
    context.status.available=true;return ESP_OK;
}
pn_start_t profinet_start(void){
    network_status_t n;network_status_get(&n);bool ota=ota_active();int64_t now=esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    if(!context.status.available){portEXIT_CRITICAL(&lock);return PN_START_UNAVAILABLE;}
    if(!n.link_up||!pn_mac_valid(context.mac)){portEXIT_CRITICAL(&lock);return PN_START_LINK_DOWN;}
    if(context.active||context.job.pending||ota){portEXIT_CRITICAL(&lock);return PN_START_BUSY;}
    if(now<context.next_start){portEXIT_CRITICAL(&lock);return PN_START_COOLDOWN;}
    uint32_t token=diagnostic_acquire();if(!token){portEXIT_CRITICAL(&lock);return PN_START_BUSY;}
    if(context.sequence==UINT32_MAX){context.sequence=esp_random();}
    if(++context.sequence==0){++context.sequence;}
    uint32_t attempts=context.status.counters.scan_attempts;
    memset(&context.status,0,sizeof(context.status));context.status.available=true;context.status.state=PN_RUNNING;
    context.status.counters.scan_attempts=attempts;increment(&context.status.counters.scan_attempts);
    context.status.xid=context.sequence;context.status.token=token;context.status.physical_epoch=n.physical_epoch;context.status.revision=n.revision;
    context.started_us=now;context.next_start=now+PN_COOLDOWN_US;context.active=true;context.cancel=false;context.collecting=false;
    context.job=(pn_tx_job_t){.xid=context.sequence,.token=token,.epoch=n.physical_epoch,.revision=n.revision,.deadline=now+PN_TX_WAIT_US,.pending=true,.result=ESP_ERR_INVALID_STATE};
    build_identify_all(context.mac,context.sequence,context.job.frame);
    portEXIT_CRITICAL(&lock);xTaskNotifyGive(context.worker);return PN_START_OK;
}
void profinet_cancel(void){portENTER_CRITICAL(&lock);context.cancel=true;context.collecting=false;portEXIT_CRITICAL(&lock);}
static void get_locked(profinet_status_t *out,const network_status_t *n){
    *out=context.status;
    if(out->state==PN_COMPLETE&&(!n->link_up||out->physical_epoch!=n->physical_epoch||out->revision!=n->revision)){out->state=PN_NETWORK_CHANGED;out->count=0;}
}
void profinet_snapshot(profinet_status_t *out,profinet_device_t *devices){
    network_status_t n;network_status_get(&n);portENTER_CRITICAL(&lock);get_locked(out,&n);
    if(devices){if(out->state!=PN_COMPLETE)out->count=0;for(unsigned i=0;i<out->count;++i)devices[i]=context.devices[i];}
    portEXIT_CRITICAL(&lock);out->free_heap=esp_get_free_heap_size();out->minimum_free_heap=esp_get_minimum_free_heap_size();
}
void profinet_get(profinet_status_t *out){profinet_snapshot(out,NULL);}
bool profinet_device(unsigned index,profinet_device_t *out){
    network_status_t n;network_status_get(&n);portENTER_CRITICAL(&lock);
    bool ok=context.status.state==PN_COMPLETE&&n.link_up&&n.physical_epoch==context.status.physical_epoch&&n.revision==context.status.revision&&index<context.status.count;
    if(ok){*out=context.devices[index];}
    portEXIT_CRITICAL(&lock);return ok;
}
const char *profinet_state_name(pn_state_t state){switch(state){case PN_IDLE:return "idle";case PN_RUNNING:return "running";case PN_COMPLETE:return "complete";case PN_CANCELLED:return "cancelled";case PN_NETWORK_CHANGED:return "network_changed";case PN_TX_ERROR:return "tx_error";}return "error";}
