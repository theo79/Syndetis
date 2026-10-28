#include "dhcp_test.h"
#include "network_policy.h"
#include "network_ping.h"
#include "ping_packet.h"
#include "ping_target.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_netif_defaults.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_random.h"
#include "lwip/def.h"
#include "lwip/dns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CONFIG_LWIP_IPV6 || CONFIG_LWIP_IP_FORWARD || CONFIG_LWIP_DHCP_RESTORE_LAST_IP || CONFIG_LWIP_AUTOIP
#error "DHCP test requires IPv6, forwarding, saved leases and AutoIP disabled"
#endif

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static esp_netif_t *netif;
static esp_eth_handle_t eth_driver;
static dhcp_status_t state;
enum { CMD_NONE, CMD_START, CMD_STOP, CMD_CANCEL, CMD_MANUAL, CMD_CONFLICT };
static int pending;
static bool initialized, link_up, rx_enabled, tx_enabled;
static unsigned link_session, session, inflight;
static bool stack_started;
// The existing worker is the sole owner of both DHCP and Manual stack mutations.
static manual_ipv4_t requested_manual, manual_config;
static uint8_t ethernet_mac[6];
static unsigned address_generation, manual_conflicts;
static uint64_t last_manual_attempt, manual_deadline;
static unsigned manual_probes, manual_announcements;
static bool manual_running;
// RX-visible observation only: no stack calls, allocation or driver changes in RX.
static bool manual_watch, watch_probing;
static uint32_t watch_ip;
static unsigned watch_session;
static bool ping_reserved, ping_window;
static bool update_reserved;
static unsigned update_generation;
static uint64_t worker_heartbeat;
static uint64_t update_deadline;
static uint32_t update_local, update_mask;
static unsigned ping_generation;
static uint32_t ping_local, ping_target;
static uint16_t ping_id, ping_sequence;
static uint64_t ping_cookie;
static uint64_t ping_arrival_us;
static void clear_addresses(dhcp_status_t *s);
// This firmware has one IP client. The AP has independent DHCP-server DNS.
// Global resolver slots are borrowed only for the explicit Ethernet session.
#if CONFIG_ESP_NETIF_SET_DNS_PER_DEFAULT_NETIF
#error "Revisit DHCP DNS ownership for per-default-netif DNS"
#endif
static ip_addr_t saved_dns[DNS_MAX_SERVERS];
static bool dns_borrowed;

static esp_err_t borrow_dns(void *ctx)
{
    (void)ctx;
    for (int i = 0; i < DNS_MAX_SERVERS; ++i) {
        saved_dns[i] = *dns_getserver(i);
        dns_setserver(i, NULL); // lwIP supports clearing; esp_netif setter does not.
    }
    dns_borrowed = true;
    return ESP_OK;
}

static esp_err_t restore_dns(void *ctx)
{
    (void)ctx;
    for (int i = 0; i < DNS_MAX_SERVERS; ++i) dns_setserver(i, &saved_dns[i]);
    dns_borrowed = false;
    return ESP_OK;
}
static uint64_t clock_ms(void) { return esp_timer_get_time() / 1000; }

const char *dhcp_state_name(dhcp_state_t s)
{
    static const char *names[] = {
        "IPv4 off / passive", "Requesting address", "Address acquired", "Cancelled",
        "Stopped / released", "No lease within 30 s", "Link lost", "Address lost",
        "Management subnet overlap", "API failure", "Connect Ethernet",
        "Probing address", "Manual IP active", "ADDRESS IN USE"
    };
    return (unsigned)s < sizeof(names)/sizeof(*names) ? names[s] : "API failure";
}

static esp_err_t checked(dhcp_status_t *s, const char *api, esp_err_t err)
{
    if (err != ESP_OK) {
        ESP_LOGE("dhcp", "%s: %s (0x%x)", api, esp_err_to_name(err), (unsigned)err);
        if (!s->error) {
            s->error = err;
            snprintf(s->error_step, sizeof(s->error_step), "%s", api);
        }
    }
    return err;
}

