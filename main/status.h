#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum { ETH_INITIALIZING, ETH_READY, ETH_ERROR } ethernet_state_t;
typedef enum { WEB_OFF, WEB_STARTING, WEB_ON, WEB_STOPPING, WEB_ERROR } web_state_t;

typedef struct {
    ethernet_state_t state;
    bool link_up;
    int speed_mbps;             // 0 = unavailable
    int duplex;                 // 0 = unavailable, 1 = half, 2 = full
    bool mac_valid;
    uint8_t mac[6];
    esp_err_t error;
    char error_step[32];
} ethernet_status_t;

typedef struct {
    web_state_t state;
    bool ap_running;
    bool http_running;
    char ssid[33];
    char error[128];
} web_status_t;

typedef struct {
    ethernet_status_t ethernet;
    web_status_t web;
} app_status_t;

// Only copies under a brief critical section; no driver/UI calls while locked.
app_status_t status_get(void);
void status_set_ethernet(const ethernet_status_t *ethernet);
void status_ethernet_link(bool up, int speed_mbps, int duplex);
void status_set_web(const web_status_t *web);
const char *status_ethernet_state_name(ethernet_state_t state);
