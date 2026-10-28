// Actual production adapters with only ESP-IDF hardware/HTTP/task I/O stubbed.
// No crypto identity or signing dependency; SDK image checks are mocked at I/O boundary.
#define _DEFAULT_SOURCE
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../main/ota_update.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
__asm__(".global _binary_update_html_start\n_binary_update_html_start:\n.byte 0\n.global _binary_update_html_end\n_binary_update_html_end:\n");
static jmp_buf loop_done;
static httpd_req_t *current_req,*queued;
static bool network_valid,async_fail,queue_fail,finished_request;
static bool acknowledge_notice=true,prepare_seen,verify_seen;
static int recv_mode,io_failure,boot_selected,aborted,ended,confirmed,invalidated,restarted,stopped;
static size_t flash_used;
static uint8_t flashed[4096],package[4096];
static size_t package_len;
static uint64_t clock_now;
static app_status_t app;
static dhcp_status_t address;
static esp_partition_t active_slot={.subtype=0x10,.address=0x20000,.size=0x1e0000};
static esp_partition_t next_slot={.subtype=0x11,.address=0x200000,.size=0x1e0000};
int xSemaphoreTake(SemaphoreHandle_t m,unsigned t){(void)t;CHECK(m);return 1;}
int xSemaphoreGive(SemaphoreHandle_t m){CHECK(m);return 1;}
SemaphoreHandle_t xSemaphoreCreateMutex(void){return (void*)1;}
QueueHandle_t xQueueCreate(unsigned n,size_t s){(void)n;(void)s;return (void*)2;}
int xQueueReceive(QueueHandle_t q,void*p,unsigned t){(void)q;(void)t;if(!queued)longjmp(loop_done,1);*(httpd_req_t**)p=queued;queued=NULL;return pdTRUE;}
int xQueueSend(QueueHandle_t q,const void*p,unsigned t){(void)q;(void)t;if(queue_fail)return 0;CHECK(!queued);queued=*(httpd_req_t*const*)p;return pdTRUE;}
int xTaskCreate(void (*fn)(void*),const char*n,unsigned s,void*a,unsigned p,void*h){(void)fn;(void)n;(void)s;(void)a;(void)p;(void)h;return pdPASS;}
void vTaskDelay(unsigned t){if(notice_id!=shown_notice_id){clock_now+=t;if(acknowledge_notice){prepare_seen|=!strcmp(engine.message,"Preparing update...");verify_seen|=!strcmp(engine.message,"Verifying...");ota_update_health_frame(true);}return;}if(t==1){++clock_now;return;}longjmp(loop_done,1);}
unsigned uxTaskGetStackHighWaterMark(void*a){(void)a;return 4096;}
int64_t esp_timer_get_time(void){return (int64_t)clock_now*1000;}
void esp_fill_random(void*p,size_t n){memset(p,0xa5,n);}
unsigned esp_get_free_heap_size(void){return 120000;}
unsigned esp_get_minimum_free_heap_size(void){return 100000;}
size_t heap_caps_get_largest_free_block(unsigned c){(void)c;return 64000;}
void esp_restart(void){++restarted;longjmp(loop_done,1);}
esp_err_t esp_flash_get_size(void*p,uint32_t*s){(void)p;*s=0x400000;return ESP_OK;}
const esp_app_desc_t *esp_app_get_description(void){static esp_app_desc_t d={"0.5.0"};return &d;}
const esp_partition_t *esp_ota_get_running_partition(void){return &active_slot;}
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t*p){(void)p;return &next_slot;}
esp_err_t esp_ota_begin(const esp_partition_t*p,size_t n,esp_ota_handle_t*h){CHECK(shown_notice_id==notice_id&&notice_id>0);CHECK(p->address!=active_slot.address&&n==OTA_WITH_SEQUENTIAL_WRITES);*h=1;return io_failure==1?ESP_FAIL:ESP_OK;}
esp_err_t esp_ota_write(esp_ota_handle_t h,const void*p,size_t n){CHECK(shown_notice_id==notice_id);CHECK(h==1&&flash_used+n<=sizeof(flashed));memcpy(flashed+flash_used,p,n);flash_used+=n;return io_failure==2?ESP_FAIL:ESP_OK;}
esp_err_t esp_ota_abort(esp_ota_handle_t h){CHECK(h==1);++aborted;return ESP_OK;}
esp_err_t esp_ota_end(esp_ota_handle_t h){CHECK(shown_notice_id==notice_id&&notice_id>=2);CHECK(h==1);++ended;return io_failure==3?ESP_FAIL:ESP_OK;}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*p){CHECK(p==&next_slot&&ended==1);++boot_selected;return io_failure==5?ESP_FAIL:ESP_OK;}
esp_err_t esp_image_verify(int mode,const esp_partition_pos_t*p,esp_image_metadata_t*m){(void)mode;CHECK(p->offset==next_slot.address);m->image_len=flash_used+(io_failure==7?1:0);return io_failure==4?ESP_FAIL:ESP_OK;}
esp_err_t esp_ota_get_state_partition(const esp_partition_t*p,esp_ota_img_states_t*s){(void)p;*s=ESP_OTA_IMG_PENDING_VERIFY;return ESP_OK;}
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void){++confirmed;return io_failure==6?ESP_FAIL:ESP_OK;}
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void){++invalidated;return ESP_FAIL;}
esp_err_t nvs_flash_init(void){return ESP_OK;}
esp_err_t nvs_flash_init_partition(const char*p){CHECK(!strcmp(p,"ota_keys"));return ESP_OK;}
esp_err_t nvs_open_from_partition(const char*p,const char*n,int mode,nvs_handle_t*h){(void)p;(void)n;(void)mode;(void)h;return ESP_FAIL;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char*n,void*p,size_t*s){(void)h;(void)n;(void)p;(void)s;return ESP_FAIL;}
void nvs_close(nvs_handle_t h){(void)h;}
esp_err_t esp_event_handler_register(esp_event_base_t b,int id,void(*fn)(void*,esp_event_base_t,int32_t,void*),void*a){(void)b;(void)id;(void)fn;(void)a;return ESP_OK;}
app_status_t status_get(void){return app;}
bool services_request_web(bool enable){app.web.state=enable?WEB_ON:WEB_OFF;return true;}
dhcp_status_t dhcp_test_status(void){return address;}
bool wired_update_reserve(unsigned*g){*g=1;return network_valid;}
bool wired_update_valid(unsigned g){return network_valid&&g==1;}
void wired_update_extend(void){}
void wired_update_release(void){reserved=false;}
int httpd_req_to_sockfd(httpd_req_t*r){current_req=r;return 3;}
int test_getsockname(int fd,struct sockaddr*p,socklen_t*n){(void)fd;(void)n;struct sockaddr_in*s=(void*)p;s->sin_family=AF_INET;s->sin_addr.s_addr=htonl(current_req->local);return 0;}
int test_getpeername(int fd,struct sockaddr*p,socklen_t*n){(void)fd;(void)n;struct sockaddr_in*s=(void*)p;s->sin_family=AF_INET;s->sin_addr.s_addr=htonl(current_req->peer);return 0;}
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t*r,const char*k,char*p,size_t n){const char*v=!strcmp(k,"Host")?r->host:!strcmp(k,"Origin")?r->origin:!strcmp(k,"Content-Type")?r->type:NULL;if(!v||strlen(v)>=n)return ESP_FAIL;strcpy(p,v);return ESP_OK;}
esp_err_t httpd_resp_set_status(httpd_req_t*r,const char*s){snprintf(r->response_status,sizeof(r->response_status),"%s",s);return ESP_OK;}
esp_err_t httpd_resp_set_type(httpd_req_t*r,const char*s){(void)r;(void)s;return ESP_OK;}
esp_err_t httpd_resp_set_hdr(httpd_req_t*r,const char*k,const char*v){(void)r;(void)k;(void)v;return ESP_OK;}
esp_err_t httpd_resp_send(httpd_req_t*r,const char*p,int n){if(n<0)n=strlen(p);if(n>255)n=255;memcpy(r->response_body,p,n);r->response_body[n]=0;return ESP_OK;}
esp_err_t httpd_req_async_handler_begin(httpd_req_t*r,httpd_req_t**out){if(async_fail)return ESP_FAIL;*out=r;return ESP_OK;}
esp_err_t httpd_req_async_handler_complete(httpd_req_t*r){(void)r;finished_request=true;return ESP_OK;}
int httpd_req_recv(httpd_req_t*r,char*p,size_t n){
 if(r->offset>=300 && recv_mode){
  int mode=recv_mode;recv_mode=0;
  if(mode==1)return 0;
  if(mode==2){clock_now+=11000;return HTTPD_SOCK_ERR_TIMEOUT;}
  if(mode==3)ota_update_cancel();
  if(mode==4)network_valid=false;
  if(mode==5)wifi_event(NULL,WIFI_EVENT,WIFI_EVENT_AP_STADISCONNECTED,NULL);
 }
 if(n>100)n=100;CHECK(r->offset+n<=package_len);memcpy(p,r->body+r->offset,n);r->offset+=n;return n;
}
esp_err_t httpd_start(httpd_handle_t*h,const httpd_config_t*c){CHECK(c->server_port==8080&&c->max_open_sockets==2);*h=(void*)3;return ESP_OK;}
esp_err_t httpd_stop(httpd_handle_t h){CHECK(h);++stopped;return ESP_OK;}
esp_err_t httpd_register_uri_handler(httpd_handle_t h,const httpd_uri_t*u){CHECK(h&&u->handler);return ESP_OK;}
static void reset(bool ethernet)
{
 writer_open=false;flash_used=0;boot_selected=aborted=ended=confirmed=invalidated=restarted=stopped=0;
 acknowledge_notice=true;prepare_seen=verify_seen=false;notice_id=shown_notice_id=0;recv_mode=io_failure=0;async_fail=queue_fail=finished_request=false;cancel_requested=upload_pending=own_web=open_requested=false;
 network_valid=flash_ok=nvs_ok=available=true;reserved=true;generation=1;trial=boot_done=false;clock_now=100;boot_started=0;ready_at=0;
 mutex=(void*)1;uploads=(void*)2;update_server=(void*)3;queued=NULL;
 app.web.state=WEB_ON;address=(dhcp_status_t){.active=true};strcpy(address.ip,"192.0.2.10");strcpy(address.mask,"255.255.255.0");
 ota_io_t io={begin_write,write_chunk,finish_write,activate,abort_write,NULL};ota_engine_init(&engine,&io,"0.5.0");CHECK(ota_engine_authorize(&engine,0x1e0000,clock_now));
 upload_wifi=!ethernet;publish();
}
static httpd_req_t request(bool ethernet)
{
 return (httpd_req_t){.local=ethernet?0xc000020a:0xc0a80401,.peer=ethernet?0xc000020b:0xc0a80402,
 .host=ethernet?"192.0.2.10:8080":"192.168.4.1:8080",.origin=ethernet?"http://192.0.2.10:8080":"http://192.168.4.1:8080",
 .type="application/octet-stream",.content_len=package_len,.body=package};
}
static void run_worker(void){if(!setjmp(loop_done))upload_worker(NULL);}
static void run_supervisor(void){if(!setjmp(loop_done))supervise(NULL);}
int main(void)
{
 package_len=640;memset(package,0,sizeof(package));package[0]=0xe9;package[1]=4;package[23]=1;
 package[28]=0;package[29]=1;package[32]=0x32;package[33]=0x54;package[34]=0xcd;package[35]=0xab;
 strcpy((char*)package+48,"0.5.1");strcpy((char*)package+80,"Syndetis");
 for(int eth=0;eth<2;++eth){
  reset(eth);httpd_req_t r=request(eth);CHECK(upload_post(&r)==ESP_OK&&queued==&r&&engine.state==OTA_RECEIVING);
  httpd_req_t second=request(eth);upload_post(&second);CHECK(!strcmp(second.response_status,"409 Conflict")&&queued==&r);
  run_worker();CHECK(finished_request&&!upload_pending&&boot_selected==1&&ended==1&&engine.state==OTA_READY&&flash_used==package_len);
  CHECK(prepare_seen&&verify_seen);CHECK(!strcmp(r.response_status,"200 OK")&&!memcmp(package,flashed,flash_used));
  second=request(eth);upload_post(&second);CHECK(!strcmp(second.response_status,"409 Conflict")&&!queued);
  for(int mode=1;mode<=5;++mode){reset(eth);r=request(eth);upload_post(&r);recv_mode=mode;run_worker();
   if(mode==5&&eth)CHECK(engine.state==OTA_READY);else CHECK(engine.state!=OTA_READY&&boot_selected==0&&aborted==1);
  }
  for(int fail_io=1;fail_io<=5;++fail_io){reset(eth);r=request(eth);io_failure=fail_io;upload_post(&r);run_worker();CHECK(engine.state==OTA_FAILED);if(fail_io<5)CHECK(boot_selected==0);}
 }
 reset(false);httpd_req_t no_oled=request(false);acknowledge_notice=false;
 upload_post(&no_oled);run_worker();CHECK(engine.state==OTA_FAILED&&!boot_selected&&!flash_used);
 // No visible preparing frame: fail closed before any flash operation.
 // Wired HTTP authority remains bound to the current address, subnet and physical session.
 for(int bad=0;bad<14;++bad){reset(true);httpd_req_t r=request(true);
  if(bad==0)r.host="evil.test";if(bad==1)r.origin="http://evil.test";
  if(bad==2)r.host="syndetis-ota.test.evil.test";if(bad==3)r.local=0xc0000263;
  if(bad==4)r.peer=0xc6336401;if(bad==5)r.host="192.0.2.99:8080";
  if(bad==6)r.origin="http://192.0.2.10";if(bad==7)r.host="192.0.2.10";
  if(bad==8)address.active=false;if(bad==9)network_valid=false;
  if(bad==10)address.busy=true;if(bad==11)engine.state=OTA_IDLE;
  if(bad==12)clock_now+=OTA_AUTHORIZE_MS;if(bad==13)r.origin=NULL;
  upload_post(&r);CHECK(!queued&&!boot_selected&&!flash_used);
 }
 // The same raw-image upload works after DHCP/Manual IPv4 changes, with a same-subnet peer.
 reset(true);strcpy(address.ip,"198.51.100.10");httpd_req_t moved=request(true);
 moved.local=0xc633640a;moved.peer=0xc633640b;moved.host="198.51.100.10:8080";moved.origin="http://198.51.100.10:8080";upload_post(&moved);CHECK(queued==&moved);
 run_worker();CHECK(engine.state==OTA_READY&&boot_selected==1);
 for(int eth=0;eth<2;++eth){
  reset(eth);httpd_req_t page=request(eth);page_get(&page);CHECK(strcmp(page.response_status,"403 Forbidden"));
  page.host="evil.test";page_get(&page);CHECK(!strcmp(page.response_status,"403 Forbidden"));
  reset(eth);page=request(eth);engine.state=OTA_IDLE;page_get(&page);CHECK(!strcmp(page.response_status,"403 Forbidden"));
  reset(eth);page=request(eth);status_get_handler(&page);CHECK(strstr(page.response_body,"Authorized"));
  page.host="evil.test";status_get_handler(&page);CHECK(!strcmp(page.response_status,"403 Forbidden"));
  reset(eth);page=request(eth);page.content_len=0;page.type="application/json";page.origin="http://evil.test";
  cancel_post(&page);CHECK(!cancellation()&&!strcmp(page.response_status,"403 Forbidden"));
 }
 // Actual header/socket guards reject cross-origin, rebinding, wrong interface/peer and content type.
 for(int bad=0;bad<8;++bad){reset(false);httpd_req_t r=request(false);
  if(bad==0)r.origin="http://evil.test";if(bad==1)r.origin=NULL;if(bad==2)r.host="evil.test";
  if(bad==3)r.local=0xc0000263;if(bad==4)r.peer=0xc000020a;if(bad==5)r.type="multipart/form-data";
  if(bad==6)engine.state=OTA_IDLE;if(bad==7)r.origin="http://192.168.4.1";
  upload_post(&r);CHECK(!queued&&boot_selected==0&&flash_used==0);
 }
 reset(false);httpd_req_t r=request(false);r.content_len=0;r.type="application/json";cancel_post(&r);CHECK(cancellation());run_supervisor();CHECK(engine.state==OTA_CANCELLED&&stopped==1&&!reserved);
 reset(false);r=request(false);r.content_len=0x1e0000+1;upload_post(&r);CHECK(!queued&&engine.state==OTA_FAILED);
 reset(false);async_fail=true;r=request(false);upload_post(&r);CHECK(!queued&&engine.state==OTA_FAILED);
 reset(false);queue_fail=true;r=request(false);upload_post(&r);CHECK(!queued&&finished_request&&engine.state==OTA_FAILED);
 reset(false);r=request(false);clock_now+=OTA_AUTHORIZE_MS;upload_post(&r);CHECK(!queued&&engine.state==OTA_FAILED);
 // SDK esp_image_verify rejection and exact image-length check prevent activation.
 reset(false);r=request(false);io_failure=4;upload_post(&r);run_worker();CHECK(engine.state==OTA_FAILED&&boot_selected==0);
 reset(false);r=request(false);io_failure=7;upload_post(&r);run_worker();CHECK(engine.state==OTA_FAILED&&boot_selected==0);
 reset(false);package[12]=2;r=request(false);upload_post(&r);run_worker();CHECK(engine.state==OTA_FAILED&&!flash_used);package[12]=0;
 reset(false);update_server=NULL;engine.state=OTA_IDLE;flash_ok=false;open_requested=true;run_supervisor();CHECK(engine.state==OTA_FAILED&&!update_server);
 reset(false);update_server=NULL;engine.state=OTA_IDLE;publish();CHECK(ota_update_request_session());CHECK(!ota_update_request_session());run_supervisor();CHECK(engine.state==OTA_AUTHORIZED&&update_server&&!strcmp(engine.message,"Update mode active"));
 reset(false);engine.state=OTA_IDLE;trial=true;health_frames=8;tasks_healthy=true;clock_now=4000;run_supervisor();CHECK(confirmed==1&&boot_done&&!invalidated);
 reset(false);engine.state=OTA_IDLE;trial=true;health_frames=8;tasks_healthy=false;clock_now=31000;run_supervisor();CHECK(invalidated==1&&restarted==1&&!confirmed);
 reset(false);engine.state=OTA_IDLE;trial=true;health_frames=8;tasks_healthy=true;io_failure=6;clock_now=4000;run_supervisor();CHECK(confirmed==1&&!boot_done);
 puts("PASS: production OTA HTTP/async adapters with raw application images: both interfaces, auth/CSRF, concurrent/replay, disconnect/stall/cancel/link/Wi-Fi loss, I/O failures, flash-capacity gate, cleanup and boot confirm/rollback API calls");
}
