#include "dashboard.h"
#include "status.h"
#include "discovery.h"
#include "dhcp_test.h"
#include "network_policy.h"
#include "web_guard.h"
#include <string.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdarg.h>
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_log.h"

extern const unsigned char dashboard_html_start[] asm("_binary_dashboard_html_start");
extern const unsigned char dashboard_html_end[] asm("_binary_dashboard_html_end");

static void response_headers(httpd_req_t *request)
{
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(request, "Connection", "close");
}

static esp_err_t index_get(httpd_req_t *request)
{
    if (!dashboard_hotspot_request(request))
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Hotspot only");
    response_headers(request);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, (const char *)dashboard_html_start,
                           dashboard_html_end - dashboard_html_start - 1);
}

typedef struct { char *data; size_t length, capacity; bool failed; } json_writer_t;

static void json_add(json_writer_t *w, const char *format, ...)
{
    if (w->failed) return;
    va_list args;
    va_start(args, format);
    int n = vsnprintf(w->data + w->length, w->capacity - w->length, format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= w->capacity - w->length) w->failed = true;
    else w->length += n;
}

static void json_string(json_writer_t *w, const char *value, bool present)
{
    if (!present) { json_add(w, "null"); return; }
    json_add(w, "\"");
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        if (*p == '"' || *p == '\\') json_add(w, "\\%c", *p);
        else if (*p < 32) json_add(w, "\\u%04x", *p);
        else json_add(w, "%c", *p);
    }
    json_add(w, "\"");
}

static void switch_json(json_writer_t *w, ethernet_state_t eth_state)
{
    static neighbor_table_t table; // HTTP handlers run serially on the HTTP server task.
    bool available = discovery_snapshot(&table);
    uint64_t now = esp_timer_get_time() / 1000;
    const char *state = !available ? "Unavailable" : eth_state != ETH_READY ? "Ethernet unavailable" :
                         neighbors_state_name(neighbors_state(&table, now));
    json_add(w, ",\"switch_info\":{\"state\":\"%s\",\"observation_seconds\":30,\"capacity\":%d,"
             "\"capacity_drops\":%" PRIu32 ",\"queue_drops\":%" PRIu32 ","
             "\"invalid_frames\":%" PRIu32 ",\"limited_frames\":%" PRIu32 ",\"neighbors\":[",
             state, NEIGHBOR_CAPACITY, table.capacity_drops, table.queue_drops, table.invalid_frames, table.limited_frames);
    bool comma = false;
    for (int i = 0; i < NEIGHBOR_CAPACITY; ++i) {
        const neighbor_t *r = &table.records[i];
        if (!r->used || !table.link_up) continue;
        json_add(w, "%s{\"slot\":%d,\"protocol\":\"%s\",\"source_mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\",",
                 comma ? "," : "", i + 1, r->protocol == PROTOCOL_LLDP ? "LLDP" : "CDP",
                 r->source[0], r->source[1], r->source[2], r->source[3], r->source[4], r->source[5]);
        comma = true;
        json_add(w, "\"name\":"); json_string(w, r->name, r->fields & FIELD_NAME);
        json_add(w, ",\"chassis\":"); json_string(w, r->chassis, r->fields & FIELD_CHASSIS);
        json_add(w, ",\"port_id\":"); json_string(w, r->port, r->fields & FIELD_PORT);
        json_add(w, ",\"port_description\":"); json_string(w, r->description, r->fields & FIELD_DESCRIPTION);
        json_add(w, ",\"management_ip\":"); json_string(w, r->management, r->fields & FIELD_MANAGEMENT);
        json_add(w, ",\"management_source\":");
        json_string(w, r->management_kind == 2 ? "CDP address TLV" : "Management address TLV", r->fields & FIELD_MANAGEMENT);
        json_add(w, ",\"vlan_kind\":\"%s\",\"vlan\":", r->protocol == PROTOCOL_LLDP ? "Port PVID" : "Native VLAN");
        if (r->fields & FIELD_VLAN) json_add(w, "%u", r->vlan); else json_add(w, "null");
        json_add(w, ",\"ttl_seconds\":%u,\"age_seconds\":%" PRIu64 ",\"expired\":%s,\"sanitized\":%s}",
                 r->ttl, now >= r->received_ms ? (now - r->received_ms) / 1000 : 0,
                 neighbor_expired(r, now) ? "true" : "false", r->sanitized ? "true" : "false");
    }
    json_add(w, "]}");
}

