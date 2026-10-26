#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct {esp_ip4_addr_t ip,netmask,gw;} esp_netif_ip_info_t;
typedef struct {struct {union {esp_ip4_addr_t ip4;} u_addr;} ip;} esp_netif_dns_info_t;
typedef struct {int flags,get_ip_event,lost_ip_event,route_prio;} esp_netif_inherent_config_t;
typedef struct {void *handle; esp_err_t (*transmit)(void*,void*,size_t); void (*driver_free_rx_buffer)(void*,void*);} esp_netif_driver_ifconfig_t;
typedef struct {const esp_netif_inherent_config_t *base;const esp_netif_driver_ifconfig_t *driver;const void *stack;} esp_netif_config_t;
typedef struct {bool up,started;esp_netif_ip_info_t ip;} esp_netif_t;
#define ESP_NETIF_DHCP_CLIENT 1
#define ESP_NETIF_FLAG_AUTOUP 4
#define ESP_NETIF_DNS_MAX 3
#define ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED 9
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(p) ((const unsigned char*)&(p)->addr)[0],((const unsigned char*)&(p)->addr)[1],((const unsigned char*)&(p)->addr)[2],((const unsigned char*)&(p)->addr)[3]
esp_netif_t *esp_netif_new(const esp_netif_config_t*);
esp_err_t esp_netif_set_mac(esp_netif_t*,uint8_t*);
esp_err_t esp_netif_dhcpc_start(esp_netif_t*);
esp_err_t esp_netif_dhcpc_stop(esp_netif_t*);
esp_err_t esp_netif_set_ip_info(esp_netif_t*,const esp_netif_ip_info_t*);
esp_err_t esp_netif_set_old_ip_info(esp_netif_t*,const esp_netif_ip_info_t*);
esp_err_t esp_netif_set_dns_info(esp_netif_t*,int,esp_netif_dns_info_t*);
esp_err_t esp_netif_get_ip_info(esp_netif_t*,esp_netif_ip_info_t*);
esp_err_t esp_netif_get_dns_info(esp_netif_t*,int,esp_netif_dns_info_t*);
esp_err_t esp_netif_tcpip_exec(esp_err_t (*fn)(void*),void*);
void esp_netif_action_start(void*,void*,int,void*);
void esp_netif_action_stop(void*,void*,int,void*);
bool esp_netif_is_netif_up(esp_netif_t*);
esp_err_t esp_netif_receive(esp_netif_t*,void*,size_t,void*);
esp_err_t esp_netif_get_netif_impl_name(esp_netif_t*,char*);