static esp_err_t transmit(void *handle, void *buffer, size_t length)
{
    portENTER_CRITICAL(&lock);
    bool enabled = tx_enabled && link_up;
    const uint8_t *f = buffer;
    if (enabled && f && length >= 34 && f[12] == 8 && f[13] == 0 && f[23] == 1)
        enabled = ping_reserved && ping_window && ping_generation == address_generation &&
                  state.active && !state.busy && pending == CMD_NONE &&
                  ping_frame_match(f, length, ping_local, ping_target, ping_id, ping_sequence, ping_cookie, false);
    if (enabled && f && length >= 34 && f[12] == 8 && f[13] == 0 && f[23] == 6) {
        enabled=update_reserved && update_generation==address_generation &&
            clock_ms()<update_deadline && update_http_frame(f,length,update_local,update_mask,false);
    }
    portEXIT_CRITICAL(&lock);
    return enabled ? esp_eth_transmit(handle, buffer, length) : ESP_ERR_INVALID_STATE;
}

static void free_rx(void *handle, void *buffer) { (void)handle; free(buffer); }

bool dhcp_test_receive(uint8_t *buffer, uint32_t length)
{
    portENTER_CRITICAL(&lock);
    if (manual_watch && watch_session == link_session && link_up && pending == CMD_NONE &&
        manual_ipv4_conflict(buffer, length, ethernet_mac, watch_ip, watch_probing)) {
        // Close the gates and invalidate publication immediately, before stack handoff.
        manual_watch = false;
        rx_enabled = tx_enabled = false;
        clear_addresses(&state);
        state.state = MANUAL_CONFLICT;
        state.busy = true;
        pending = CMD_CONFLICT;
        ++address_generation;
    }
    portEXIT_CRITICAL(&lock);
    bool ordinary = dhcp_stack_frame(buffer, length);
    portENTER_CRITICAL(&lock);
    bool echo = ping_reserved && ping_window && ping_generation == address_generation && state.active &&
                pending == CMD_NONE && !state.busy &&
                ping_frame_match(buffer, length, ping_target, ping_local, ping_id, ping_sequence, ping_cookie, true);
    if (echo && !ping_arrival_us) ping_arrival_us = (uint64_t)esp_timer_get_time();
    // A late DHCP datagram must never reach the client in Manual mode.
    bool tls = update_reserved && update_generation == address_generation && clock_ms()<update_deadline &&
               state.active && !state.busy && update_http_frame(buffer,length,update_local,update_mask,true);
    bool accept = rx_enabled && link_up && (tls || echo || (ordinary &&
                  (!manual_watch || (buffer[12] == 8 && buffer[13] == 6))));
    if (accept) ++inflight;
    portEXIT_CRITICAL(&lock);
    if (!accept) return false;
    // Pinned ethernetif_input consumes/frees the buffer on success AND failure.
    esp_netif_receive(netif, buffer, length, NULL);
    portENTER_CRITICAL(&lock);
    --inflight;
    portEXIT_CRITICAL(&lock);
    return true;
}

static void clear_addresses(dhcp_status_t *s)
{
    s->active = false;
    s->mode = WIRED_NO_IP;
    s->prefix = 0;
    s->ip[0] = s->mask[0] = s->gateway[0] = s->dns[0][0] = s->dns[1][0] = 0;
}

