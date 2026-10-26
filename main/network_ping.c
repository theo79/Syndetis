#include "network_ping.h"
#include "dhcp_test.h"
#include "ping_target.h"
#include "ping_packet.h"
#include <errno.h>
#include <string.h>
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "lwip/sockets.h"
#include "lwip/def.h"

static portMUX_TYPE ping_lock = portMUX_INITIALIZER_UNLOCKED;
static network_ping_status_t ping_state;
static bool ping_cancelled, ping_new;
// All socket/protocol fields below are owned by the existing wired worker.
static int ping_fd = -1;
static bool ping_waiting, ping_finishing;
static const char *ping_finish_detail;
static int ping_finish_error;
static uint64_t ping_token, ping_sent_at, ping_next_send;
static uint16_t ping_identifier;
static uint32_t ping_serial;
static wired_ping_binding_t ping_binding;
static struct sockaddr_in ping_destination;

static uint64_t ping_now(void) { return (uint64_t)esp_timer_get_time(); }

network_ping_status_t network_ping_status(void)
{
    portENTER_CRITICAL(&ping_lock);
    network_ping_status_t result = ping_state;
    portEXIT_CRITICAL(&ping_lock);
    dhcp_status_t owner = dhcp_test_status();
    if (result.valid && (result.generation != owner.generation || !owner.active)) {
        result.valid = false;
        result.replies = 0; result.rtt_sum_us = 0;
        result.detail = "Network changed";
    }
    return result;
}

const char *network_ping_start(bool gateway, uint32_t custom)
{
    portENTER_CRITICAL(&ping_lock);
    if (ping_state.active) { portEXIT_CRITICAL(&ping_lock); return "Ping busy / stopping"; }
    dhcp_status_t owner;
    if (!wired_ping_reserve(&owner)) { portEXIT_CRITICAL(&ping_lock); return "Need usable wired IP"; }
    uint32_t target = gateway ? ping_ipv4(owner.gateway) : custom;
    const char *error = gateway && !target ? "No gateway set" :
        ping_target_validate(target, ping_ipv4(owner.ip), ping_ipv4(owner.mask), ping_ipv4(owner.gateway));
    if (error) { wired_ping_release(); portEXIT_CRITICAL(&ping_lock); return error; }
    ping_state = (network_ping_status_t){.active = true, .valid = true, .target = target,
        .generation = owner.generation, .detail = "Starting Ping"};
    ping_cancelled = false; ping_new = true;
    portEXIT_CRITICAL(&ping_lock);
    return NULL;
}

void network_ping_cancel(void)
{
    portENTER_CRITICAL(&ping_lock);
    if (ping_state.active) {
        ping_cancelled = true;
        wired_ping_expect(0, 0, 0, 0, 0); // Suppress queued TX immediately, before the worker retires the socket.
    }
    portEXIT_CRITICAL(&ping_lock);
}

static bool ping_still_current(unsigned generation)
{
    portENTER_CRITICAL(&ping_lock);
    bool current = !ping_cancelled;
    portEXIT_CRITICAL(&ping_lock);
    return current && wired_ping_valid(generation);
}

static void ping_finish(const char *detail, int error)
{
    if (!ping_finishing) { ping_finish_detail = detail; ping_finish_error = error; ping_finishing = true; }
    wired_ping_expect(0, 0, 0, 0, 0);
    if (ping_fd >= 0) {
        if (close(ping_fd) < 0 && errno != EBADF) {
            portENTER_CRITICAL(&ping_lock);
            ping_state.detail = "Socket cleanup retry"; ping_state.error = errno;
            portEXIT_CRITICAL(&ping_lock);
            return; // Retain the one descriptor/reservation; retry on the next worker tick.
        }
        ping_fd = -1;
    }
    wired_ping_release();
    portENTER_CRITICAL(&ping_lock);
    ping_state.detail = ping_finish_detail; ping_state.error = ping_finish_error;
    if (!strcmp(ping_finish_detail, "Network changed") || !strcmp(ping_finish_detail, "Cancelled / changed")) {
        ping_state.valid = false; ping_state.replies = 0; ping_state.rtt_sum_us = 0;
    }
    ping_state.active = false;
    portEXIT_CRITICAL(&ping_lock);
    ping_waiting = ping_finishing = false;
}

