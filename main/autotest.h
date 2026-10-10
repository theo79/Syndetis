#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "network_status.h"
#include "autotest_rules.h"

typedef enum { AT_CHECK_LINK, AT_CHECK_NEGOTIATION, AT_CHECK_DHCP, AT_CHECK_IPV4,
               AT_CHECK_GATEWAY, AT_CHECK_DNS, AT_CHECK_RESOLUTION, AT_CHECK_COUNT } autotest_check_t;
typedef struct {
    bool completed;
    uint32_t finished_uptime_s;
    autotest_grade_t overall;
    autotest_grade_t checks[AT_CHECK_COUNT];
    network_status_t network;
    uint32_t ping_sent, ping_received, loss_percent;
    uint32_t ping_min_ms, ping_avg_ms, ping_max_ms;
    bool ping_measured;
    char note[80];
    char dns_hostname[64];
} autotest_result_t;
typedef struct {
    bool running;
    char progress[32];
    /* Retains the last completed result throughout a subsequent run. */
    autotest_result_t last;
} autotest_status_t;

esp_err_t autotest_init(void);
esp_err_t autotest_start(void);
void autotest_get(autotest_status_t *status);
const char *autotest_grade_name(autotest_grade_t grade);