static void cleanup(dhcp_status_t *s)
{
    portENTER_CRITICAL(&lock);
    rx_enabled = false;
    manual_watch = false;
    portEXIT_CRITICAL(&lock);
    manual_running = false;
    // Drain callbacks before the synchronous tcpip-thread stop. No driver restart.
    unsigned waited = 0;
    for (;;) {
        portENTER_CRITICAL(&lock);
        unsigned count = inflight;
        portEXIT_CRITICAL(&lock);
        if (!count) break;
        if (waited++ >= 20) {
            // Retain stack/DNS ownership until a later explicit Stop can drain RX.
            portENTER_CRITICAL(&lock);
            tx_enabled = false;
            portEXIT_CRITICAL(&lock);
            s->cleanup_error = true;
            s->cleanup_code = ESP_ERR_TIMEOUT;
            snprintf(s->cleanup_step, sizeof(s->cleanup_step), "RX drain timeout");
            clear_addresses(s);
            return;
        }
        vTaskDelay(1);
    }
    // Keep primary outcome/API error independent from cleanup diagnostics.
    dhcp_status_t failure = {0};
    // set_ip_info also clears global DNS in the pinned SDK. Protect resolver state
    // even for cancellation before Start, when no DHCP DNS snapshot exists yet.
    if (!dns_borrowed && checked(&failure, "esp_netif_tcpip_exec(borrow DNS)",
                                esp_netif_tcpip_exec(borrow_dns, NULL)) != ESP_OK) {
        portENTER_CRITICAL(&lock);
        tx_enabled = false;
        portEXIT_CRITICAL(&lock);
        s->cleanup_error = true;
        s->cleanup_code = failure.error;
        snprintf(s->cleanup_step, sizeof(s->cleanup_step), "%s", failure.error_step);
        clear_addresses(s);
        return; // No DNS-clearing setter may run without a recoverable snapshot.
    }
    if (stack_started) {
        // Supported public action -> esp_netif_stop: DHCP RELEASE, stop, cleanup,
        // reset IP, remove lwIP interface. TX stays enabled until release completes.
        esp_netif_action_stop(netif, NULL, 0, NULL);
        if (esp_netif_is_netif_up(netif))
            checked(&failure, "esp_netif_action_stop verification", ESP_FAIL);
        else stack_started = false;
    }
    portENTER_CRITICAL(&lock);
    tx_enabled = false;
    portEXIT_CRITICAL(&lock);
    esp_err_t err = esp_netif_dhcpc_stop(netif);
    if (err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED)
        checked(&failure, "esp_netif_dhcpc_stop", err);
    esp_netif_ip_info_t zero = {0};
    checked(&failure, "esp_netif_set_ip_info(clear)", esp_netif_set_ip_info(netif, &zero));
    checked(&failure, "esp_netif_set_old_ip_info(clear)", esp_netif_set_old_ip_info(netif, &zero));
    if (dns_borrowed)
        checked(&failure, "esp_netif_tcpip_exec(restore DNS)", esp_netif_tcpip_exec(restore_dns, NULL));
    s->cleanup_error = failure.error != ESP_OK;
    s->cleanup_code = failure.error;
    snprintf(s->cleanup_step, sizeof(s->cleanup_step), "%s", failure.error_step);
    clear_addresses(s);
}

dhcp_status_t dhcp_test_status(void)
{
    portENTER_CRITICAL(&lock);
    dhcp_status_t copy = state;
    copy.generation = address_generation;
    portEXIT_CRITICAL(&lock);
    if (copy.state == DHCP_REQUESTING) copy.duration_ms = clock_ms() - copy.started_ms;
    return copy;
}

bool dhcp_test_request(bool start)
{
    uint64_t now = clock_ms();
    portENTER_CRITICAL(&lock);
    bool ok = initialized && !ping_reserved && !update_reserved && (start ? pending == CMD_NONE && !state.busy :
                              pending != CMD_STOP && pending != CMD_CANCEL &&
                              (!state.busy || state.state == DHCP_REQUESTING));
    ok = ok && state.mode != WIRED_MANUAL && state.state != MANUAL_PROBING;
    if (start) ok = ok && !state.cleanup_error && state.state != DHCP_REQUESTING && !state.active;
    if (ok && start && !link_up) {
        state = (dhcp_status_t){.state = DHCP_CONNECT};
        ok = false;
    } else if (ok) {
        pending = start ? CMD_START : CMD_STOP;
        ++address_generation;
        if (start) {
            state = (dhcp_status_t){.state = DHCP_REQUESTING, .started_ms = now};
            session = link_session;
        } else {
            state.state = state.state == DHCP_REQUESTING ? DHCP_CANCELLED : DHCP_STOPPED;
            clear_addresses(&state);
        }
        state.busy = true;
    }
    portEXIT_CRITICAL(&lock);
    return ok;
}