static esp_err_t status_get_handler(httpd_req_t *request)
{
    if (!dashboard_hotspot_request(request))
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Hotspot only");
    app_status_t snapshot = status_get();
    ethernet_status_t *eth = &snapshot.ethernet;
    char mac[24] = "Unavailable";
    if (eth->mac_valid)
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 eth->mac[0], eth->mac[1], eth->mac[2], eth->mac[3], eth->mac[4], eth->mac[5]);
    char speed[12] = "null";
    if (eth->link_up && eth->speed_mbps)
        snprintf(speed, sizeof(speed), "%d", eth->speed_mbps);
    const char *duplex = !eth->link_up || !eth->duplex ? "Unavailable" :
                         eth->duplex == 2 ? "Full" : "Half";
    const char *link = eth->state == ETH_READY ? (eth->link_up ? "Up" : "Down") : "Unavailable";
    dhcp_status_t dhcp = dhcp_test_status();
    bool dhcp_active = dhcp.active && dhcp.mode == WIRED_DHCP;
    bool manual_owner = dhcp.mode == WIRED_MANUAL || dhcp.state == MANUAL_PROBING;
    // Fixed upper bound includes worst-case escaping of four full records.
    static char json[16384];
    json_writer_t writer = {.data = json, .capacity = sizeof(json)};
    json_add(&writer,
        "{\"firmware\":\"%s\",\"uptime_seconds\":%" PRId64 ","
        "\"ethernet\":{\"initialization\":\"%s\",\"error_code\":%d,\"error_step\":\"%s\","
        "\"link\":\"%s\",\"speed_mbps\":%s,\"duplex\":\"%s\",\"mac\":\"%s\","
        "\"ip\":\"%s\"}",
        esp_app_get_description()->version, esp_timer_get_time() / 1000000,
        status_ethernet_state_name(eth->state), (int)eth->error, eth->error_step,
        link, speed, duplex, mac, dhcp.active ? dhcp.ip : "Not configured");
    json_add(&writer, ",\"addressing\":{\"mode\":\"%s\",\"state\":",
             dhcp.mode == WIRED_MANUAL ? "MANUAL IP" : dhcp.mode == WIRED_DHCP ? "DHCP" : "NO IP");
    json_string(&writer, dhcp_state_name(dhcp.state), true);
    json_add(&writer, ",\"prefix\":%u,\"gateway\":", dhcp.prefix);
    json_string(&writer, dhcp.gateway, dhcp.active && dhcp.gateway[0]);
    json_add(&writer, "}");
    json_add(&writer, ",\"dhcp\":{\"blocked_by_manual\":%s,\"state\":", manual_owner ? "true" : "false");
    json_string(&writer, dhcp_state_name(dhcp.state), true);
    json_add(&writer, ",\"requesting\":%s,\"active\":%s,\"busy\":%s,\"cleanup_error\":%s,\"duration_ms\":%" PRIu64,
             dhcp.state == DHCP_REQUESTING ? "true" : "false", dhcp_active ? "true" : "false",
             dhcp.busy ? "true" : "false", dhcp.cleanup_error ? "true" : "false", dhcp.duration_ms);
    json_add(&writer, ",\"ip\":"); json_string(&writer, dhcp.ip, dhcp_active);
    json_add(&writer, ",\"mask\":"); json_string(&writer, dhcp.mask, dhcp_active);
    json_add(&writer, ",\"gateway\":"); json_string(&writer, dhcp.gateway, dhcp_active);
    json_add(&writer, ",\"dns\":["); json_string(&writer, dhcp.dns[0], dhcp_active && dhcp.dns[0][0]);
    json_add(&writer, ","); json_string(&writer, dhcp.dns[1], dhcp_active && dhcp.dns[1][0]);
    json_add(&writer, "],\"error_code\":%d,\"error_step\":", (int)dhcp.error);
    json_string(&writer, dhcp.error_step, dhcp.error != ESP_OK);
    json_add(&writer, ",\"cleanup_code\":%d,\"cleanup_step\":", (int)dhcp.cleanup_code);
    json_string(&writer, dhcp.cleanup_step, dhcp.cleanup_error);
    json_add(&writer, "}");
    switch_json(&writer, eth->state);
    json_add(&writer, "}");
    if (writer.failed)
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Status too large");
    response_headers(request);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, json, writer.length);
}

