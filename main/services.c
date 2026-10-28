#include "services.h"
#include "ota_update.h"
#include "dhcp_test.h"
#include "discovery.h"
#include "access_config.h"
#include "dashboard.h"
#include "ethernet.h"
#include "status.h"
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static QueueHandle_t requests;
static httpd_handle_t server;
static esp_netif_t *ap_netif;
static bool wifi_initialized, ap_running, netif_initialized;
static esp_err_t event_loop_error;

static esp_err_t log_result(const char *operation, esp_err_t err)
{
    if (err != ESP_OK)
        ESP_LOGE("web", "%s: %s (0x%x)", operation, esp_err_to_name(err), (unsigned)err);
    return err;
}

static esp_err_t prepare_wifi(const char **step)
{
    if (event_loop_error != ESP_OK) {
        *step = "Event loop";
        return log_result("esp_event_loop_create_default", event_loop_error);
    }
    if (wifi_initialized) return ESP_OK;
    // Never erase NVS to recover an error. Enabled state is not stored in NVS.
    *step = "NVS init";
    esp_err_t err = log_result("nvs_flash_init", nvs_flash_init());
    if (err != ESP_OK) return err;
    if (!netif_initialized) {
        *step = "Netif init";
        err = log_result("esp_netif_init", esp_netif_init());
        if (err != ESP_OK) return err;
        netif_initialized = true;
    }
    // Expanded default AP helper so allocation/attachment errors don't abort.
    if (!ap_netif) {
        esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_WIFI_AP();
        *step = "AP netif";
        ap_netif = esp_netif_new(&netif_config);
        if (!ap_netif) return log_result("esp_netif_new(AP)", ESP_ERR_NO_MEM);
        *step = "AP attach";
        err = log_result("esp_netif_attach_wifi_ap", esp_netif_attach_wifi_ap(ap_netif));
        if (err != ESP_OK) {
            esp_netif_destroy(ap_netif);
            ap_netif = NULL;
            return err;
        }
        *step = "AP handlers";
        err = log_result("esp_wifi_set_default_wifi_ap_handlers", esp_wifi_set_default_wifi_ap_handlers());
        if (err != ESP_OK) {
            esp_netif_destroy_default_wifi(ap_netif);
            ap_netif = NULL;
            return err;
        }
        // ESP-IDF v6.1 defaults to 192.168.4.1/24 already. Do not set it again:
        // set_ip_info rejects DHCP_INIT too, not just an active DHCP server.
        // Keep DHCP_INIT so the default AP_START handler starts DHCP. AP_STOP
        // stops it and restores INIT, allowing the retained netif to start again.
    }
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    init.nvs_enable = 0; // AP settings stay in RAM too.
    *step = "WiFi init";
    err = log_result("esp_wifi_init", esp_wifi_init(&init));
    if (err != ESP_OK) return err;
    wifi_initialized = true;
    return ESP_OK;
}

static void stop_services(esp_err_t *http_error, esp_err_t *ap_error)
{
    *http_error = ESP_OK;
    *ap_error = ESP_OK;
    if (server) {
        // Stops listener and closes all HTTP sessions, including keep-alives.
        *http_error = log_result("httpd_stop", httpd_stop(server));
        if (*http_error == ESP_OK) server = NULL;
    }
    if (ap_running) {
        // esp_wifi_stop disconnects all AP stations and stops the AP radio.
        // Attempt this even if HTTP shutdown failed, to remove hotspot access.
        *ap_error = log_result("esp_wifi_stop", esp_wifi_stop());
        if (*ap_error == ESP_OK) ap_running = false;
    }
}

