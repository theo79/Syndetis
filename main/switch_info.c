#include "switch_info.h"
#include "switch_info_select.h"
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "network_status.h"

#if CONFIG_ESP_NETIF_L2_TAP
#error "Switch Info RX replacement requires L2 TAP disabled; review routing before combining"
#endif

typedef struct {
    uint8_t frame[SWITCH_INFO_FRAME_SIZE];
    uint32_t length, generation, epoch;
    int64_t received_us;
    bool link_up, used;
    switch_protocol_t protocol;
} capture_slot_t;
typedef struct {
    esp_netif_t *netif;
    esp_eth_handle_t driver;
    capture_slot_t slots[SWITCH_INFO_CAPTURE_SLOTS];
    QueueHandle_t queue;
    TaskHandle_t worker;
    uint32_t epoch;
    bool attempted;
    switch_info_observations_t observations;
    switch_info_status_t status;
} capture_context_t;
static capture_context_t context;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static const char *TAG = "Switch Info";
static void increment(uint32_t *counter) { if (*counter != UINT32_MAX) ++*counter; }

/* Caller holds our lock. network_status never takes our lock, so this nesting
 * has no reverse acquisition. Its getter only copies a bounded lifecycle state. */
static void invalidate_locked(int64_t now)
{
    network_status_t network;
    network_status_get(&network);
    unsigned expired = switch_info_expire(&context.observations, network.link_up, network.link_generation, now);
    for (unsigned i = 0; i < expired; ++i) increment(&context.status.counters.expired);
    const switch_neighbor_t *selected = switch_info_selected(&context.observations);
    if (selected) context.status.neighbor = *selected;
    else memset(&context.status.neighbor, 0, sizeof(context.status.neighbor));
}

void switch_info_get(switch_info_status_t *out)
{
    if (!out) return;
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    invalidate_locked(now);
    *out = context.status;
    portEXIT_CRITICAL(&lock);
    out->free_heap = esp_get_free_heap_size();
    out->minimum_free_heap = esp_get_minimum_free_heap_size();
}

static void on_eth(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;
    if (!data || *(esp_eth_handle_t *)data != context.driver) return;
    if (id == ETHERNET_EVENT_CONNECTED || id == ETHERNET_EVENT_DISCONNECTED || id == ETHERNET_EVENT_STOP) {
        portENTER_CRITICAL(&lock);
        ++context.epoch; /* Private capture epoch; never changes network lifecycle. */
        memset(&context.observations, 0, sizeof(context.observations));
        memset(&context.status.neighbor, 0, sizeof(context.status.neighbor));
        portEXIT_CRITICAL(&lock);
    }
}

bool switch_info_capture(uint8_t *buffer, uint32_t length)
{
    capture_context_t *c = &context;
    if (!c->status.available) return false;
    switch_protocol_t protocol = switch_info_is_lldp(buffer, length) ? SWITCH_PROTOCOL_LLDP :
                                 switch_info_is_cdp(buffer, length) ? SWITCH_PROTOCOL_CDP : SWITCH_PROTOCOL_NONE;
    if (protocol == SWITCH_PROTOCOL_NONE) return false;

    uint8_t index = SWITCH_INFO_CAPTURE_SLOTS;
    network_status_t network;
    network_status_get(&network);
    int64_t received = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    increment(&c->status.counters.candidates);
    if (protocol == SWITCH_PROTOCOL_CDP) increment(&c->status.counters.cdp_candidates);
    if (length > SWITCH_INFO_FRAME_SIZE) increment(&c->status.counters.oversize);
    else {
        for (uint8_t i = 0; i < SWITCH_INFO_CAPTURE_SLOTS; ++i) if (!c->slots[i].used) {
            index = i; c->slots[i].used = true;
            c->slots[i].epoch = c->epoch;
            break;
        }
        if (index == SWITCH_INFO_CAPTURE_SLOTS) increment(&c->status.counters.pool_full);
    }
    portEXIT_CRITICAL(&lock);
    if (index != SWITCH_INFO_CAPTURE_SLOTS) {
        capture_slot_t *slot = &c->slots[index];
        memcpy(slot->frame, buffer, length);
        slot->length = length; slot->generation = network.link_generation;
        slot->link_up = network.link_up; slot->received_us = received;
        slot->protocol = protocol;
        bool queued = xQueueSend(c->queue, &index, 0) == pdTRUE;
        portENTER_CRITICAL(&lock);
        if (queued) increment(&c->status.counters.queued);
        else { increment(&c->status.counters.queue_full); slot->used = false; }
        portEXIT_CRITICAL(&lock);
    }
    /* Only consumed candidates reach here. Stack input owns all other frames,
     * even on errors. The EMAC task does not free callback errors. */
    free(buffer);
    return true;
}