bool dhcp_test_cancel_requesting(void)
{
    portENTER_CRITICAL(&lock);
    bool cancel = initialized && state.state == DHCP_REQUESTING;
    if (cancel) {
        pending = CMD_CANCEL; // Also cancels a Start still being prepared.
        ++address_generation;
        state.state = DHCP_CANCELLED;
        state.busy = true;
        clear_addresses(&state);
    }
    portEXIT_CRITICAL(&lock);
    return cancel;
}

void dhcp_test_link(bool up)
{
    portENTER_CRITICAL(&lock);
    if (link_up != up) { ++link_session; ++address_generation; }
    link_up = up;
    if (!up) {
        rx_enabled = tx_enabled = false;
        manual_watch = false;
        if (state.state == DHCP_REQUESTING || state.state == MANUAL_PROBING || state.active) {
            pending = CMD_CANCEL;
            state.state = DHCP_LINK_LOST;
            state.busy = true;
            clear_addresses(&state);
        }
    }
    portEXIT_CRITICAL(&lock);
}

const char *wired_manual_apply(const manual_ipv4_t *config, unsigned review_generation)
{
    const char *invalid = manual_ipv4_validate(config);
    if (invalid) return invalid;
    uint64_t now = clock_ms();
    portENTER_CRITICAL(&lock);
    const char *error = !initialized ? "Ethernet unavailable" : !link_up ? "Connect Ethernet" :
        update_reserved ? "Firmware update active" :
        ping_reserved ? "Ping active: cancel" :
        review_generation != address_generation ? "Changed: review again" :
        state.cleanup_error ? "Retry Stop / Release" :
        pending != CMD_NONE || state.busy || state.state == MANUAL_PROBING ? "Operation active" :
        manual_conflicts >= 10 && now - last_manual_attempt < 60000 ? "Rate limit: wait 60s" : NULL;
    if (!error) {
        requested_manual = *config;
        last_manual_attempt = now;
        session = link_session;
        pending = CMD_MANUAL;
        manual_watch = false;
        rx_enabled = false;
        ++address_generation;
        state = (dhcp_status_t){.state = MANUAL_PROBING, .busy = true};
    }
    portEXIT_CRITICAL(&lock);
    return error;
}

bool wired_manual_cancel(void)
{
    portENTER_CRITICAL(&lock);
    bool cancel = state.state == MANUAL_PROBING && pending != CMD_CANCEL;
    if (cancel) {
        pending = CMD_CANCEL;
        manual_watch = false;
        rx_enabled = false; // Retain a prior DHCP session's RELEASE path until cleanup.
        ++address_generation;
        clear_addresses(&state);
        state.state = DHCP_CANCELLED;
        state.busy = true;
    }
    portEXIT_CRITICAL(&lock);
    return cancel;
}

bool wired_use_dhcp(void)
{
    portENTER_CRITICAL(&lock);
    bool ok = initialized && !ping_reserved && !update_reserved && pending == CMD_NONE && !state.busy;
    if (ok) {
        pending = CMD_STOP;
        manual_watch = false;
        rx_enabled = false;
        ++address_generation;
        clear_addresses(&state);
        state.state = DHCP_STOPPED;
        state.busy = true;
    }
    portEXIT_CRITICAL(&lock);
    return ok;
}

