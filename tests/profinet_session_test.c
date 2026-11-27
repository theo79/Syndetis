#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "profinet.h"
#include "dhcp_test.h"
#include "discovery.h"
#include "esp_eth.h"

static unsigned freed, passive_received, dhcp_received, sent, notifications;
static bool reserved, conflict, init_failed, send_failed, dhcp_owns;
static void (*task_entry)(void *);
static jmp_buf suspended;
int64_t test_time_us = 1000000;
static const uint8_t local[] = {2,0,0,0,0,1};
static uint32_t random_xid = 0x10203040;
static enum { WAIT, RESPONSES, LINK_LOSS, CANCEL, NETWORK_CHANGE } action;

static void count_free(void *buffer)
{
    ++freed;
    free(buffer);
}

#define free count_free
#include "receive_dispatch.h"
#undef free

void discovery_receive(const uint8_t *buffer, size_t length)
{
    assert(buffer && length == 60);
    ++passive_received;
}

bool dhcp_test_receive(uint8_t *buffer, uint32_t length)
{
    assert(length == 60 && buffer[12] == 0x88 && buffer[15] == 0xff);
    ++dhcp_received;
    memset(buffer, 0, length); // The discovery worker must have its own copy.
    if (dhcp_owns) count_free(buffer);
    return dhcp_owns;
}

uint32_t esp_random(void) { return random_xid; }

bool wired_profinet_reserve(void)
{
    if (reserved || conflict) return false;
    return reserved = true;
}

void wired_profinet_release(void)
{
    assert(reserved);
    reserved = false;
}

bool wired_profinet_valid(void) { return reserved && !conflict; }

int xTaskCreate(void (*f)(void *), const char *name, int stack, void *arg, int priority, void *handle)
{
    (void)name; (void)stack; (void)arg; (void)priority;
    if (init_failed) return 0;
    task_entry = f;
    *(void **)handle = &task_entry;
    return 1;
}

void xTaskNotifyGive(void *handle)
{
    assert(handle == &task_entry);
    ++notifications;
}

unsigned ulTaskNotifyTake(int clear, unsigned ticks)
{
    (void)clear; (void)ticks;
    if (!notifications) longjmp(suspended, 1);
    return notifications--;
}

static void reply(unsigned source, bool malformed)
{
    uint8_t *frame = calloc(1, 60);
    assert(frame);
    const uint8_t header[] = {
        2,0,0,0,0,1, 2,0,0,0,0,0, 0x88,0x92,0xfe,0xff,5,1,
        0,0,0,0,0,0,0,8, 2,2,0,3,0,0,'a',0
    };
    memcpy(frame, header, sizeof(header));
    frame[11] = source;
    uint32_t xid = profinet_summary().xid;
    for (unsigned i = 0; i < 4; ++i) frame[18 + i] = xid >> (24 - 8 * i);
    if (malformed) frame[29] = 100;
    ethernet_dispatch(frame, 60);
}

void vTaskDelay(int ms)
{
    test_time_us += (int64_t)ms * 1000;
    if (action == LINK_LOSS) { profinet_link(false); profinet_link(true); }
    if (action == CANCEL) profinet_cancel();
    if (action == NETWORK_CHANGE) conflict = true;
    if (action == RESPONSES) {
        reply(2, true);
        for (unsigned i = 2; i < 14; ++i) {
            dhcp_owns = i % 2 == 0;
            reply(i, false);
        }
    }
    action = WAIT;
}

esp_err_t esp_eth_transmit(void *driver, void *frame, size_t size)
{
    assert(driver == &task_entry && size == DCP_REQUEST_SIZE && reserved);
    uint8_t expected[DCP_REQUEST_SIZE];
    dcp_identify_all(expected, local, profinet_summary().xid);
    assert(!memcmp(expected, frame, size));
    ++sent;
    return send_failed ? ESP_FAIL : ESP_OK;
}

static void run_worker(void)
{
    if (!setjmp(suspended)) task_entry(NULL);
    assert(!reserved);
}

int main(void)
{
    init_failed = true;
    profinet_init(&task_entry, local);
    assert(profinet_summary().state == DCP_UNAVAILABLE && profinet_summary().error_code);
    assert(profinet_start() && !notifications && !reserved);
    init_failed = false;
    profinet_init(&task_entry, local);
    assert(profinet_start());
    profinet_link(true);
    reply(2, false);
    run_worker();
    assert(!sent && !profinet_summary().count);
    conflict = true;
    assert(profinet_start());
    conflict = false;

    assert(!profinet_start() && profinet_start());
    profinet_cancel();
    run_worker();
    assert(!sent && profinet_summary().state == DCP_CANCELLED && profinet_start());
    test_time_us += DCP_COOLDOWN_MS * 1000;

    assert(!profinet_start());
    action = RESPONSES;
    run_worker();
    dcp_summary_t scan = profinet_summary();
    assert(sent == 1 && scan.state == DCP_COMPLETE && scan.valid && scan.count == 7);
    assert(scan.finished_ms - scan.started_ms == DCP_WINDOW_MS);
    assert(scan.malformed == 1 && scan.queue_drops == 5 && scan.duplicate_name && dcp_partial(&scan));
    assert(freed == 14 && passive_received == freed && dhcp_received == freed);
    dcp_device_t device;
    assert(profinet_device(scan.xid, 0, &device) && !strcmp(device.name, "a"));
    reply(20, false);
    run_worker();
    assert(sent == 1 && profinet_summary().count == 7);

    profinet_link(false);
    profinet_link(true);
    assert(!profinet_summary().valid && profinet_summary().count == 7);
    test_time_us += DCP_COOLDOWN_MS * 1000;
    assert(!profinet_start() && profinet_summary().xid != scan.xid);
    action = LINK_LOSS;
    run_worker();
    assert(sent == 2 && profinet_summary().state == DCP_LINK_LOST && !profinet_summary().valid);
    test_time_us += DCP_COOLDOWN_MS * 1000;
    assert(!profinet_start());
    action = CANCEL;
    run_worker();
    assert(sent == 3 && profinet_summary().state == DCP_CANCELLED);
    test_time_us += DCP_COOLDOWN_MS * 1000;
    assert(!profinet_start());
    action = NETWORK_CHANGE;
    run_worker();
    assert(sent == 4 && profinet_summary().state == DCP_NETWORK_CHANGED);
    conflict = false;
    test_time_us += DCP_COOLDOWN_MS * 1000;
    assert(!profinet_start());
    send_failed = true;
    run_worker();
    assert(sent == 5 && profinet_summary().state == DCP_TX_FAILED && profinet_summary().error_code == ESP_FAIL);
    puts("PASS: PROFINET one-shot runtime, timeout, cancel, link changes, TX failure and RX ownership");
}
