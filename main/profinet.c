#include "profinet.h"
#include "dhcp_test.h"
#include "esp_eth.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

enum { RX_CAPACITY = 8 };
typedef struct {
    size_t length;
    uint8_t bytes[DCP_FRAME_LIMIT];
} received_frame_t;

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static dcp_inventory_t inventory;
static received_frame_t *queue;
static unsigned tail, queued;
static uint8_t local_mac[6];
static esp_eth_handle_t ethernet;
static TaskHandle_t task;
static bool link_up;

static uint64_t now_ms(void) { return esp_timer_get_time() / 1000; }

void profinet_receive(const uint8_t *frame, size_t length)
{
    portENTER_CRITICAL(&lock);
    dcp_summary_t *s = &inventory.summary;
    if (s->state == DCP_SCANNING && now_ms() < s->deadline_ms &&
        dcp_matches(frame, length, local_mac, s->xid)) {
        if (length > DCP_FRAME_LIMIT) ++s->malformed;
        else if (queued == RX_CAPACITY) ++s->queue_drops;
        else {
            received_frame_t *item = &queue[(tail + queued) % RX_CAPACITY];
            item->length = length;
            memcpy(item->bytes, frame, length);
            ++queued;
        }
    }
    portEXIT_CRITICAL(&lock);
}

static void worker(void *arg)
{
    (void)arg;
    received_frame_t frame;
    dcp_summary_t *s = &inventory.summary;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        bool network_valid = wired_profinet_valid();
        uint8_t request[DCP_REQUEST_SIZE];
        portENTER_CRITICAL(&lock);
        uint32_t xid = s->xid;
        if (!network_valid) dcp_finish(s, DCP_NETWORK_CHANGED, now_ms());
        bool send = s->state == DCP_STARTING;
        if (send) {
            dcp_identify_all(request, local_mac, xid);
            s->state = DCP_SCANNING;
            s->deadline_ms = now_ms() + DCP_WINDOW_MS;
        }
        portEXIT_CRITICAL(&lock);
        if (send) {
            esp_err_t result = esp_eth_transmit(ethernet, request, sizeof(request));
            if (result != ESP_OK) {
                portENTER_CRITICAL(&lock);
                s->error_code = result;
                dcp_finish(s, DCP_TX_FAILED, now_ms());
                portEXIT_CRITICAL(&lock);
            }
        }
        for (;;) {
            network_valid = wired_profinet_valid();
            portENTER_CRITICAL(&lock);
            if (!network_valid) dcp_finish(s, DCP_NETWORK_CHANGED, now_ms());
            size_t length = 0;
            if (s->state == DCP_SCANNING) {
                if (queued) {
                    length = queue[tail].length;
                    memcpy(frame.bytes, queue[tail].bytes, length);
                    tail = (tail + 1) % RX_CAPACITY;
                    --queued;
                } else if (now_ms() >= s->deadline_ms) {
                    dcp_finish(s, DCP_COMPLETE, now_ms());
                }
            }
            bool running = dcp_active(s->state);
            portEXIT_CRITICAL(&lock);
            if (!running) break;
            if (!length) {
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            dcp_device_t device;
            dcp_parse_t parsed = dcp_parse(frame.bytes, length, local_mac, xid, &device);
            portENTER_CRITICAL(&lock);
            if (s->state == DCP_SCANNING) {
                if (parsed == DCP_PARSED) dcp_observe(&inventory, &device);
                else if (parsed == DCP_MALFORMED) ++s->malformed;
            }
            portEXIT_CRITICAL(&lock);
        }
        free(queue);
        wired_profinet_release();
    }
}

void profinet_init(void *driver, const uint8_t mac[6])
{
    ethernet = driver;
    memcpy(local_mac, mac, 6);
    if (xTaskCreate(worker, "profinet", 4096, NULL, 3, &task) != pdPASS) {
        portENTER_CRITICAL(&lock);
        inventory.summary.error_code = ESP_ERR_NO_MEM;
        portEXIT_CRITICAL(&lock);
        ESP_LOGW("profinet", "Discovery allocation failed");
        return;
    }
    portENTER_CRITICAL(&lock);
    inventory.summary = (dcp_summary_t){.state = DCP_IDLE};
    portEXIT_CRITICAL(&lock);
}

void profinet_link(bool up)
{
    portENTER_CRITICAL(&lock);
    if (link_up && !up) dcp_link_down(&inventory.summary, now_ms());
    link_up = up;
    portEXIT_CRITICAL(&lock);
}

const char *profinet_start(void)
{
    if (!wired_profinet_reserve()) return "Network busy";
    received_frame_t *frames = malloc(RX_CAPACITY * sizeof(*frames));
    uint32_t xid = esp_random();
    uint64_t now = now_ms();
    portENTER_CRITICAL(&lock);
    dcp_summary_t *s = &inventory.summary;
    const char *error = s->state == DCP_UNAVAILABLE ? "Unavailable" :
        !link_up ? "Connect Ethernet" :
        s->started_ms && now - s->finished_ms < DCP_COOLDOWN_MS ? "Wait 3s after scan" : NULL;
    if (!error) {
        if (!xid || xid == s->xid) xid = s->xid == 1 ? 2 : 1;
        dcp_begin(&inventory, xid, now);
        tail = queued = 0;
        queue = frames;
        if (!frames) {
            s->error_code = ESP_ERR_NO_MEM;
            dcp_finish(s, DCP_TX_FAILED, now);
            error = "No scan memory";
        }
    }
    portEXIT_CRITICAL(&lock);
    if (error) {
        free(frames);
        wired_profinet_release();
    }
    else xTaskNotifyGive(task);
    return error;
}

void profinet_cancel(void)
{
    portENTER_CRITICAL(&lock);
    dcp_finish(&inventory.summary, DCP_CANCELLED, now_ms());
    portEXIT_CRITICAL(&lock);
}

dcp_summary_t profinet_summary(void)
{
    portENTER_CRITICAL(&lock);
    dcp_summary_t copy = inventory.summary;
    portEXIT_CRITICAL(&lock);
    return copy;
}

bool profinet_device(uint32_t xid, unsigned index, dcp_device_t *device)
{
    portENTER_CRITICAL(&lock);
    bool found = inventory.summary.xid == xid && index < inventory.summary.count;
    if (found) *device = inventory.devices[index];
    portEXIT_CRITICAL(&lock);
    return found;
}

void profinet_snapshot(dcp_inventory_t *out)
{
    portENTER_CRITICAL(&lock);
    out->summary = inventory.summary;
    memcpy(out->devices, inventory.devices, out->summary.count * sizeof(*out->devices));
    portEXIT_CRITICAL(&lock);
}