static uint32_t address_value(const char *p)
{
    uint32_t result=0;
    for(unsigned i=0;i<4;++i) {
        unsigned n=0;
        while(*p>='0' && *p<='9') n=n*10+(unsigned)(*p++-'0');
        if(n>255 || (i<3 ? *p!='.' : *p!=0)) return 0;
        if(i<3)++p;
        result=(result<<8)|n;
    }
    return result;
}
bool wired_update_reserve(unsigned *generation)
{
    portENTER_CRITICAL(&lock);
    bool ok=initialized && !update_reserved && !ping_reserved && pending==CMD_NONE &&
        !state.busy && !state.cleanup_error && state.state!=DHCP_REQUESTING && state.state!=MANUAL_PROBING;
    if(ok) {
        update_reserved=true;update_generation=address_generation;*generation=address_generation;
        update_local=address_value(state.ip);update_mask=address_value(state.mask);
        update_deadline=clock_ms()+180000;
    }
    portEXIT_CRITICAL(&lock);return ok;
}
bool wired_update_valid(unsigned generation)
{
    portENTER_CRITICAL(&lock);
    bool ok=update_reserved && generation==address_generation && generation==update_generation &&
        !state.busy && !state.cleanup_error && clock_ms()<update_deadline;
    portEXIT_CRITICAL(&lock);return ok;
}
void wired_update_extend(void)
{
    portENTER_CRITICAL(&lock);if(update_reserved)update_deadline=clock_ms()+600000;portEXIT_CRITICAL(&lock);
}
void wired_update_release(void)
{
    portENTER_CRITICAL(&lock);update_reserved=false;update_local=update_mask=0;portEXIT_CRITICAL(&lock);
}

static void ip_text(char out[16], esp_ip4_addr_t ip)
{
    snprintf(out, 16, IPSTR, IP2STR(&ip));
}

static bool ping_valid_locked(unsigned generation)
{
    return initialized && ping_reserved && link_up && state.active && !state.busy &&
           !state.cleanup_error && pending == CMD_NONE && state.mode != WIRED_NO_IP &&
           generation == address_generation && generation == ping_generation;
}

bool wired_ping_reserve(dhcp_status_t *snapshot)
{
    portENTER_CRITICAL(&lock);
    bool ok = initialized && !ping_reserved && !update_reserved && link_up && state.active && !state.busy &&
              !state.cleanup_error && pending == CMD_NONE && rx_enabled && tx_enabled;
    if (ok) {
        ping_reserved = true; ping_window = false; ping_generation = address_generation;
        *snapshot = state; snapshot->generation = address_generation;
    }
    portEXIT_CRITICAL(&lock);
    return ok;
}

bool wired_ping_valid(unsigned generation)
{
    portENTER_CRITICAL(&lock);
    bool valid = ping_valid_locked(generation);
    portEXIT_CRITICAL(&lock);
    return valid;
}

bool wired_ping_binding(unsigned generation, wired_ping_binding_t *binding)
{
    if (!wired_ping_valid(generation)) return false;
    esp_netif_ip_info_t ip;
    if (!esp_netif_is_netif_up(netif) || esp_netif_get_ip_info(netif, &ip) != ESP_OK ||
        esp_netif_get_netif_impl_name(netif, binding->name) != ESP_OK) return false;
    binding->name[sizeof(binding->name) - 1] = 0;
    binding->ip = lwip_ntohl(ip.ip.addr); binding->mask = lwip_ntohl(ip.netmask.addr);
    binding->gateway = lwip_ntohl(ip.gw.addr);
    portENTER_CRITICAL(&lock);
    bool valid = ping_valid_locked(generation) && binding->ip && binding->name[0] &&
                 binding->ip == ping_ipv4(state.ip) && binding->mask == ping_ipv4(state.mask) &&
                 binding->gateway == ping_ipv4(state.gateway);
    portEXIT_CRITICAL(&lock);
    return valid;
}

void wired_ping_expect(unsigned generation, uint32_t target, uint16_t id, uint16_t sequence, uint64_t cookie)
{
    portENTER_CRITICAL(&lock);
    ping_window = false;
    ping_arrival_us = 0;
    if (sequence && ping_valid_locked(generation)) {
        ping_local = ping_ipv4(state.ip); ping_target = target;
        ping_id = id; ping_sequence = sequence; ping_cookie = cookie; ping_window = true;
    }
    portEXIT_CRITICAL(&lock);
}