static void change_web(bool enable)
{
    web_status_t web = status_get().web;
    esp_err_t err = ESP_OK, http_error = ESP_OK, ap_error = ESP_OK;
    const char *step = "Disable";
    if (enable) {
        // Never restart on top of services left over from a failed shutdown.
        if (server || ap_running) {
            err = log_result("Web enable before cleanup", ESP_ERR_INVALID_STATE);
            step = "Cleanup required";
            goto failed;
        }
        if ((err = prepare_wifi(&step)) != ESP_OK) goto failed;
        step = "RAM storage";
        if ((err = log_result("esp_wifi_set_storage", esp_wifi_set_storage(WIFI_STORAGE_RAM))) != ESP_OK) goto failed;
        step = "AP mode";
        if ((err = log_result("esp_wifi_set_mode(AP)", esp_wifi_set_mode(WIFI_MODE_AP))) != ESP_OK) goto failed;
        wifi_config_t config = {0};
        config.ap.ssid_len = strnlen(web.ssid, sizeof(config.ap.ssid));
        memcpy(config.ap.ssid, web.ssid, config.ap.ssid_len);
        memcpy(config.ap.password, WEB_AP_PASSWORD, sizeof(WEB_AP_PASSWORD));
        config.ap.channel = 1;
        config.ap.max_connection = 4;
        config.ap.authmode = WIFI_AUTH_WPA2_PSK;
        config.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP;
        config.ap.pmf_cfg.capable = true;
        step = "AP configuration";
        if ((err = log_result("esp_wifi_set_config(AP)", esp_wifi_set_config(WIFI_IF_AP, &config))) != ESP_OK) goto failed;
        step = "AP start";
        if ((err = log_result("esp_wifi_start", esp_wifi_start())) != ESP_OK) goto failed;
        ap_running = true;
        step = "HTTP start";
        if ((err = dashboard_start(&server)) != ESP_OK) goto failed;
        web.state = WEB_ON; // Only after BOTH successful starts.
        web.error[0] = '\0';
    } else {
        stop_services(&http_error, &ap_error);
        if (http_error != ESP_OK || ap_error != ESP_OK) goto shutdown_failed;
        web.state = WEB_OFF;
        web.error[0] = '\0';
    }
    goto publish;

failed:
    stop_services(&http_error, &ap_error); // Roll back every successfully started service.
shutdown_failed:
    web.state = WEB_ERROR;
    // Serial logs retain every failed API; OLED shows one short actionable error.
    // Prefer a cleanup failure while a service may still be running.
    if (http_error != ESP_OK) { step = "HTTP stop"; err = http_error; }
    else if (ap_error != ESP_OK) { step = "Hotspot stop"; err = ap_error; }
    snprintf(web.error, sizeof(web.error), "%s: 0x%x", step, (unsigned)err);
publish:
    web.http_running = server != NULL;
    web.ap_running = ap_running;
    status_set_web(&web);
}

static void service_worker(void *arg)
{
    (void)arg;
    event_loop_error = esp_event_loop_create_default();
    if (event_loop_error == ESP_OK) {
        event_loop_error = log_result("esp_netif_init", esp_netif_init());
        netif_initialized = event_loop_error == ESP_OK;
    }
    if (event_loop_error == ESP_OK) { ota_update_register_events(); ethernet_start(); }
    else {
        ethernet_status_t eth = {.state = ETH_ERROR, .error = event_loop_error};
        snprintf(eth.error_step, sizeof(eth.error_step), "Event loop");
        status_set_ethernet(&eth);
    }
    bool enable;
    while (xQueueReceive(requests, &enable, portMAX_DELAY) == pdTRUE) change_web(enable);
    vTaskDelete(NULL);
}

void services_start(void)
{
    web_status_t web = {.state = WEB_OFF};
    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    if (err == ESP_OK) snprintf(web.ssid, sizeof(web.ssid), "Syndetis-%02X%02X", mac[4], mac[5]);
    else {
        web.state = WEB_ERROR;
        log_result("esp_read_mac(SoftAP)", err);
        snprintf(web.error, sizeof(web.error), "SSID MAC: 0x%x", (unsigned)err);
    }
    status_set_web(&web);
    requests = xQueueCreate(1, sizeof(bool));
    if (!requests || xTaskCreate(service_worker, "services", 6144, NULL, 4, NULL) != pdPASS) {
        log_result(requests ? "xTaskCreate(services)" : "xQueueCreate(web)", ESP_ERR_NO_MEM);
        if (requests) { vQueueDelete(requests); requests = NULL; }
        ethernet_status_t eth = {.state = ETH_ERROR, .error = ESP_ERR_NO_MEM};
        snprintf(eth.error_step, sizeof(eth.error_step), "Service worker");
        status_set_ethernet(&eth);
        web.state = WEB_ERROR;
        snprintf(web.error, sizeof(web.error), "Service worker: no memory");
        status_set_web(&web);
    }
}

bool services_request_web(bool enable)
{
    web_status_t web = status_get().web;
    ota_state_t update=ota_update_status().state;
    if(!enable && (update==OTA_AUTHORIZED || update==OTA_RECEIVING || update==OTA_VALIDATING)) {
        ota_update_cancel();return false;
    }
    if (!requests || web.state == WEB_STARTING || web.state == WEB_STOPPING) return false;
    if (enable && (!web.ssid[0] || web.state == WEB_ON || web.ap_running || web.http_running)) return false;
    web_status_t before = web;
    web.state = enable ? WEB_STARTING : WEB_STOPPING;
    status_set_web(&web);
    if (xQueueSend(requests, &enable, 0) != pdTRUE) { status_set_web(&before); return false; }
    return true;
}

bool services_boot_healthy(void)
{
    return status_get().ethernet.state==ETH_READY && discovery_worker_started() && wired_worker_healthy();
}
