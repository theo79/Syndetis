#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ping/ping_sock.h"
#include "lwip/dns.h"
#include "lwip/tcpip.h"
#include "sdkconfig.h"
#include "autotest.h"
#include "firmware_update.h"
#include "diagnostic.h"

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static autotest_status_t state;
static TaskHandle_t worker;
static uint32_t run_token;
static uint32_t requested_generation;
/* Permanent callback storage survives stopped sessions and late resolver callbacks. */
static struct {
    SemaphoreHandle_t done;
    esp_ping_handle_t handle;
    uint32_t sent, received, sum_ms, min_ms, max_ms;
    uint32_t revision, token;
} ping_state;
static struct {
    bool busy, done, success;
    char name[64];
    uint32_t revision, token;
} dns_job;

const char *autotest_grade_name(autotest_grade_t grade)
{
    static const char *names[] = {"PASS", "WARN", "FAIL", "SKIP"};
    return grade >= AT_PASS && grade <= AT_SKIP ? names[grade] : "SKIP";
}

void autotest_get(autotest_status_t *status)
{
    portENTER_CRITICAL(&lock);
    *status = state;
    portEXIT_CRITICAL(&lock);
}

static void progress(const char *text)
{
    portENTER_CRITICAL(&lock);
    strncpy(state.progress,text,sizeof(state.progress)-1);
    state.progress[sizeof(state.progress)-1]=0;
    portEXIT_CRITICAL(&lock);
    /* Keep short stages visible without blocking the UI task. */
    vTaskDelay(pdMS_TO_TICKS(250));
}

static bool update_running(void)
{
    ota_status_t ota;
    firmware_update_status(&ota);
    return ota.phase == OTA_RECEIVING || ota.phase == OTA_SUCCESS;
}

static bool network_changed(uint32_t revision)
{
    network_status_t net;
    network_status_get(&net);
    return !diagnostic_accept(1,1,revision,net.revision,net.link_up) || update_running();
}

static void ping_success(esp_ping_handle_t handle, void *arg)
{
    if (!diagnostic_current(ping_state.token) || network_changed(ping_state.revision)) return;
    uint32_t ms = 0;
    esp_ping_get_profile(handle, ESP_PING_PROF_TIMEGAP, &ms, sizeof(ms));
    ping_state.sum_ms += ms;
    if (ms < ping_state.min_ms) ping_state.min_ms = ms;
    if (ms > ping_state.max_ms) ping_state.max_ms = ms;
}

static void ping_end(esp_ping_handle_t handle, void *arg)
{
    esp_ping_get_profile(handle, ESP_PING_PROF_REQUEST, &ping_state.sent, sizeof(ping_state.sent));
    esp_ping_get_profile(handle, ESP_PING_PROF_REPLY, &ping_state.received, sizeof(ping_state.received));
    xSemaphoreGive(ping_state.done); /* Last callback action; publishes collected statistics. */
}

static bool ping_gateway(autotest_result_t *result)
{
    /* Reap a previous delayed completion before reusing permanent callback state. */
    if (ping_state.handle) {
        if (xSemaphoreTake(ping_state.done, 0) != pdTRUE) return false;
        esp_ping_delete_session(ping_state.handle);
        ping_state.handle = NULL;
    }
    while (xSemaphoreTake(ping_state.done, 0) == pdTRUE) {}
    ping_state.sent = ping_state.received = ping_state.sum_ms = ping_state.max_ms = 0;
    ping_state.min_ms = UINT32_MAX;
    ping_state.revision=result->network.revision;ping_state.token=run_token;
    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.count = 3;
    config.timeout_ms = 1000;
    config.interval_ms = 500;
    config.task_stack_size = 3072;
    ip_addr_set_ip4_u32(&config.target_addr, result->network.ipv4.gw.addr);
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
    int index = netif ? esp_netif_get_netif_impl_index(netif) : -1;
    if (index <= 0) return false;
    config.interface = index;
    const esp_ping_callbacks_t callbacks = {.on_ping_success = ping_success, .on_ping_end = ping_end};
    if (esp_ping_new_session(&config, &callbacks, &ping_state.handle) != ESP_OK) return false;
    if (esp_ping_start(ping_state.handle) != ESP_OK) {
        esp_ping_delete_session(ping_state.handle);
        ping_state.handle = NULL;
        return false;
    }
    bool ended = false, stopped = false;
    int64_t deadline = esp_timer_get_time() + 6000000;
    for (;;) {
        if (xSemaphoreTake(ping_state.done, pdMS_TO_TICKS(100)) == pdTRUE) { ended = true; break; }
        if (network_changed(result->network.revision) || esp_timer_get_time() >= deadline) {
            stopped = true;
            ESP_LOGI("AUTOTEST","cancel requested: network change or probe deadline");
            esp_ping_stop(ping_state.handle);
            /* One socket receive may still be in progress (timeout is 1 second). */
            ended = xSemaphoreTake(ping_state.done, pdMS_TO_TICKS(2000)) == pdTRUE;
            break;
        }
    }
    if (!ended) return false; /* Context remains valid; next run cannot reuse it yet. */
    result->ping_sent = ping_state.sent;
    result->ping_received = ping_state.received;
    result->loss_percent = ping_state.sent ? (ping_state.sent - ping_state.received) * 100 / ping_state.sent : 0;
    result->ping_measured = ping_state.sent != 0;
    if (ping_state.received) {
        result->ping_avg_ms = ping_state.sum_ms / ping_state.received;
        result->ping_min_ms = ping_state.min_ms;
        result->ping_max_ms = ping_state.max_ms;
    }
    esp_ping_delete_session(ping_state.handle);
    ping_state.handle = NULL;
    return !stopped;
}