void wired_ping_release(void)
{
    portENTER_CRITICAL(&lock);
    ping_window = ping_reserved = false;
    portEXIT_CRITICAL(&lock);
}

uint64_t wired_ping_reply_time(unsigned generation)
{
    portENTER_CRITICAL(&lock);
    uint64_t stamp = ping_valid_locked(generation) && ping_window ? ping_arrival_us : 0;
    portEXIT_CRITICAL(&lock);
    return stamp;
}

static bool manual_current(void)
{
    portENTER_CRITICAL(&lock);
    bool valid = link_up && session == link_session && pending == CMD_NONE &&
                 state.state == MANUAL_PROBING;
    portEXIT_CRITICAL(&lock);
    return valid;
}

static esp_err_t manual_send(bool announce)
{
    uint8_t frame[60];
    manual_ipv4_arp(frame, ethernet_mac, manual_config.ip, announce);
    portENTER_CRITICAL(&lock);
    bool valid = manual_watch && watch_session == link_session && link_up && pending == CMD_NONE;
    portEXIT_CRITICAL(&lock);
    return valid ? esp_eth_transmit(eth_driver, frame, sizeof(frame)) : ESP_ERR_INVALID_STATE;
}

static unsigned random_delay(unsigned range)
{
    // Map 32 random bits to millisecond buckets, with no retry loop.
    return (unsigned)(((uint64_t)esp_random() * range) >> 32);
}

static void manual_step(dhcp_status_t *next)
{
    if (!manual_running || clock_ms() < manual_deadline) return;
    if (manual_probes < 3) {
        if (checked(next, "ARP probe transmit", manual_send(false)) != ESP_OK) goto failed;
        ++manual_probes;
        manual_deadline = clock_ms() + (manual_probes == 3 ? 2000 : 1000 + random_delay(1001));
        return;
    }
    if (!manual_announcements) {
        if (!manual_current()) return;
        esp_netif_ip_info_t ip = {0};
        ip.ip.addr = lwip_htonl(manual_config.ip);
        ip.netmask.addr = lwip_htonl(manual_ipv4_mask(manual_config.prefix));
        ip.gw.addr = lwip_htonl(manual_config.gateway);
        if (checked(next, "esp_netif_set_ip_info(manual)", esp_netif_set_ip_info(netif, &ip)) != ESP_OK) goto failed;
        if (!manual_current()) return;
        // Keep stack TX/RX gated during startup: lwIP's automatic link-up GARP
        // is suppressed. Only our timed RFC 5227 announcements may claim the IP.
        esp_netif_action_start(netif, NULL, 0, NULL);
        stack_started = esp_netif_is_netif_up(netif);
        if (!stack_started) { checked(next, "manual netif start verification", ESP_FAIL); goto failed; }
        if (!manual_current()) return;
        if (checked(next, "ARP announcement transmit", manual_send(true)) != ESP_OK) goto failed;
        portENTER_CRITICAL(&lock);
        if (link_up && session == link_session && pending == CMD_NONE) {
            watch_probing = false;
            rx_enabled = tx_enabled = true;
        }
        portEXIT_CRITICAL(&lock);
        next->state = MANUAL_ACTIVE;
        next->mode = WIRED_MANUAL;
        next->active = true;
        next->prefix = manual_config.prefix;
        ip_text(next->ip, ip.ip); ip_text(next->mask, ip.netmask);
        if (manual_config.gateway) ip_text(next->gateway, ip.gw);
        ++manual_announcements;
        manual_deadline = clock_ms() + 2000;
    } else if (manual_announcements == 1) {
        if (checked(next, "ARP announcement transmit", manual_send(true)) != ESP_OK) goto failed;
        ++manual_announcements;
    }
    return;
failed:
    next->state = DHCP_ERROR;
    cleanup(next);
}

