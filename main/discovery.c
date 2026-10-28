#include "discovery.h"
#include <stdlib.h>
#include <string.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

typedef struct {
    uint32_t session;
    uint64_t received_ms;
    size_t length;
    uint8_t bytes[NEIGHBOR_FRAME_MAX];
} queued_frame_t;

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static neighbor_table_t table;
static QueueHandle_t frames;
static bool available, worker_started;
static uint64_t clock_ms(void) { return esp_timer_get_time() / 1000; }

static void parse_worker(void *arg)
{
    (void)arg;
    queued_frame_t frame;
    neighbor_t record;
    portENTER_CRITICAL(&lock);worker_started=true;portEXIT_CRITICAL(&lock);
    for (;;) {
        if (xQueueReceive(frames, &frame, portMAX_DELAY) != pdTRUE) continue;
        parse_result_t result = neighbor_parse(frame.bytes, frame.length, &record);
        uint64_t now = clock_ms();
        portENTER_CRITICAL(&lock);
        // Recheck AFTER parsing: disconnect/reconnect may have happened meanwhile.
        if (table.link_up && frame.session == table.session) {
            if (result == PARSE_VALID)
                neighbors_accept(&table, &record, frame.session, frame.received_ms, now);
            else if (result == PARSE_INVALID) ++table.invalid_frames;
            else if (result == PARSE_LIMIT) ++table.limited_frames;
        }
        portEXIT_CRITICAL(&lock);
    }
}

esp_err_t discovery_init(void)
{
    frames = xQueueCreate(4, sizeof(queued_frame_t)); // One bounded allocation, no per-frame allocation.
    if (!frames) return ESP_ERR_NO_MEM;
    if (xTaskCreate(parse_worker, "discovery", 8192, NULL, 3, NULL) != pdPASS) {
        vQueueDelete(frames); frames = NULL;
        return ESP_ERR_NO_MEM;
    }
    portENTER_CRITICAL(&lock);
    available = true;
    portEXIT_CRITICAL(&lock);
    return ESP_OK;
}

void discovery_link(bool up)
{
    uint64_t now = clock_ms();
    portENTER_CRITICAL(&lock);
    neighbors_link(&table, up, now);
    portEXIT_CRITICAL(&lock);
}

void discovery_receive(const uint8_t *buffer, size_t length)
{
    if (frames && neighbor_destination(buffer, length)) {
        // MAC task stack stays small: this fixed scratch belongs only to its RX callback.
        static queued_frame_t frame;
        portENTER_CRITICAL(&lock);
        bool accept = table.link_up;
        frame.session = table.session;
        if (accept && length > NEIGHBOR_FRAME_MAX) ++table.limited_frames;
        portEXIT_CRITICAL(&lock);
        if (accept && length <= NEIGHBOR_FRAME_MAX) {
            frame.received_ms = clock_ms(); frame.length = length;
            memcpy(frame.bytes, buffer, length);
            if (xQueueSend(frames, &frame, 0) != pdTRUE) {
                portENTER_CRITICAL(&lock);
                if (table.link_up && table.session == frame.session) ++table.queue_drops;
                portEXIT_CRITICAL(&lock);
            }
        }
    }
}

discovery_summary_t discovery_summary(void)
{
    discovery_summary_t summary = {0};
    uint64_t now = clock_ms();
    portENTER_CRITICAL(&lock);
    summary.available = available;
    summary.state = neighbors_state(&table, now);
    summary.capacity_drops = table.capacity_drops;
    summary.invalid_frames = table.invalid_frames;
    summary.limited_frames = table.limited_frames;
    summary.queue_drops = table.queue_drops;
    for (int i = 0; i < NEIGHBOR_CAPACITY; ++i) {
        summary.used[i] = table.records[i].used;
        summary.protocols[i] = table.records[i].protocol;
        summary.expired[i] = neighbor_expired(&table.records[i], now);
    }
    portEXIT_CRITICAL(&lock);
    return summary;
}

bool discovery_record(int index, neighbor_t *out)
{
    if (index < 0 || index >= NEIGHBOR_CAPACITY) return false;
    portENTER_CRITICAL(&lock);
    bool exists = table.link_up && table.records[index].used;
    if (exists) *out = table.records[index];
    portEXIT_CRITICAL(&lock);
    return exists;
}

bool discovery_snapshot(neighbor_table_t *out)
{
    portENTER_CRITICAL(&lock);
    *out = table;
    bool ready = available;
    portEXIT_CRITICAL(&lock);
    return ready;
}

bool discovery_worker_started(void)
{
    portENTER_CRITICAL(&lock);bool ready=available && worker_started;portEXIT_CRITICAL(&lock);return ready;
}