void network_ping_step(void)
{
    portENTER_CRITICAL(&ping_lock);
    network_ping_status_t s = ping_state;
    bool fresh = ping_new, cancel = ping_cancelled;
    ping_new = false;
    portEXIT_CRITICAL(&ping_lock);
    if (!s.active) return;
    if (ping_finishing) { ping_finish(ping_finish_detail, ping_finish_error); return; }
    if (cancel) { ping_finish("Cancelled", 0); return; }
    wired_ping_binding_t current;
    if (!wired_ping_binding(s.generation, &current)) { ping_finish("Network changed", 0); return; }
    if (fresh) {
        ping_binding = current;
        ping_waiting = false;
        ping_token = (uint64_t)(esp_random() ^ ++ping_serial) << 32 | esp_random();
        ping_identifier = (uint16_t)(ping_token >> 32);
        ping_fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        if (ping_fd < 0) { ping_finish("Socket create error", errno); return; }
        struct ifreq device = {0};
        if (strlen(current.name) >= sizeof(device.ifr_name)) { ping_finish("Interface name error", 0); return; }
        memcpy(device.ifr_name, current.name, strlen(current.name) + 1);
        struct sockaddr_in local = {.sin_family = AF_INET};
        local.sin_addr.s_addr = lwip_htonl(current.ip);
        // Both are required; failure never falls back to default/AP routing.
        if (setsockopt(ping_fd, SOL_SOCKET, SO_BINDTODEVICE, &device, sizeof(device)) < 0 ||
            bind(ping_fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
            ping_finish("Ethernet bind error", errno); return;
        }
        ping_destination = (struct sockaddr_in){.sin_family = AF_INET};
        ping_destination.sin_addr.s_addr = lwip_htonl(s.target);
        ping_next_send = ping_now();
    } else if (current.ip != ping_binding.ip || current.mask != ping_binding.mask ||
               current.gateway != ping_binding.gateway || strcmp(current.name, ping_binding.name)) {
        ping_finish("Network changed", 0); return;
    }
    if (!ping_still_current(s.generation)) { ping_finish("Cancelled / changed", 0); return; }
    uint64_t now = ping_now();
    if (ping_waiting) {
        // Bounded draining under unrelated raw traffic; every receive is nonblocking.
        for (unsigned i = 0; i < 8; ++i) {
            if (!ping_still_current(s.generation)) { ping_finish("Cancelled / changed", 0); return; }
            uint8_t packet[128]; struct sockaddr_in peer = {0}; socklen_t size = sizeof(peer);
            int n = recvfrom(ping_fd, packet, sizeof(packet), MSG_DONTWAIT, (struct sockaddr *)&peer, &size);
            if (n < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) { ping_finish("Receive error", errno); return; }
                break;
            }
            if (peer.sin_family != AF_INET || peer.sin_addr.s_addr != ping_destination.sin_addr.s_addr ||
                !ping_packet_match(packet, (size_t)n, s.target, current.ip, ping_identifier,
                                   (uint16_t)s.sent, ping_token, true)) continue;
            uint64_t arrived = wired_ping_reply_time(s.generation);
            if (!arrived || arrived < ping_sent_at || arrived - ping_sent_at >= 1000000) continue;
            if (!ping_still_current(s.generation)) break;
            portENTER_CRITICAL(&ping_lock);
            if (!ping_cancelled) { ++ping_state.replies; ping_state.rtt_sum_us += arrived - ping_sent_at; }
            portEXIT_CRITICAL(&ping_lock);
            ping_waiting = false; break;
        }
        if (!ping_waiting || ping_now() - ping_sent_at >= 1000000) {
            ping_waiting = false; ++s.completed;
            wired_ping_expect(0, 0, 0, 0, 0);
            portENTER_CRITICAL(&ping_lock);
            ping_state.completed = s.completed;
            portEXIT_CRITICAL(&ping_lock);
            if (s.completed == 4) {
                portENTER_CRITICAL(&ping_lock);
                unsigned replies = ping_state.replies;
                portEXIT_CRITICAL(&ping_lock);
                ping_finish(replies ? "Ping complete" : "NO REPLY", 0); return;
            }
        }
    }
    if (!ping_waiting && s.sent < 4 && now >= ping_next_send) {
        if (!ping_still_current(s.generation)) { ping_finish("Cancelled / changed", 0); return; }
        uint8_t packet[PING_PACKET_BYTES];
        ping_packet_make(packet, ping_identifier, (uint16_t)(s.sent + 1), ping_token);
        wired_ping_expect(s.generation, s.target, ping_identifier, (uint16_t)(s.sent + 1), ping_token);
        ping_sent_at = ping_now();
        int n = sendto(ping_fd, packet, sizeof(packet), MSG_DONTWAIT,
                       (struct sockaddr *)&ping_destination, sizeof(ping_destination));
        if (n != sizeof(packet)) { ping_finish("Send error", n < 0 ? errno : 0); return; }
        ping_waiting = true; ping_next_send = ping_sent_at + 1000000;
        portENTER_CRITICAL(&ping_lock);
        ++ping_state.sent; ping_state.detail = "Pinging / Hold Back";
        portEXIT_CRITICAL(&ping_lock);
    }
}