// One bounded worker step; no IP event callback is registered. Live stack reads
// after synchronized cleanup cannot resurrect queued acquisition/loss events.
static void worker_step(void)
{
    portENTER_CRITICAL(&lock);
    int command = pending;
    pending = CMD_NONE;
    dhcp_status_t next = state;
    unsigned expected = link_session;
    bool connected = link_up && session == link_session;
    portEXIT_CRITICAL(&lock);
    if (command == CMD_STOP || command == CMD_CANCEL || command == CMD_CONFLICT) {
        if (command == CMD_CONFLICT) {
            portENTER_CRITICAL(&lock);
            if (manual_conflicts < 10) ++manual_conflicts;
            portEXIT_CRITICAL(&lock);
        }
        next.error = ESP_OK;
        next.error_step[0] = 0;
        cleanup(&next);
    } else if (command == CMD_MANUAL && connected) {
        manual_config = requested_manual;
        cleanup(&next); // Release DHCP and remove old routing before any probe.
        if (next.cleanup_error) { next.state = DHCP_ERROR; goto publish; }
        checked(&next, "esp_netif_tcpip_exec(borrow DNS)", esp_netif_tcpip_exec(borrow_dns, NULL));
        if (next.error) { next.state = DHCP_ERROR; cleanup(&next); goto publish; }
        if (!manual_current()) goto publish;
        manual_running = true;
        manual_probes = manual_announcements = 0;
        manual_deadline = clock_ms() + random_delay(1001);
        portENTER_CRITICAL(&lock);
        if (link_up && session == link_session && pending == CMD_NONE) {
            watch_ip = manual_config.ip;
            watch_session = link_session;
            watch_probing = manual_watch = true;
            tx_enabled = false;
        }
        portEXIT_CRITICAL(&lock);
    } else if (command == CMD_START && connected) {
        next.error = ESP_OK;
        next.error_step[0] = 0;
        checked(&next, "esp_netif_tcpip_exec(borrow DNS)", esp_netif_tcpip_exec(borrow_dns, NULL));
        if (next.error) {
            next.state = DHCP_ERROR;
            cleanup(&next);
            goto publish;
        }
        portENTER_CRITICAL(&lock);
        tx_enabled = link_up && session == link_session && pending == CMD_NONE;
        portEXIT_CRITICAL(&lock);
        esp_netif_action_start(netif, NULL, 0, NULL);
        stack_started = esp_netif_is_netif_up(netif);
        if (!stack_started) checked(&next, "esp_netif_action_start verification", ESP_FAIL);
        else {
            portENTER_CRITICAL(&lock);
            rx_enabled = link_up && session == link_session && pending == CMD_NONE;
            portEXIT_CRITICAL(&lock);
            checked(&next, "esp_netif_dhcpc_start", esp_netif_dhcpc_start(netif));
        }
        if (next.error) { next.state = DHCP_ERROR; cleanup(&next); }
    }
    if (connected && (next.state == MANUAL_PROBING || next.mode == WIRED_MANUAL)) manual_step(&next);
    if (connected && (next.state == DHCP_REQUESTING || (next.active && next.mode == WIRED_DHCP)) && stack_started) {
        esp_netif_ip_info_t ip = {0};
        esp_err_t err = checked(&next, "esp_netif_get_ip_info", esp_netif_get_ip_info(netif, &ip));
        uint64_t elapsed = clock_ms() - next.started_ms;
        if (err != ESP_OK) next.state = DHCP_ERROR;
        else if (next.state == DHCP_REQUESTING && elapsed >= 30000) next.state = DHCP_TIMEOUT;
        else if (ip.ip.addr) {
            if (dhcp_management_overlap(lwip_ntohl(ip.ip.addr), lwip_ntohl(ip.netmask.addr))) next.state = DHCP_OVERLAP;
            else {
                if (!next.active) next.duration_ms = elapsed;
                next.state = DHCP_ACQUIRED;
                next.active = true;
                next.mode = WIRED_DHCP;
                ip_text(next.ip, ip.ip); ip_text(next.mask, ip.netmask); ip_text(next.gateway, ip.gw);
                for (int i = 0; i < 2; ++i) {
                    esp_netif_dns_info_t dns = {0};
                    if (checked(&next, "esp_netif_get_dns_info", esp_netif_get_dns_info(netif, i, &dns)) != ESP_OK)
                        next.state = DHCP_ERROR;
                    if (dns.ip.u_addr.ip4.addr) ip_text(next.dns[i], dns.ip.u_addr.ip4);
                    else next.dns[i][0] = 0;
                }
            }
        } else if (next.active) next.state = DHCP_ADDRESS_LOST;
        if (next.state != DHCP_REQUESTING && next.state != DHCP_ACQUIRED) {
            if (!next.active) next.duration_ms = elapsed;
            cleanup(&next);
        }
    }
publish:
    next.busy = false;
    portENTER_CRITICAL(&lock);
    if (pending == CMD_NONE && expected == link_session) {
        if (state.active != next.active || state.mode != next.mode || strcmp(state.ip, next.ip) ||
            strcmp(state.mask, next.mask) || strcmp(state.gateway, next.gateway))
            ++address_generation;
        state = next;
    } else if (next.cleanup_error) {
        // Link/cancel owns the outcome, but cannot discard failed resource cleanup.
        state.cleanup_error = true;
        state.cleanup_code = next.cleanup_code;
        snprintf(state.cleanup_step, sizeof(state.cleanup_step), "%s", next.cleanup_step);
    }
    // A newer cancellation/link event already owns published state and next step.
    portEXIT_CRITICAL(&lock);
}