static void dns_finished(const char *name, const ip_addr_t *address, void *arg)
{
    bool current=diagnostic_current(dns_job.token) && !network_changed(dns_job.revision);
    portENTER_CRITICAL(&lock);
    dns_job.success = current && address != NULL;
    dns_job.done = true;
    dns_job.busy = false;
    portEXIT_CRITICAL(&lock);
}

static void resolve_in_tcpip(void *arg)
{
    if (!diagnostic_current(dns_job.token) || network_changed(dns_job.revision)) { dns_finished(NULL,NULL,NULL);return; }
    ip_addr_t address;
    err_t err = dns_gethostbyname_addrtype(dns_job.name, &address, dns_finished, NULL, LWIP_DNS_ADDRTYPE_IPV4);
    if (err != ERR_INPROGRESS) dns_finished(dns_job.name, err == ERR_OK ? &address : NULL, NULL);
}

static autotest_grade_t optional_resolution(uint32_t revision)
{
    const char *name = CONFIG_SYNDETIS_AUTOTEST_DNS_HOSTNAME;
    if (!name[0]) return AT_SKIP;
    if (strlen(name) >= sizeof(dns_job.name)) return AT_WARN;
    char name_copy[64];
    snprintf(name_copy,sizeof(name_copy),"%s",name);
    portENTER_CRITICAL(&lock);
    bool busy = dns_job.busy;
    if (!busy) {
        dns_job.busy = true;
        dns_job.revision=revision;dns_job.token=run_token;
        dns_job.done = dns_job.success = false;
        memcpy(dns_job.name,name_copy,sizeof(name_copy));
    }
    portEXIT_CRITICAL(&lock);
    if (busy) return AT_WARN;
    /* lwIP DNS APIs run only in the TCP/IP thread; the AutoTest worker waits. */
    if (tcpip_try_callback(resolve_in_tcpip, NULL) != ERR_OK) {
        dns_finished(NULL, NULL, NULL);
        return AT_WARN;
    }
    int64_t deadline = esp_timer_get_time() + 5000000;
    while (esp_timer_get_time() < deadline && !network_changed(revision)) {
        portENTER_CRITICAL(&lock);
        bool done = dns_job.done, success = dns_job.success;
        portEXIT_CRITICAL(&lock);
        if (done) return AT_RESOLUTION(success);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    /* lwIP may finish later. Permanent storage prevents use-after-free and
     * no later completion changes a published AutoTest result.
     */
    return AT_WARN;
}

static void execute(autotest_result_t *result)
{
    for (unsigned i = 0; i < AT_CHECK_COUNT; ++i) result->checks[i] = AT_SKIP;
    uint32_t generation=requested_generation;
    progress("Checking link...");
    network_status_inspect(&result->network);
    if (result->network.link_generation!=generation) {
        result->checks[AT_CHECK_LINK]=AT_FAIL;
        snprintf(result->note,sizeof(result->note),"ABORTED_LINK_DOWN/IP_CHANGE");return;
    }
    result->checks[AT_CHECK_LINK] = AT_LINK(result->network.link_up);
    if (!result->network.link_up) {
        result->checks[AT_CHECK_LINK]=AT_FAIL;
        snprintf(result->note,sizeof(result->note),"Ethernet link down");return;
    }
    progress("Checking DHCP/IP...");
    int64_t deadline = esp_timer_get_time() + 10000000;
    while (!result->network.ipv4.ip.addr && result->network.link_up && result->network.link_generation==generation && !update_running() && esp_timer_get_time() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(100));
        network_status_get(&result->network);
    }
    network_status_inspect(&result->network);
    if (result->network.link_generation!=generation) {
        result->checks[AT_CHECK_LINK]=AT_FAIL;
        snprintf(result->note,sizeof(result->note),"ABORTED_LINK_DOWN/IP_CHANGE");return;
    }
    result->checks[AT_CHECK_LINK] = AT_LINK(result->network.link_up);
    result->checks[AT_CHECK_NEGOTIATION] = result->network.link_up ?
        AT_NEGOTIATION(result->network.speed_known && result->network.duplex_known, result->network.full_duplex) : AT_SKIP;
    uint32_t ip = esp_netif_htonl(result->network.ipv4.ip.addr);
    uint32_t mask = esp_netif_htonl(result->network.ipv4.netmask.addr);
    bool valid_ip = AT_IP_VALID(ip, mask);
    result->checks[AT_CHECK_DHCP] = AT_DHCP(result->network.link_up, valid_ip, result->network.dhcp_running);
    result->checks[AT_CHECK_IPV4] = !result->network.link_up ? AT_SKIP : valid_ip ? AT_PASS : AT_FAIL;
    if (!result->network.link_up || !valid_ip) {
        snprintf(result->note, sizeof(result->note), "Link lost or no valid IPv4 configuration");
        return;
    }
    uint32_t gateway = esp_netif_htonl(result->network.ipv4.gw.addr);
    bool gateway_valid = AT_GATEWAY_VALID(ip, mask, gateway);
    if (!gateway_valid) {
        result->checks[AT_CHECK_IPV4] = AT_FAIL;
        result->checks[AT_CHECK_GATEWAY] = AT_SKIP;
        snprintf(result->note, sizeof(result->note), "Invalid/off-subnet default gateway");
    } else if (gateway) {
        progress("Testing gateway...");
        bool completed = ping_gateway(result);
        result->checks[AT_CHECK_GATEWAY] = completed ? AT_GATEWAY(true, result->ping_sent, result->ping_received) : AT_FAIL;
        if (!completed) snprintf(result->note, sizeof(result->note), "Gateway test interrupted or unavailable");
        else if (!result->ping_received) snprintf(result->note, sizeof(result->note), "Gateway did not reply to ICMP");
    } else {
        result->checks[AT_CHECK_GATEWAY] = AT_GATEWAY(false, 0, 0);
        snprintf(result->note, sizeof(result->note), "No default gateway configured");
    }
    if (network_changed(result->network.revision)) {
        result->checks[AT_CHECK_LINK]=AT_FAIL;
        snprintf(result->note,sizeof(result->note),"ABORTED_LINK_DOWN/IP_CHANGE/OTA");return;
    }
    progress("Checking DNS...");
    if (network_changed(result->network.revision)) {
        result->checks[AT_CHECK_LINK]=AT_FAIL;
        snprintf(result->note,sizeof(result->note),"ABORTED_LINK_DOWN/IP_CHANGE/OTA");return;
    }
    bool dns_configured = result->network.dns[0].addr || result->network.dns[1].addr;
    result->checks[AT_CHECK_DNS] = AT_DNS(dns_configured);
    if (dns_configured && CONFIG_SYNDETIS_AUTOTEST_DNS_HOSTNAME[0]) {
        progress("Resolving local name");
        snprintf(result->dns_hostname, sizeof(result->dns_hostname), "%.63s", CONFIG_SYNDETIS_AUTOTEST_DNS_HOSTNAME);
        result->checks[AT_CHECK_RESOLUTION] = optional_resolution(result->network.revision);
    }
    if (network_changed(result->network.revision)) {
        result->checks[AT_CHECK_LINK] = AT_FAIL;
        snprintf(result->note, sizeof(result->note), "ABORTED_LINK_DOWN/IP_CHANGE/OTA");
    }
}