static esp_err_t dhcp_post(httpd_req_t *request)
{
    if (!dashboard_hotspot_request(request))
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Hotspot only");
    char host[32], origin[48], type[32];
    if (httpd_req_get_hdr_value_str(request, "Host", host, sizeof(host)) != ESP_OK ||
        httpd_req_get_hdr_value_str(request, "Origin", origin, sizeof(origin)) != ESP_OK ||
        httpd_req_get_hdr_value_str(request, "Content-Type", type, sizeof(type)) != ESP_OK ||
        !dhcp_browser_request(host, origin, type, request->content_len))
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Same-origin JSON required");
    char body[2];
    size_t used = 0;
    while (used < sizeof(body)) {
        int n = httpd_req_recv(request, body + used, sizeof(body) - used);
        if (n <= 0) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Expected {}");
        used += n;
    }
    if (memcmp(body, "{}", 2)) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Expected {}");
    bool start = !strcmp(request->uri, "/api/dhcp/start");
    if (!dhcp_test_request(start)) {
        httpd_resp_set_status(request, "409 Conflict");
        return httpd_resp_send(request, "Connect Ethernet, operation active or cleanup required", HTTPD_RESP_USE_STRLEN);
    }
    response_headers(request);
    httpd_resp_set_status(request, "202 Accepted");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, "{\"accepted\":true}", HTTPD_RESP_USE_STRLEN);
}

esp_err_t dashboard_start(httpd_handle_t *server)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 1;
    config.send_wait_timeout = 1;
    esp_err_t err = httpd_start(server, &config);
    if (err != ESP_OK) {
        ESP_LOGE("web", "httpd_start: %s (0x%x)", esp_err_to_name(err), (unsigned)err);
        return err;
    }
    const httpd_uri_t index = {.uri = "/", .method = HTTP_GET, .handler = index_get};
    const httpd_uri_t status = {.uri = "/api/status", .method = HTTP_GET, .handler = status_get_handler};
    const httpd_uri_t start = {.uri = "/api/dhcp/start", .method = HTTP_POST, .handler = dhcp_post};
    const httpd_uri_t stop = {.uri = "/api/dhcp/stop", .method = HTTP_POST, .handler = dhcp_post};
    err = httpd_register_uri_handler(*server, &index);
    if (err != ESP_OK)
        ESP_LOGE("web", "httpd_register_uri_handler(/): %s (0x%x)", esp_err_to_name(err), (unsigned)err);
    else {
        err = httpd_register_uri_handler(*server, &status);
        if (err != ESP_OK)
            ESP_LOGE("web", "httpd_register_uri_handler(/api/status): %s (0x%x)", esp_err_to_name(err), (unsigned)err);
    }
    if (err == ESP_OK) err = httpd_register_uri_handler(*server, &start);
    if (err == ESP_OK) err = httpd_register_uri_handler(*server, &stop);
    if (err != ESP_OK) ESP_LOGE("web", "DHCP route registration: %s (0x%x)", esp_err_to_name(err), (unsigned)err);
    // Caller owns any non-null handle, including partial registration failure.
    return err;
}