static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        worker_step(); network_ping_step();
        portENTER_CRITICAL(&lock);worker_heartbeat=clock_ms();portEXIT_CRITICAL(&lock);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

esp_err_t dhcp_test_init(esp_eth_handle_t driver, const uint8_t mac[6])
{
    eth_driver = driver;
    memcpy(ethernet_mac, mac, 6);
    esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_ETH();
    base.flags = ESP_NETIF_DHCP_CLIENT | ESP_NETIF_FLAG_AUTOUP;
    // No default glue/ETH handlers: physical events never start an IP session.
    base.route_prio = 5;
    esp_netif_driver_ifconfig_t io = {
        .handle = eth_driver, .transmit = transmit, .driver_free_rx_buffer = free_rx
    };
    esp_netif_config_t config = {.base = &base, .driver = &io, .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH};
    netif = esp_netif_new(&config);
    dhcp_status_t setup = {0};
    esp_err_t err = checked(&setup, "esp_netif_new(DHCP)", netif ? ESP_OK : ESP_ERR_NO_MEM);
    if (err == ESP_OK) err = checked(&setup, "esp_netif_set_mac", esp_netif_set_mac(netif, (uint8_t *)mac));
    if (err == ESP_OK) err = checked(&setup, "esp_netif_dhcpc_stop(boot)", esp_netif_dhcpc_stop(netif));
    if (err == ESP_OK) err = checked(&setup, "xTaskCreate(dhcp_test)",
        xTaskCreate(worker, "dhcp_test", 4096, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    portENTER_CRITICAL(&lock);
    initialized = err == ESP_OK;
    if (!initialized) {
        state = setup;
        state.state = DHCP_ERROR;
    }
    portEXIT_CRITICAL(&lock);
    if (err != ESP_OK) ESP_LOGE("dhcp", "DHCP test setup: %s (0x%x)", esp_err_to_name(err), (unsigned)err);
    return err;
}

bool wired_worker_healthy(void)
{
    portENTER_CRITICAL(&lock);
    bool ok=initialized && worker_heartbeat && clock_ms()-worker_heartbeat<1000;
    portEXIT_CRITICAL(&lock);return ok;
}