static void autotest_task(void *arg)
{
    for (;;) {
        /* Idle request wait only: owns no locks/reservation/resources and
         * cannot block UI/events. Every active diagnostic wait is bounded. */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        autotest_result_t result = {0};
        execute(&result);
        result.overall = AT_PASS;
        for (unsigned i = 0; i < AT_CHECK_COUNT; ++i) result.overall = AT_MERGE(result.overall, result.checks[i]);
        result.completed = true;
        result.finished_uptime_s = esp_timer_get_time() / 1000000;
        ESP_LOGI("SYNDETIS", "AutoTest %s: %s", autotest_grade_name(result.overall), result.note);
        portENTER_CRITICAL(&lock);
        state.last = result;
        state.running = false;
        memcpy(state.progress,"Complete",sizeof("Complete"));
        portEXIT_CRITICAL(&lock);
        diagnostic_release(run_token);
        if (strstr(result.note,"ABORTED")) ESP_LOGI("AUTOTEST","abort completed; coordinator released");
    }
}

esp_err_t autotest_start(void)
{
    if (!worker) return ESP_ERR_INVALID_STATE;
    if (update_running()) return ESP_ERR_INVALID_STATE;
    network_status_t n;
    network_status_get(&n);
    uint32_t token=diagnostic_acquire();
    if (!token) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&lock);
    bool running = state.running;
    if (!running) { state.running = true; memcpy(state.progress,"Starting...",sizeof("Starting...")); }
    portEXIT_CRITICAL(&lock);
    if (running) { diagnostic_release(token); return ESP_ERR_INVALID_STATE; }
    run_token=token;
    requested_generation=n.link_generation;
    xTaskNotifyGive(worker);
    return ESP_OK;
}

esp_err_t autotest_init(void)
{
    if (worker) return ESP_ERR_INVALID_STATE;
    ping_state.done = xSemaphoreCreateBinary();
    if (!ping_state.done) return ESP_ERR_NO_MEM;
    if (xTaskCreate(autotest_task, "autotest", 6144, NULL, 2, &worker) != pdPASS) {
        vSemaphoreDelete(ping_state.done);
        ping_state.done = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
