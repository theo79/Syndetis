#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "manual_ipv4.h"

typedef enum {
    DHCP_OFF, DHCP_REQUESTING, DHCP_ACQUIRED, DHCP_CANCELLED, DHCP_STOPPED,
    DHCP_TIMEOUT, DHCP_LINK_LOST, DHCP_ADDRESS_LOST, DHCP_OVERLAP, DHCP_ERROR,
    DHCP_CONNECT, MANUAL_PROBING, MANUAL_ACTIVE, MANUAL_CONFLICT
} dhcp_state_t;

typedef enum { WIRED_NO_IP, WIRED_DHCP, WIRED_MANUAL } wired_mode_t;

typedef struct {
    dhcp_state_t state;
    bool active, busy, cleanup_error;
    uint64_t started_ms, duration_ms;
    char ip[16], mask[16], gateway[16], dns[2][16];
    esp_err_t error;
    char error_step[48];
    esp_err_t cleanup_code;
    char cleanup_step[48];
    wired_mode_t mode;
    uint8_t prefix;
    unsigned generation;
} dhcp_status_t;

esp_err_t dhcp_test_init(void *driver, const uint8_t mac[6]);
void dhcp_test_link(bool up); // Short event notification only; no stack work.
bool dhcp_test_request(bool start);
bool dhcp_test_cancel_requesting(void);
dhcp_status_t dhcp_test_status(void);
const char *dhcp_state_name(dhcp_state_t state);
// Takes ownership only when true, including receive errors. Otherwise caller frees.
bool dhcp_test_receive(uint8_t *buffer, uint32_t length);
// NULL means explicitly accepted; otherwise a stable, displayable rejection.
// The review generation must still match, including a DHCP lease acquired while editing.
const char *wired_manual_apply(const manual_ipv4_t *config, unsigned review_generation);
bool wired_manual_cancel(void); // Cancels probing only; navigation never releases an active address.
bool wired_use_dhcp(void); // Release to passive mode; DHCP still requires its normal Start.
typedef struct { uint32_t ip, mask, gateway; char name[8]; } wired_ping_binding_t;
bool wired_ping_reserve(dhcp_status_t *snapshot);
bool wired_ping_valid(unsigned generation);
bool wired_ping_binding(unsigned generation, wired_ping_binding_t *binding);
void wired_ping_expect(unsigned generation, uint32_t target, uint16_t id, uint16_t sequence, uint64_t cookie);
void wired_ping_release(void);
uint64_t wired_ping_reply_time(unsigned generation);
// Maintenance owns the current address without changing it. Wi-Fi-only sessions
// can reserve while wired IPv4 is off. New diagnostic mutations are excluded.
bool wired_update_reserve(unsigned *generation);
bool wired_update_valid(unsigned generation);
void wired_update_extend(void);
void wired_update_release(void);

bool wired_worker_healthy(void);