static bool slot_current_locked(const capture_slot_t *slot)
{
    network_status_t network;
    network_status_get(&network);
    return switch_info_capture_current(slot->link_up, slot->generation, slot->epoch,
                                       network.link_up, network.link_generation, context.epoch);
}

static void worker(void *arg)
{
    (void)arg;
    /* Stay out of shared state until setup commits. Failure cleanup can delete
     * a waiting worker without racing its state lock or queue access. */
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    /* Permanent worker-only scratch keeps the 4 KiB stack bounded. */
    static switch_neighbor_t parsed;
    int64_t next_log[2] = {0};
    int64_t next_stats = 10000000;
    uint32_t last_expired = 0, last_candidates = 0;
    for (;;) {
        uint8_t index;
        bool have = xQueueReceive(context.queue, &index, pdMS_TO_TICKS(100)) == pdTRUE;
        int64_t now = esp_timer_get_time();
        portENTER_CRITICAL(&lock);
        invalidate_locked(now);
        bool expired = context.status.counters.expired != last_expired;
        last_expired = context.status.counters.expired;
        portEXIT_CRITICAL(&lock);
        if (expired) ESP_LOGI(TAG, "neighbor expired");
        if (have) {
            capture_slot_t *slot = &context.slots[index];
            uint32_t frame_length = slot->length;
            portENTER_CRITICAL(&lock);
            bool current = slot_current_locked(slot);
            portEXIT_CRITICAL(&lock);
            bool cdp = slot->protocol == SWITCH_PROTOCOL_CDP;
            const char *packet_tag = cdp ? "CDP" : "LLDP";
            bool ok = current && (cdp ? switch_info_parse_cdp(slot->frame, slot->length, &parsed) :
                                       switch_info_parse_lldp(slot->frame, slot->length, &parsed));
            bool updated = false, withdrew = false;
            now = esp_timer_get_time();
            portENTER_CRITICAL(&lock);
            /* Recheck after parsing, with publication serialized against events. */
            if (!current || !slot_current_locked(slot)) increment(&context.status.counters.stale_generation);
            else if (!ok) {
                increment(&context.status.counters.malformed);
                if (cdp) increment(&context.status.counters.cdp_malformed);
            }
            else {
                increment(&context.status.counters.parsed);
                if (cdp) increment(&context.status.counters.cdp_parsed);
                parsed.receive_timestamp_us = slot->received_us;
                parsed.expiration_us = slot->received_us + (int64_t)parsed.ttl * 1000000;
                parsed.link_generation = slot->generation;
                bool changed = switch_info_observe(&context.observations, &parsed, now);
                withdrew = changed && !parsed.ttl;
                updated = changed && parsed.ttl;
                invalidate_locked(now); /* Re-select: fresh LLDP always wins. */
            }
            slot->used = false;
            portEXIT_CRITICAL(&lock);
            /* At most one update/withdrawal log group per five seconds. Never RX logging. */
            unsigned log_index = cdp ? 1 : 0;
            if ((updated || withdrew) && now >= next_log[log_index]) {
                next_log[log_index] = now + 5000000;
                ESP_LOGI(packet_tag, "frame received len=%" PRIu32, frame_length);
                if (withdrew) ESP_LOGI(packet_tag, "neighbor withdrawn");
                else {
                    ESP_LOGI(packet_tag, "neighbor updated %s=%s port=%s ttl=%u", cdp ? "device" : "chassis",
                             cdp ? parsed.device_id : parsed.chassis_id, parsed.port_id, parsed.ttl);
                    if (parsed.system_name[0]) ESP_LOGI(packet_tag, "system=%s", parsed.system_name);
                    if (parsed.platform[0]) ESP_LOGI(packet_tag, "platform=%s", parsed.platform);
                    if (parsed.management_ipv4_present) ESP_LOGI(packet_tag, "mgmt=%u.%u.%u.%u",
                        parsed.management_ipv4[0], parsed.management_ipv4[1], parsed.management_ipv4[2], parsed.management_ipv4[3]);
                    if (parsed.pvid_present) ESP_LOGI(packet_tag, "%s=%u", cdp ? "native_vlan" : "pvid", parsed.pvid);
                }
            }
        }
        uint32_t watermark = (uint32_t)uxTaskGetStackHighWaterMark(NULL); /* IDF reports bytes. */
        portENTER_CRITICAL(&lock);
        context.status.worker_stack_free_bytes = watermark;
        portEXIT_CRITICAL(&lock);
        if (now >= next_stats) {
            switch_info_counters_t counters;
            portENTER_CRITICAL(&lock);
            counters = context.status.counters;
            portEXIT_CRITICAL(&lock);
            if (counters.candidates != last_candidates) {
                ESP_LOGI(TAG, "counters candidate=%" PRIu32 " queued=%" PRIu32 " parsed=%" PRIu32
                    " malformed=%" PRIu32 " oversize=%" PRIu32 " pool_full=%" PRIu32
                    " queue_full=%" PRIu32 " stale=%" PRIu32 " expired=%" PRIu32
                    " cdp_candidate=%" PRIu32 " cdp_parsed=%" PRIu32 " cdp_malformed=%" PRIu32
                    " heap=%" PRIu32 " min_heap=%" PRIu32 " stack_free=%" PRIu32,
                    counters.candidates, counters.queued, counters.parsed, counters.malformed,
                    counters.oversize, counters.pool_full, counters.queue_full,
                    counters.stale_generation, counters.expired, counters.cdp_candidates, counters.cdp_parsed,
                    counters.cdp_malformed, (uint32_t)esp_get_free_heap_size(),
                    (uint32_t)esp_get_minimum_free_heap_size(), watermark);
                last_candidates = counters.candidates;
            }
            next_stats = now + 10000000;
        }
        /* Bound continuous flood CPU consumption; sacrifice discovery if needed. */
        if (have) vTaskDelay(1);
    }
}

