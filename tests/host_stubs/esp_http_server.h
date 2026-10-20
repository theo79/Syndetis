#ifndef TEST_ESP_HTTP_SERVER_H
#define TEST_ESP_HTTP_SERVER_H
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
typedef void *httpd_handle_t;
typedef struct { int unused; } httpd_req_t;
typedef struct { int max_open_sockets; bool lru_purge_enable; int recv_wait_timeout, send_wait_timeout; } httpd_config_t;
typedef struct { const char *uri; int method; esp_err_t (*handler)(httpd_req_t *); } httpd_uri_t;
#define HTTPD_DEFAULT_CONFIG() {0}
#define HTTP_GET 0
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
esp_err_t httpd_resp_send(httpd_req_t *, const char *, int);
static inline void httpd_resp_set_hdr(httpd_req_t *r,const char *k,const char *v) {(void)r;(void)k;(void)v;}
static inline void httpd_resp_set_type(httpd_req_t *r,const char *v) {(void)r;(void)v;}
static inline esp_err_t httpd_resp_send_err(httpd_req_t *r,int c,const char *v) {(void)r;(void)v;return c;}
static inline esp_err_t httpd_start(httpd_handle_t *h,const httpd_config_t *c) {(void)h;(void)c;return 0;}
static inline esp_err_t httpd_register_uri_handler(httpd_handle_t h,const httpd_uri_t *u) {(void)h;(void)u;return 0;}
#endif
