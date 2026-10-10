#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "diagnostic_rules.h"

typedef struct {
    union {
        network_lifecycle_t lifecycle;
        struct { bool link_up, ipv4_ready; uint32_t link_generation, revision; };
    };
    uint32_t physical_epoch; /* Ethernet events only; existing IPv4 guards unchanged. */
    esp_netif_ip_info_t ipv4;
    uint8_t mac[6];
    bool speed_known, duplex_known, full_duplex, dhcp_running;
    unsigned speed_mbps;
    esp_ip4_addr_t dns[2];
} network_status_t;

esp_err_t network_status_init(esp_eth_handle_t ethernet);
void network_status_get(network_status_t *status);
/* Worker-only: submit read-only inspection to TCP/IP thread, wait <=1 second
 * with link-generation polling. Never called by UI or event callbacks. */
void network_status_inspect(network_status_t *status);