esp_err_t switch_info_init(esp_eth_handle_t driver, esp_netif_t *netif)
{
    if (context.attempted || !driver || !netif) return ESP_ERR_INVALID_STATE;
    context.attempted = true;
    context.driver = driver; context.netif = netif;
    esp_err_t err = ESP_ERR_NO_MEM;
    bool registered = false, filter_added = false, cdp_filter_added = false;
    uint8_t destination[6] = {1, 0x80, 0xc2, 0, 0, 0x0e};
    uint8_t cdp_destination[6] = {1, 0, 0x0c, 0xcc, 0xcc, 0xcc};
    esp_err_t cdp_error = ESP_ERR_INVALID_STATE;
    context.queue = xQueueCreate(SWITCH_INFO_QUEUE_DEPTH, sizeof(uint8_t));
    if (!context.queue) goto failed;
    if (xTaskCreate(worker, "switch_info", SWITCH_INFO_WORKER_STACK, NULL, SWITCH_INFO_WORKER_PRIORITY, &context.worker) != pdPASS)
        goto failed;
    err = esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, on_eth, NULL);
    if (err != ESP_OK) goto failed;
    registered = true;
    err = esp_eth_ioctl(driver, ETH_CMD_ADD_MAC_FILTER, destination);
    if (err != ESP_OK) {
        portENTER_CRITICAL(&lock);
        increment(&context.status.counters.filter_failures);
        portEXIT_CRITICAL(&lock);
        ESP_LOGW("LLDP", "multicast filter installation failed: %s", esp_err_to_name(err));
        goto failed;
    }
    filter_added = true;
    ESP_LOGI("LLDP", "multicast filter installed: 01:80:C2:00:00:0E (%s)", esp_err_to_name(err));
    /* Last optional operation, before Ethernet start. Never remove CDP filters
     * by address: that API could remove another owner's duplicate first. */
    cdp_error = esp_eth_ioctl(driver, ETH_CMD_ADD_MAC_FILTER, cdp_destination);
    cdp_filter_added = cdp_error == ESP_OK;
    if (!cdp_filter_added) {
        portENTER_CRITICAL(&lock);
        increment(&context.status.counters.filter_failures);
        portEXIT_CRITICAL(&lock);
        ESP_LOGW("CDP", "multicast filter installation failed: %s; LLDP retained", esp_err_to_name(cdp_error));
    } else ESP_LOGI("CDP", "multicast filter installed: 01:00:0C:CC:CC:CC");
    portENTER_CRITICAL(&lock);
    context.status.available = true;
    context.status.cdp_available = cdp_filter_added;
    context.status.cdp_setup_error = cdp_error;
    portEXIT_CRITICAL(&lock);
    xTaskNotifyGive(context.worker);
    return ESP_OK;
failed:
    /* No callback has been installed on these paths. Ethernet has not started. */
    if (filter_added) {
        uint8_t destination[6] = {1, 0x80, 0xc2, 0, 0, 0x0e};
        esp_err_t cleanup = esp_eth_ioctl(driver, ETH_CMD_DEL_MAC_FILTER, destination);
        if (cleanup != ESP_OK) ESP_LOGW(TAG, "filter cleanup failed: %s", esp_err_to_name(cleanup));
    }
    if (registered) esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, on_eth);
    if (context.worker) { vTaskDelete(context.worker); context.worker = NULL; }
    if (context.queue) { vQueueDelete(context.queue); context.queue = NULL; }
    portENTER_CRITICAL(&lock);
    context.status.setup_error = err;
    context.status.cdp_setup_error = err;
    portEXIT_CRITICAL(&lock);
    ESP_LOGW(TAG, "reception unavailable: %s; normal Ethernet retained", esp_err_to_name(err));
    return err;
}
