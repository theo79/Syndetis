#include "status.h"
#include "freertos/FreeRTOS.h"

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static app_status_t status = {
    .ethernet.state = ETH_INITIALIZING,
    .web.state = WEB_OFF,
};

app_status_t status_get(void)
{
    portENTER_CRITICAL(&lock);
    app_status_t copy = status;
    portEXIT_CRITICAL(&lock);
    return copy;
}

void status_set_ethernet(const ethernet_status_t *ethernet)
{
    portENTER_CRITICAL(&lock);
    status.ethernet = *ethernet;
    portEXIT_CRITICAL(&lock);
}

void status_ethernet_link(bool up, int speed_mbps, int duplex)
{
    portENTER_CRITICAL(&lock);
    if (status.ethernet.state == ETH_READY) {
        status.ethernet.link_up = up;
        status.ethernet.speed_mbps = up ? speed_mbps : 0;
        status.ethernet.duplex = up ? duplex : 0;
    }
    portEXIT_CRITICAL(&lock);
}

void status_set_web(const web_status_t *web)
{
    portENTER_CRITICAL(&lock);
    status.web = *web;
    portEXIT_CRITICAL(&lock);
}

const char *status_ethernet_state_name(ethernet_state_t state)
{
    switch (state) {
    case ETH_INITIALIZING: return "Initializing";
    case ETH_READY: return "Ready";
    default: return "Error";
    }
}
