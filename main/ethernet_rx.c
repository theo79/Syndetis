#include "ethernet_rx.h"
#include "switch_info.h"
#include "profinet.h"
#if CONFIG_ESP_NETIF_L2_TAP
#error "Review shared Ethernet RX ownership before enabling L2 TAP"
#endif
static esp_err_t rx_input(esp_eth_handle_t driver,uint8_t *buffer,uint32_t length,void *priv,void *info){
    (void)driver;(void)info;
    if(switch_info_capture(buffer,length)||profinet_capture(buffer,length))return ESP_OK;
    return esp_netif_receive((esp_netif_t *)priv,buffer,length,NULL);
}
esp_err_t ethernet_rx_init(esp_eth_handle_t driver,esp_netif_t *netif){
    if(!driver||!netif)return ESP_ERR_INVALID_ARG;
    return esp_eth_update_input_path_info(driver,rx_input,netif);
}
