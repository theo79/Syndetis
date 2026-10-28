#include "ota_update.h"
#include "ota_boot_policy.h"
#include "ota_http_policy.h"
#include "dhcp_test.h"
#include "services.h"
#include "status.h"
#include "network_policy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

extern const uint8_t update_html_start[] __asm__("_binary_update_html_start");
extern const uint8_t update_html_end[] __asm__("_binary_update_html_end");
static ota_engine_t engine;
static SemaphoreHandle_t mutex;
static QueueHandle_t uploads;
static httpd_handle_t update_server;
static portMUX_TYPE publication_lock=portMUX_INITIALIZER_UNLOCKED;
static ota_update_status_t published;
static bool open_requested, cancel_requested, upload_pending, own_web, reserved, upload_wifi;
static unsigned generation;
static uint32_t upload_local, upload_peer;
static bool nvs_ok, available, flash_ok;
static esp_ota_handle_t writer;
static bool writer_open;
static const esp_partition_t *target;
static uint64_t boot_started, ready_at;
static bool trial, boot_done;
static unsigned health_frames;
static uint32_t upload_stack_free;
static bool tasks_healthy;
static unsigned notice_id, shown_notice_id;
static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time()/1000; }
static bool cancellation(void)
{
    portENTER_CRITICAL(&publication_lock);bool b=cancel_requested;portEXIT_CRITICAL(&publication_lock);return b;
}
static void publish(void)
{
    ota_update_status_t s={.state=engine.state,.received=engine.received,
        .total=engine.image_size?engine.image_size:0,
        .minimum_heap=esp_get_minimum_free_heap_size(),.worker_stack_free=upload_stack_free};
    snprintf(s.message,sizeof(s.message),"%s",engine.message);
    portENTER_CRITICAL(&publication_lock);published=s;portEXIT_CRITICAL(&publication_lock);
}
ota_update_status_t ota_update_status(void)
{
    portENTER_CRITICAL(&publication_lock);ota_update_status_t s=published;portEXIT_CRITICAL(&publication_lock);return s;
}
static void fail(const char *reason)
{
    ota_engine_cancel(&engine,reason);engine.state=OTA_FAILED;publish();
}
static bool begin_write(void *ctx,size_t length)
{
    (void)ctx;
    const esp_partition_t *running=esp_ota_get_running_partition();
    target=esp_ota_get_next_update_partition(NULL);
    if(!running || !target || target->address==running->address ||
       running->subtype<ESP_PARTITION_SUBTYPE_APP_OTA_0 || running->subtype>ESP_PARTITION_SUBTYPE_APP_OTA_15 ||
       length>target->size || esp_get_free_heap_size()<32768 ||
       heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)<16384) return false;
    if(esp_ota_begin(target,OTA_WITH_SEQUENTIAL_WRITES,&writer)!=ESP_OK)return false;
    writer_open=true;return true;
}
static bool write_chunk(void *ctx,const uint8_t *p,size_t n)
{
    (void)ctx;
    return !cancellation() && wired_update_valid(generation) &&
        esp_ota_write(writer,p,n)==ESP_OK;
}
static bool finish_write(void *ctx)
{
    (void)ctx;
    // esp_ota_end consumes its handle even on error.
    writer_open=false;
    if(esp_ota_end(writer)!=ESP_OK) return false;
    esp_image_metadata_t meta={0};
    esp_partition_pos_t pos={.offset=target->address,.size=target->size};
    return esp_image_verify(ESP_IMAGE_VERIFY,&pos,&meta)==ESP_OK && meta.image_len==engine.image_size;
}
static bool activate(void *ctx)
{
    (void)ctx;
    return !cancellation() && wired_update_valid(generation) && esp_ota_set_boot_partition(target)==ESP_OK;
}
static void abort_write(void *ctx)
{
    (void)ctx;if(writer_open) {esp_ota_abort(writer);writer_open=false;}
}
static bool endpoint(httpd_req_t *req,uint32_t *local,uint32_t *peer,char host[32])
{
    struct sockaddr_in l={0},p={0};socklen_t ln=sizeof(l),pn=sizeof(p);
    int fd=httpd_req_to_sockfd(req);
    if(getsockname(fd,(struct sockaddr*)&l,&ln) || getpeername(fd,(struct sockaddr*)&p,&pn) ||
       l.sin_family!=AF_INET || p.sin_family!=AF_INET) return false;
    *local=ntohl(l.sin_addr.s_addr);*peer=ntohl(p.sin_addr.s_addr);
    if(*local==0xc0a80401u) {
        if((*peer&0xffffff00u)!=0xc0a80400u || (*peer&255)==0 || (*peer&255)==255 || *peer==*local) return false;
    } else {
        dhcp_status_t d=dhcp_test_status();struct in_addr ip,mask;
        if(!d.active || d.busy || !inet_aton(d.ip,&ip) || !inet_aton(d.mask,&mask) ||
           *local!=ntohl(ip.s_addr) || (*peer&ntohl(mask.s_addr))!=(*local&ntohl(mask.s_addr)) ||
           *peer==*local || (*peer&~ntohl(mask.s_addr))==0 || (*peer&~ntohl(mask.s_addr))==~ntohl(mask.s_addr)) return false;
    }
    char ip[16];if(!inet_ntop(AF_INET,&l.sin_addr,ip,sizeof(ip)))return false;
    snprintf(host,32,"%s",ip); // Host authority is validated separately against this local IPv4 and port 8080.
    return true;
}
static bool headers(httpd_req_t *r,uint32_t *local,uint32_t *peer,bool mutation)
{
    char expected[32],host[40],origin[64]={0};
    if(!endpoint(r,local,peer,expected) ||
       httpd_req_get_hdr_value_str(r,"Host",host,sizeof(host))!=ESP_OK) return false;
    if(mutation && httpd_req_get_hdr_value_str(r,"Origin",origin,sizeof(origin))!=ESP_OK)return false;
    if(!ota_http_authority(*local,host,origin,mutation))return false;
    return true;
}
static esp_err_t response(httpd_req_t *r,const char *status,const char *text)
{
    httpd_resp_set_status(r,status);httpd_resp_set_type(r,"text/plain; charset=utf-8");
    httpd_resp_set_hdr(r,"Cache-Control","no-store");httpd_resp_set_hdr(r,"X-Content-Type-Options","nosniff");
    httpd_resp_set_hdr(r,"Connection","close");return httpd_resp_send(r,text,HTTPD_RESP_USE_STRLEN);
}
static esp_err_t page_get(httpd_req_t *r)
{
    char host[32];uint32_t l,p;
    if(!endpoint(r,&l,&p,host) ||
       httpd_req_get_hdr_value_str(r,"Host",host,sizeof(host))!=ESP_OK ||
       !ota_http_authority(l,host,NULL,false))return response(r,"403 Forbidden","Update interface only");
    xSemaphoreTake(mutex,portMAX_DELAY);
    ota_engine_tick(&engine,now_ms());bool active=engine.state==OTA_AUTHORIZED || engine.state==OTA_RECEIVING;
    xSemaphoreGive(mutex);
    if(!active)return response(r,"403 Forbidden","Open Firmware Update on the device");
    httpd_resp_set_type(r,"text/html; charset=utf-8");httpd_resp_set_hdr(r,"Cache-Control","no-store");
    httpd_resp_set_hdr(r,"Content-Security-Policy","default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; form-action 'self'; frame-ancestors 'none'");
    httpd_resp_set_hdr(r,"X-Content-Type-Options","nosniff");
    return httpd_resp_send(r,(const char *)update_html_start,update_html_end-update_html_start-1);
}
static esp_err_t status_get_handler(httpd_req_t *r)
{
    uint32_t l,p;
    if(!headers(r,&l,&p,false)) return response(r,"403 Forbidden","Not authorized");
    xSemaphoreTake(mutex,portMAX_DELAY);
    bool ok=ota_engine_session_valid(&engine,now_ms()) &&
        (engine.state==OTA_AUTHORIZED || (p==upload_peer && l==upload_local));
    publish();xSemaphoreGive(mutex);
    if(!ok)return response(r,"403 Forbidden","Session expired or unavailable");
    ota_update_status_t s=ota_update_status();char json[240];
    // Engine messages are compile-time constants, not network supplied text.
    snprintf(json,sizeof(json),"{\"state\":\"%s\",\"received\":%u,\"total\":%u,\"message\":\"%s\"}",
        ota_state_name(s.state),(unsigned)s.received,(unsigned)s.total,s.message);
    httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"Cache-Control","no-store");
    return httpd_resp_send(r,json,HTTPD_RESP_USE_STRLEN);
}
static esp_err_t cancel_post(httpd_req_t *r)
{
    uint32_t l,p;char type[40];
    if(!headers(r,&l,&p,true) || r->content_len!=0 ||
       httpd_req_get_hdr_value_str(r,"Content-Type",type,sizeof(type))!=ESP_OK || strcmp(type,"application/json"))
        return response(r,"403 Forbidden","Not authorized");
    xSemaphoreTake(mutex,portMAX_DELAY);
    bool ok=engine.state!=OTA_READY && ota_engine_session_valid(&engine,now_ms()) &&
        (engine.state==OTA_AUTHORIZED || (p==upload_peer && l==upload_local));
    if(ok)ota_update_cancel();
    publish();xSemaphoreGive(mutex);
    return response(r,ok?"202 Accepted":"403 Forbidden",ok?"Cancelling":"Not authorized");
}
static esp_err_t upload_post(httpd_req_t *r)
{
    uint32_t l,p;char type[48];
    if(!headers(r,&l,&p,true) ||
       httpd_req_get_hdr_value_str(r,"Content-Type",type,sizeof(type))!=ESP_OK || strcmp(type,"application/octet-stream"))
        return response(r,"403 Forbidden","Same-origin application binary required");
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(upload_pending || cancellation() || !wired_update_valid(generation) || !ota_engine_begin(&engine,r->content_len,now_ms())) {
        publish();xSemaphoreGive(mutex);return response(r,"409 Conflict","Closed, busy or invalid length");
    }
    upload_local=l;upload_peer=p;
    portENTER_CRITICAL(&publication_lock);upload_wifi=l==0xc0a80401u;portEXIT_CRITICAL(&publication_lock);
    wired_update_extend();
    httpd_req_t *async=NULL;
    if(httpd_req_async_handler_begin(r,&async)!=ESP_OK) {
        fail("Cannot allocate upload request");xSemaphoreGive(mutex);return response(r,"503 Service Unavailable","Please reopen update session");
    }
    upload_pending=true;
    if(xQueueSend(uploads,&async,0)!=pdTRUE) {
        upload_pending=false;fail("Upload queue unavailable");
        response(async,"503 Service Unavailable","Upload unavailable");httpd_req_async_handler_complete(async);
    }
    publish();xSemaphoreGive(mutex);return ESP_OK;
}
static esp_err_t start_update_server(void)
{
    httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();
    cfg.server_port=OTA_HTTP_PORT;cfg.stack_size=8192;cfg.max_open_sockets=2;cfg.max_uri_handlers=4;
    cfg.ctrl_port=32769;cfg.recv_wait_timeout=2;cfg.send_wait_timeout=2;
    cfg.lru_purge_enable=false;
    esp_err_t err=httpd_start(&update_server,&cfg);
    if(err!=ESP_OK)return err;
    const httpd_uri_t paths[]={
        {.uri="/update",.method=HTTP_GET,.handler=page_get},
        {.uri="/update/status",.method=HTTP_GET,.handler=status_get_handler},
        {.uri="/update/upload",.method=HTTP_POST,.handler=upload_post},
        {.uri="/update/cancel",.method=HTTP_POST,.handler=cancel_post}};
    for(unsigned i=0;i<4;++i)if((err=httpd_register_uri_handler(update_server,&paths[i]))!=ESP_OK)return err;
    return ESP_OK;
}
// Called only by the existing upload task, without the engine mutex held.
// A successful OLED frame must acknowledge this notice before flash work starts.
static bool show_notice(const char *message)
{
    xSemaphoreTake(mutex,portMAX_DELAY);
    engine.message=message;publish();
    portENTER_CRITICAL(&publication_lock);unsigned wanted=++notice_id;portEXIT_CRITICAL(&publication_lock);
    xSemaphoreGive(mutex);
    uint64_t until=now_ms()+2000;
    while(now_ms()<until) {
        portENTER_CRITICAL(&publication_lock);bool shown=shown_notice_id==wanted;portEXIT_CRITICAL(&publication_lock);
        if(cancellation())return false;
        if(shown)return true;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    xSemaphoreTake(mutex,portMAX_DELAY);fail("OLED not ready");xSemaphoreGive(mutex);return false;
}
static void upload_worker(void *arg)
{
    (void)arg;httpd_req_t *req;uint8_t buffer[2048];
    for(;;) {
        if(xQueueReceive(uploads,&req,portMAX_DELAY)!=pdTRUE)continue;
        size_t remaining=req->content_len;bool ok=show_notice("Preparing update...");
        while(remaining && ok) {
            xSemaphoreTake(mutex,portMAX_DELAY);
            ota_engine_tick(&engine,now_ms());
            if(cancellation() || !wired_update_valid(generation))ota_engine_cancel(&engine,"Cancelled or network changed");
            ok=engine.state==OTA_RECEIVING;publish();xSemaphoreGive(mutex);
            if(!ok)break;
            int n=httpd_req_recv(req,(char *)buffer,remaining<sizeof(buffer)?remaining:sizeof(buffer));
            if(n==HTTPD_SOCK_ERR_TIMEOUT)continue; // Core's absolute and stall deadlines still apply.
            xSemaphoreTake(mutex,portMAX_DELAY);
            if(n<=0) {ota_engine_cancel(&engine,"Client disconnected or incomplete upload");ok=false;}
            else {engine.message="Receiving application";ok=ota_engine_feed(&engine,buffer,(size_t)n,now_ms());remaining-=(size_t)n;}
            upload_stack_free=uxTaskGetStackHighWaterMark(NULL);
            publish();xSemaphoreGive(mutex);
            // Give the existing UI task a chance to present a frame between flash chunks.
            if(ok)vTaskDelay(pdMS_TO_TICKS(1));
        }
        if(ok)ok=show_notice("Verifying...");
        xSemaphoreTake(mutex,portMAX_DELAY);
        if(ok)ok=ota_engine_finish(&engine,now_ms());
        publish();xSemaphoreGive(mutex);
        response(req,ok?"200 OK":"400 Bad Request",ota_update_status().message);
        httpd_req_async_handler_complete(req);
        xSemaphoreTake(mutex,portMAX_DELAY);upload_pending=false;if(ok)ready_at=now_ms();xSemaphoreGive(mutex);
    }
}
static void wifi_event(void *arg,esp_event_base_t base,int32_t id,void *data)
{
    (void)arg;(void)base;(void)data;
    // Conservative: any AP disconnect aborts a Wi-Fi upload, never an Ethernet one.
    if(id!=WIFI_EVENT_AP_STADISCONNECTED)return;
    portENTER_CRITICAL(&publication_lock);
    if(upload_wifi && published.state==OTA_RECEIVING)cancel_requested=true;
    portEXIT_CRITICAL(&publication_lock);
}
static void supervise(void *arg)
{
    (void)arg;
    for(;;) {
        portENTER_CRITICAL(&publication_lock);bool open=open_requested;open_requested=false;portEXIT_CRITICAL(&publication_lock);
        xSemaphoreTake(mutex,portMAX_DELAY);
        if(open && engine.state!=OTA_AUTHORIZED && engine.state!=OTA_RECEIVING && engine.state!=OTA_READY && !update_server) {
            if(!flash_ok)fail("Update requires verified 4 MiB flash");
            else if(!wired_update_reserve(&generation))fail("Stop active diagnostics, then retry");
            else {
                reserved=true;
                const esp_partition_t *slot=esp_ota_get_next_update_partition(NULL);
                const esp_partition_t *running=esp_ota_get_running_partition();
                if(!running || running->subtype<ESP_PARTITION_SUBTYPE_APP_OTA_0 ||
                   running->subtype>ESP_PARTITION_SUBTYPE_APP_OTA_15 || !slot || slot->address==running->address || !ota_engine_authorize(&engine,slot->size,now_ms()))fail("Dual OTA layout required");
                else {
                    portENTER_CRITICAL(&publication_lock);cancel_requested=false;portEXIT_CRITICAL(&publication_lock);
                    web_status_t web=status_get().web;
                    if(web.state==WEB_OFF) {own_web=services_request_web(true);if(!own_web)fail("Cannot enable update hotspot");}
                    else if(web.state!=WEB_ON)fail("Finish Web Access cleanup first");
                    if(engine.state==OTA_AUTHORIZED && start_update_server()!=ESP_OK)fail("Cannot start local HTTP update");
                }
            }
        }
        ota_engine_tick(&engine,now_ms());
        if(engine.state==OTA_AUTHORIZED || engine.state==OTA_RECEIVING) {
            if(cancellation() || !wired_update_valid(generation) || status_get().web.state==WEB_ERROR || status_get().web.state==WEB_OFF)ota_engine_cancel(&engine,"Cancelled, expired or network changed");
        }
        bool terminal=engine.state==OTA_FAILED || engine.state==OTA_CANCELLED || engine.state==OTA_IDLE || engine.state==OTA_READY;
        bool close=terminal && !upload_pending;
        bool reboot=engine.state==OTA_READY && ready_at && now_ms()-ready_at>=3000;
        publish();xSemaphoreGive(mutex);
        // No engine lock held while the update server joins its tasks.
        if(close) {
            if(update_server) {if(httpd_stop(update_server)==ESP_OK)update_server=NULL;}
            if(reserved) {wired_update_release();reserved=false;}
            web_status_t web=status_get().web;
            if(own_web && web.state!=WEB_STARTING && web.state!=WEB_STOPPING) {
                if(web.state==WEB_OFF)own_web=false;
                else if(services_request_web(false))own_web=false;
            }
        }
        if(reboot)esp_restart();
        if(trial && !boot_done) {
            portENTER_CRITICAL(&publication_lock);unsigned frames=health_frames;bool tasks=tasks_healthy;portEXIT_CRITICAL(&publication_lock);
            ota_boot_action_t a=ota_boot_action(now_ms()-boot_started,frames,nvs_ok,tasks);
            if(a!=OTA_BOOT_WAIT) {
                xSemaphoreTake(mutex,portMAX_DELAY); // Boot metadata writes share the OLED/flash gate.
                if(a==OTA_BOOT_CONFIRM) {if(esp_ota_mark_app_valid_cancel_rollback()==ESP_OK)boot_done=true;}
                else {esp_ota_mark_app_invalid_rollback_and_reboot();esp_restart();}
                xSemaphoreGive(mutex);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
void ota_update_start(void)
{
    boot_started=now_ms();nvs_ok=nvs_flash_init()==ESP_OK;
    uint32_t bytes=0;flash_ok=esp_flash_get_size(NULL,&bytes)==ESP_OK && bytes>=0x400000;
    ota_io_t io={.begin=begin_write,.write=write_chunk,.finish=finish_write,.activate=activate,.abort=abort_write};
    ota_engine_init(&engine,&io,esp_app_get_description()->version);
    esp_ota_img_states_t s;const esp_partition_t *running=esp_ota_get_running_partition();
    trial=running && esp_ota_get_state_partition(running,&s)==ESP_OK && s==ESP_OTA_IMG_PENDING_VERIFY;
    mutex=xSemaphoreCreateMutex();uploads=xQueueCreate(1,sizeof(httpd_req_t *));
    if(!mutex || !uploads) {published.state=OTA_FAILED;strcpy(published.message,"Update task allocation failed");if(trial)esp_restart();return;}
    if(xTaskCreate(upload_worker,"ota_upload",8192,NULL,3,NULL)!=pdPASS ||
       xTaskCreate(supervise,"ota_control",6144,NULL,3,NULL)!=pdPASS) {
        published.state=OTA_FAILED;strcpy(published.message,"Update task startup failed");if(trial)esp_restart();return;
    }
    available=true;
}
// Event registration occurs after services creates the default event loop.
void ota_update_register_events(void)
{
    if(mutex && esp_event_handler_register(WIFI_EVENT,WIFI_EVENT_AP_STADISCONNECTED,wifi_event,NULL)!=ESP_OK) {
        available=false;ota_update_cancel();
    }
}
bool ota_update_request_session(void)
{
    if(!available)return false;
    portENTER_CRITICAL(&publication_lock);
    bool ok=!open_requested && (published.state==OTA_IDLE || published.state==OTA_FAILED || published.state==OTA_CANCELLED);
    if(ok)open_requested=true;
    portEXIT_CRITICAL(&publication_lock);return ok;
}
void ota_update_cancel(void)
{
    portENTER_CRITICAL(&publication_lock);cancel_requested=true;portEXIT_CRITICAL(&publication_lock);
}
void ota_update_health_frame(bool healthy)
{
    portENTER_CRITICAL(&publication_lock);
    if(health_frames<1000)++health_frames;
    tasks_healthy=healthy;shown_notice_id=notice_id;
    portEXIT_CRITICAL(&publication_lock);
}

bool ota_update_ui_begin(void) { return mutex && xSemaphoreTake(mutex,0)==pdTRUE; }
void ota_update_ui_end(void) { xSemaphoreGive(mutex); }
