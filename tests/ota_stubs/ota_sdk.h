#pragma once
// Linux host-only ESP-IDF I/O boundary for exercising actual OTA adapters.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_PARTITION_SUBTYPE_APP_OTA_0 0x10
#define ESP_PARTITION_SUBTYPE_APP_OTA_15 0x1f
#define ESP_OTA_IMG_PENDING_VERIFY 1
#define OTA_WITH_SEQUENTIAL_WRITES 0xffffffffu
#define ESP_IMAGE_VERIFY 0
#define MALLOC_CAP_8BIT 1
#define NVS_READONLY 0
#define WIFI_EVENT "wifi"
#define WIFI_EVENT_AP_STADISCONNECTED 1
#define portMUX_TYPE int
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define portMAX_DELAY 0xffffffffu
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;
typedef unsigned nvs_handle_t;
typedef unsigned esp_ota_handle_t;
typedef unsigned esp_ota_img_states_t;
typedef const char *esp_event_base_t;
typedef struct {uint8_t subtype;uint32_t address,size;} esp_partition_t;
typedef struct {uint32_t offset,size;} esp_partition_pos_t;
typedef struct {uint32_t image_len;} esp_image_metadata_t;
typedef struct {char version[32];} esp_app_desc_t;
typedef struct httpd_req {
 size_t content_len;
 const char *host,*origin,*type;
 const uint8_t *body;size_t offset;
 uint32_t local,peer;
 char response_status[40],response_body[256];
} httpd_req_t;
typedef void *httpd_handle_t;
typedef struct {
 size_t stack_size;unsigned server_port, max_open_sockets,max_uri_handlers,ctrl_port,recv_wait_timeout,send_wait_timeout;
 bool lru_purge_enable;
} httpd_config_t;
typedef struct {
 httpd_config_t httpd;const uint8_t *servercert,*prvtkey_pem;size_t servercert_len,prvtkey_len;
 int transport_mode;unsigned port_secure;
} httpd_ssl_config_t;
#define HTTPD_SSL_CONFIG_DEFAULT() ((httpd_ssl_config_t){.port_secure=443})
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){.server_port=80})
#define HTTP_GET 0
#define HTTP_POST 1
#define HTTPD_RESP_USE_STRLEN -1
#define HTTPD_SOCK_ERR_TIMEOUT -3
typedef struct {const char *uri;int method;esp_err_t (*handler)(httpd_req_t*);} httpd_uri_t;
int xSemaphoreTake(SemaphoreHandle_t,unsigned);
int xSemaphoreGive(SemaphoreHandle_t);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
QueueHandle_t xQueueCreate(unsigned,size_t);
int xQueueReceive(QueueHandle_t,void*,unsigned);
int xQueueSend(QueueHandle_t,const void*,unsigned);
int xTaskCreate(void (*)(void*),const char*,unsigned,void*,unsigned,void*);
void vTaskDelay(unsigned);
unsigned uxTaskGetStackHighWaterMark(void*);
int64_t esp_timer_get_time(void);
void esp_fill_random(void*,size_t);
unsigned esp_get_free_heap_size(void);
unsigned esp_get_minimum_free_heap_size(void);
size_t heap_caps_get_largest_free_block(unsigned);
void esp_restart(void);
esp_err_t esp_flash_get_size(void*,uint32_t*);
const esp_app_desc_t *esp_app_get_description(void);
const esp_partition_t *esp_ota_get_running_partition(void);
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t*);
esp_err_t esp_ota_begin(const esp_partition_t*,size_t,esp_ota_handle_t*);
esp_err_t esp_ota_write(esp_ota_handle_t,const void*,size_t);
esp_err_t esp_ota_abort(esp_ota_handle_t);
esp_err_t esp_ota_end(esp_ota_handle_t);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*);
esp_err_t esp_ota_get_state_partition(const esp_partition_t*,esp_ota_img_states_t*);
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void);
esp_err_t esp_image_verify(int,const esp_partition_pos_t*,esp_image_metadata_t*);
esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_init_partition(const char*);
esp_err_t nvs_open_from_partition(const char*,const char*,int,nvs_handle_t*);
esp_err_t nvs_get_blob(nvs_handle_t,const char*,void*,size_t*);
void nvs_close(nvs_handle_t);
esp_err_t esp_event_handler_register(esp_event_base_t,int,void (*)(void*,esp_event_base_t,int32_t,void*),void*);
int httpd_req_to_sockfd(httpd_req_t*);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t*,const char*,char*,size_t);
esp_err_t httpd_resp_set_status(httpd_req_t*,const char*);
esp_err_t httpd_resp_set_type(httpd_req_t*,const char*);
esp_err_t httpd_resp_set_hdr(httpd_req_t*,const char*,const char*);
esp_err_t httpd_resp_send(httpd_req_t*,const char*,int);
esp_err_t httpd_req_async_handler_begin(httpd_req_t*,httpd_req_t**);
esp_err_t httpd_req_async_handler_complete(httpd_req_t*);
int httpd_req_recv(httpd_req_t*,char*,size_t);
esp_err_t httpd_ssl_start(httpd_handle_t*,const httpd_ssl_config_t*);
esp_err_t httpd_ssl_stop(httpd_handle_t);
esp_err_t httpd_register_uri_handler(httpd_handle_t,const httpd_uri_t*);
int test_getsockname(int,struct sockaddr*,socklen_t*);
int test_getpeername(int,struct sockaddr*,socklen_t*);
#define getsockname test_getsockname
#define getpeername test_getpeername
void vQueueDelete(QueueHandle_t);

esp_err_t httpd_start(httpd_handle_t*,const httpd_config_t*);
esp_err_t httpd_stop(httpd_handle_t);
