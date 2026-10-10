#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_event.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/tcpip.h"
#include "network_status.h"

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static network_status_t state;
static esp_eth_handle_t eth_handle;
static QueueHandle_t event_queue;
typedef struct { bool ip_event; network_status_t network; } status_event_t;
static struct { bool busy; network_status_t network; } inspect_job;
static void report_event(bool ip_event)
{
    status_event_t event={.ip_event=ip_event};
    network_status_get(&event.network);
    if (event_queue) xQueueSend(event_queue, &event, 0);
}
static void event_log_task(void *arg)
{
    status_event_t event;
    for (;;) if (xQueueReceive(event_queue,&event,pdMS_TO_TICKS(100))==pdTRUE) {
        const network_status_t *n=&event.network;
        if (event.ip_event && n->ipv4_ready) ESP_LOGI("SYNDETIS","IPv4 acquired " IPSTR,IP2STR(&n->ipv4.ip));
        else if (event.ip_event) ESP_LOGI("SYNDETIS","IPv4 lost; diagnostic cancellation requested (generation %u)",(unsigned)n->link_generation);
        else ESP_LOGI("SYNDETIS","Ethernet link %s%s (generation %u)",n->link_up ? "up":"down",n->link_up ? "":"; diagnostic cancellation requested",(unsigned)n->link_generation);
    }
}

static void on_eth(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    portENTER_CRITICAL(&lock);
    if (id == ETHERNET_EVENT_CONNECTED || id == ETHERNET_EVENT_DISCONNECTED || id == ETHERNET_EVENT_STOP) ++state.physical_epoch;
    if (id == ETHERNET_EVENT_CONNECTED) network_link_event(&state.lifecycle,true);
    if (id == ETHERNET_EVENT_DISCONNECTED || id == ETHERNET_EVENT_STOP) {
        network_link_event(&state.lifecycle,false);
        state.dhcp_running=false;
        state.speed_known = state.duplex_known = false;
        state.speed_mbps = 0;
        memset(state.dns, 0, sizeof(state.dns));
        memset(&state.ipv4, 0, sizeof(state.ipv4));
    }
    portEXIT_CRITICAL(&lock);
    if (id==ETHERNET_EVENT_CONNECTED || id==ETHERNET_EVENT_DISCONNECTED || id==ETHERNET_EVENT_STOP) report_event(false);
}

static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    portENTER_CRITICAL(&lock);
    if (id == IP_EVENT_ETH_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        if (network_ip_event(&state.lifecycle,true,memcmp(&state.ipv4,&event->ip_info,sizeof(state.ipv4))!=0)) state.ipv4=event->ip_info;
    } else if (id == IP_EVENT_ETH_LOST_IP) {
        network_ip_event(&state.lifecycle,false,true);
        memset(&state.ipv4, 0, sizeof(state.ipv4));
    }
    portEXIT_CRITICAL(&lock);
    if (id==IP_EVENT_ETH_GOT_IP || id==IP_EVENT_ETH_LOST_IP) report_event(true);
}

esp_err_t network_status_init(esp_eth_handle_t ethernet)
{
    eth_handle = ethernet;
    event_queue=xQueueCreate(8,sizeof(status_event_t));
    if (!event_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(event_log_task,"net_events",3072,NULL,1,NULL)!=pdPASS) {
        vQueueDelete(event_queue);event_queue=NULL;return ESP_ERR_NO_MEM;
    }
    /* Installed before esp_eth_start so no initial link/DHCP event is missed. */
    esp_err_t err = esp_eth_ioctl(ethernet, ETH_CMD_G_MAC_ADDR, state.mac);
    if (err != ESP_OK) return err;
    err = esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, on_eth, NULL);
    if (err != ESP_OK) return err;
    err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, on_ip, NULL);
    if (err != ESP_OK) {
        esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, on_eth);
    }
    return err;
}

void network_status_get(network_status_t *status)
{
    portENTER_CRITICAL(&lock);
    *status = state;
    portEXIT_CRITICAL(&lock);
}

/* Executed on TCP/IP thread: esp_netif inspection then calls directly instead
 * of waiting indefinitely on SDK api_lock_sem/api_sync_sem in a worker. */
static void inspect_on_tcpip(void *arg)
{
    network_status_t snapshot=inspect_job.network;
    snapshot.speed_known = snapshot.duplex_known = false;
    snapshot.speed_mbps = 0;
    snapshot.dhcp_running = false;
    memset(snapshot.dns, 0, sizeof(snapshot.dns));
    if (snapshot.link_up && eth_handle) {
        eth_speed_t speed;
        eth_duplex_t duplex;
        if (esp_eth_ioctl(eth_handle, ETH_CMD_G_SPEED, &speed) == ESP_OK) {
            if (speed == ETH_SPEED_10M || speed == ETH_SPEED_100M) {
                snapshot.speed_known = true;
                snapshot.speed_mbps = speed == ETH_SPEED_100M ? 100 : 10;
            }
        }
        if (esp_eth_ioctl(eth_handle, ETH_CMD_G_DUPLEX_MODE, &duplex) == ESP_OK) {
            snapshot.duplex_known = true;
            snapshot.full_duplex = duplex == ETH_DUPLEX_FULL;
        }
    }
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
    if (netif) {
        esp_netif_dhcp_status_t dhcp;
        snapshot.dhcp_running = esp_netif_dhcpc_get_status(netif, &dhcp) == ESP_OK &&
                                dhcp == ESP_NETIF_DHCP_STARTED;
        for (unsigned i = 0; i < 2; ++i) {
            esp_netif_dns_info_t dns;
            if (esp_netif_get_dns_info(netif, i == 0 ? ESP_NETIF_DNS_MAIN : ESP_NETIF_DNS_BACKUP, &dns) == ESP_OK &&
                dns.ip.type == ESP_IPADDR_TYPE_V4) snapshot.dns[i] = dns.ip.u_addr.ip4;
        }
    }
    portENTER_CRITICAL(&lock);
    if (state.revision == snapshot.revision) state = snapshot;
    inspect_job.busy=false;
    portEXIT_CRITICAL(&lock);
}

void network_status_inspect(network_status_t *status)
{
    network_status_get(status);
    if (!status->link_up) return;
    portENTER_CRITICAL(&lock);
    bool busy=inspect_job.busy;
    if (!busy) { inspect_job.busy=true;inspect_job.network=*status; }
    portEXIT_CRITICAL(&lock);
    if (busy) return;
    if (tcpip_try_callback(inspect_on_tcpip,NULL)!=ERR_OK) {
        portENTER_CRITICAL(&lock);inspect_job.busy=false;portEXIT_CRITICAL(&lock);
        return;
    }
    uint32_t revision=status->revision;
    int64_t deadline=esp_timer_get_time()+1000000;
    while (esp_timer_get_time()<deadline) {
        network_status_get(status);
        if (!status->link_up || status->revision!=revision) break;
        portENTER_CRITICAL(&lock);busy=inspect_job.busy;portEXIT_CRITICAL(&lock);
        if (!busy) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    network_status_get(status);
    /* A delayed callback retains permanent storage and cannot overwrite a
     * different network generation; a worker never waits indefinitely. */
}
