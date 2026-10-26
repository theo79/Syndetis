#ifndef TEST_ESP_HTTP_SERVER_H
#define TEST_ESP_HTTP_SERVER_H
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "esp_err.h"
typedef void *httpd_handle_t;
typedef struct { size_t content_len; const char *uri; const char *host,*origin,*type,*body; int used, status; bool denied; } httpd_req_t;
typedef struct { int max_open_sockets; bool lru_purge_enable; int recv_wait_timeout, send_wait_timeout; } httpd_config_t;
typedef struct { const char *uri; int method; esp_err_t (*handler)(httpd_req_t *); } httpd_uri_t;
#define HTTPD_DEFAULT_CONFIG() {0}
#define HTTP_GET 0
#define HTTP_POST 1
#define HTTPD_RESP_USE_STRLEN -1
#define HTTPD_403_FORBIDDEN 403
#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
esp_err_t httpd_resp_send(httpd_req_t *, const char *, int);
int httpd_req_to_sockfd(httpd_req_t*);
static inline void httpd_resp_set_hdr(httpd_req_t *r,const char *k,const char *v) {(void)r;(void)k;(void)v;}
static inline void httpd_resp_set_type(httpd_req_t *r,const char *v) {(void)r;(void)v;}
static inline esp_err_t httpd_resp_send_err(httpd_req_t *r,int c,const char *v) {(void)r;(void)v;r->status=c;return c;}
static inline esp_err_t httpd_start(httpd_handle_t *h,const httpd_config_t *c) {(void)h;(void)c;return 0;}
static inline esp_err_t httpd_register_uri_handler(httpd_handle_t h,const httpd_uri_t *u) {(void)h;(void)u;return 0;}
static inline void httpd_resp_set_status(httpd_req_t*r,const char*s){r->status=(s[0]-'0')*100+(s[1]-'0')*10+s[2]-'0';}
static inline esp_err_t httpd_req_get_hdr_value_str(httpd_req_t*r,const char*k,char*v,size_t n){
 const char*s=!strcmp(k,"Host")?r->host:!strcmp(k,"Origin")?r->origin:r->type;
 if(!s||strlen(s)>=n)return ESP_FAIL;strcpy(v,s);return ESP_OK;
}
static inline int httpd_req_recv(httpd_req_t*r,char*b,size_t n){
 if(!r->body||r->used>=2)return -1;if(n>1)n=1;memcpy(b,r->body+r->used,n);r->used+=n;return (int)n;
}
#endif
