#include "profinet.h"
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "network_status.h"
#include "firmware_update.h"
#include "web_server.h"
#include "autotest.h"
#include "discovery.h"
#include "switch_info.h"

extern const char web_index_start[] asm("_binary_web_index_html_start");
extern const char web_index_end[] asm("_binary_web_index_html_end");
static httpd_handle_t server;

static esp_err_t index_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    return httpd_resp_send(req, web_index_start, web_index_end - web_index_start - 1);
}

/* Escape metadata/messages even though the browser uses textContent. */
static void json_escape(char *out, size_t size, const char *in, size_t limit)
{
    size_t used = 0;
    for (size_t i = 0; i < limit && in[i] && used + 6 < size; ++i) {
        unsigned char c = in[i];
        if (c == '"' || c == '\\' || c < 0x20 || c >= 0x7f) {
            used += snprintf(out + used, size - used, "\\u%04x", c);
        } else out[used++] = c;
    }
    out[used] = 0;
}

static esp_err_t status_json(httpd_req_t *req)
{
    network_status_t network;
    ota_status_t ota;
    network_status_get(&network);
    firmware_update_status(&ota);
    const esp_partition_t *partition = esp_ota_get_running_partition();
    char ip[16], netmask[16], gateway[16], mac[18];
    snprintf(ip, sizeof(ip), IPSTR, IP2STR(&network.ipv4.ip));
    snprintf(netmask, sizeof(netmask), IPSTR, IP2STR(&network.ipv4.netmask));
    snprintf(gateway, sizeof(gateway), IPSTR, IP2STR(&network.ipv4.gw));
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", network.mac[0],
             network.mac[1], network.mac[2], network.mac[3], network.mac[4], network.mac[5]);
    char version[193], message[577], label[97], body[1536];
    json_escape(version, sizeof(version), esp_app_get_description()->version, 32);
    json_escape(message, sizeof(message), ota.message, sizeof(ota.message));
    json_escape(label, sizeof(label), partition ? partition->label : "unknown", 16);
    int size = snprintf(body, sizeof(body),
        "{\"version\":\"%s\",\"link_up\":%s,\"ip\":\"%s\","
        "\"netmask\":\"%s\",\"gateway\":\"%s\",\"mac\":\"%s\","
        "\"partition\":\"%s\",\"uptime\":%" PRId64 ","
        "\"update_unlocked\":%s,\"update_seconds\":%u,\"update_phase\":%u,"
        "\"update_percent\":%u,\"update_message\":\"%s\"}",
        version, network.link_up ? "true" : "false", ip, netmask, gateway, mac, label,
        esp_timer_get_time() / 1000000, ota.unlocked ? "true" : "false", ota.seconds_left,
        (unsigned)ota.phase, ota.percent, message);
    if (size < 0 || size >= sizeof(body)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Status overflow");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body, size);
}


/* Read-only snapshot: HTTP never starts diagnostics or calls network APIs. */
static esp_err_t autotest_json(httpd_req_t *req)
{
    autotest_status_t test;
    autotest_get(&test);
    const autotest_result_t *r = &test.last;
    const network_status_t *n = &r->network;
    char progress[193], note[481], hostname[385], body[2304];
    json_escape(progress, sizeof(progress), test.progress, sizeof(test.progress));
    json_escape(note, sizeof(note), r->note, sizeof(r->note));
    json_escape(hostname, sizeof(hostname), r->dns_hostname, sizeof(r->dns_hostname));
    int size = snprintf(body, sizeof(body),
        "{\"running\":%s,\"progress\":\"%s\",\"completed\":%s,\"finished_uptime\":%u,"
        "\"overall\":\"%s\",\"link_up\":%s,\"speed_mbps\":%u,\"speed_known\":%s,"
        "\"duplex\":\"%s\",\"link\":\"%s\",\"negotiation\":\"%s\",\"dhcp\":\"%s\",\"ipv4\":\"%s\","
        "\"ip\":\"" IPSTR "\",\"netmask\":\"" IPSTR "\",\"gateway\":\"" IPSTR "\","
        "\"gateway_status\":\"%s\",\"ping_measured\":%s,\"ping_sent\":%u,\"ping_received\":%u,"
        "\"ping_avg_ms\":%u,\"ping_min_ms\":%u,\"ping_max_ms\":%u,\"loss_percent\":%u,"
        "\"dns\":\"%s\",\"dns_main\":\"" IPSTR "\",\"dns_backup\":\"" IPSTR "\","
        "\"resolution\":\"%s\",\"dns_hostname\":\"%s\",\"note\":\"%s\"}",
        test.running ? "true" : "false", progress, r->completed ? "true" : "false", (unsigned)r->finished_uptime_s,
        r->completed ? autotest_grade_name(r->overall) : "SKIP", n->link_up ? "true" : "false", n->speed_mbps,
        n->speed_known ? "true" : "false", !n->duplex_known ? "Unknown" : n->full_duplex ? "Full" : "Half",
        autotest_grade_name(r->checks[AT_CHECK_LINK]), autotest_grade_name(r->checks[AT_CHECK_NEGOTIATION]),
        autotest_grade_name(r->checks[AT_CHECK_DHCP]), autotest_grade_name(r->checks[AT_CHECK_IPV4]),
        IP2STR(&n->ipv4.ip), IP2STR(&n->ipv4.netmask), IP2STR(&n->ipv4.gw),
        autotest_grade_name(r->checks[AT_CHECK_GATEWAY]), r->ping_measured ? "true" : "false",
        (unsigned)r->ping_sent, (unsigned)r->ping_received, (unsigned)r->ping_avg_ms, (unsigned)r->ping_min_ms, (unsigned)r->ping_max_ms, (unsigned)r->loss_percent,
        autotest_grade_name(r->checks[AT_CHECK_DNS]), IP2STR(&n->dns[0]), IP2STR(&n->dns[1]),
        autotest_grade_name(r->checks[AT_CHECK_RESOLUTION]), hostname, note);
    if (size < 0 || size >= sizeof(body)) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "AutoTest overflow");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body, size);
}

/* One HTTP server task owns this fixed snapshot buffer; never put the table
 * on the HTTP task stack and never hold discovery's mutex while sending. */
static esp_err_t discovery_json(httpd_req_t *req)
{
    static discovery_device_t snapshot[DISCOVERY_MAX_HOSTS];
    discovery_status_t d;
    discovery_snapshot(&d, snapshot);
    esp_ip4_addr_t network={.addr=esp_netif_htonl(d.subnet.network)};
    esp_ip4_addr_t mask={.addr=esp_netif_htonl(d.subnet.mask)};
    esp_ip4_addr_t broadcast={.addr=esp_netif_htonl(d.subnet.broadcast)};
    char error[481], body[1536];
    json_escape(error,sizeof(error),d.error,sizeof(d.error));
    int size=snprintf(body,sizeof(body),
        "{\"state\":\"%s\",\"network\":\"" IPSTR "\",\"mask\":\"" IPSTR "\",\"prefix\":%u,"
        "\"broadcast\":\"" IPSTR "\",\"usable_hosts\":%u,\"candidate_hosts\":%u,\"processed\":%u,\"found\":%u,"
        "\"start_uptime_ms\":%u,\"completion_uptime_ms\":%u,\"duration_ms\":%u,\"max_hosts\":254,"
        "\"icmp_setup_failures\":%u,\"icmp_measured\":%u,\"icmp_unmeasured\":%u,"
        "\"hostname_enriching\":%s,\"hostname_attempted\":%u,\"hostname_resolved\":%u,\"hostname_timeout\":%u,"
        "\"hostname_failed\":%u,\"hostname_duration_ms\":%u,\"hostname_packets_sent\":%u,\"hostname_cancelled\":%s,"
        "\"limit_note\":\"Active discovery is limited to 254 usable hosts (/24 max)\",\"error\":\"%s\",\"devices\":[",
        discovery_state_name(d.state),IP2STR(&network),IP2STR(&mask),d.subnet.prefix,IP2STR(&broadcast),
        (unsigned)d.subnet.usable,(unsigned)d.subnet.candidates,(unsigned)d.processed,(unsigned)d.found,
        (unsigned)d.start_uptime_ms,(unsigned)d.completion_uptime_ms,(unsigned)d.duration_ms,
        (unsigned)d.icmp_setup_failures,(unsigned)d.icmp_measured,(unsigned)d.icmp_unmeasured,
        d.hostname.enriching ? "true":"false",(unsigned)d.hostname.attempted,(unsigned)d.hostname.resolved,
        (unsigned)d.hostname.timeout,(unsigned)d.hostname.failed,(unsigned)d.hostname.duration_ms,
        (unsigned)d.hostname.packets_sent,d.hostname.cancelled ? "true":"false",error);
    httpd_resp_set_type(req,"application/json");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    if (size<0 || size>=sizeof(body)) return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Discovery overflow");
    esp_err_t err=httpd_resp_send_chunk(req,body,size);
    for (unsigned i=0; err==ESP_OK && i<d.found; ++i) {
        discovery_device_t *host=&snapshot[i];
        esp_ip4_addr_t ip={.addr=esp_netif_htonl(host->ip)};
        char mac[21]="null",rtt[16]="null";
        if (host->mac_valid) snprintf(mac,sizeof(mac),"\"%02X:%02X:%02X:%02X:%02X:%02X\"",host->mac[0],host->mac[1],host->mac[2],host->mac[3],host->mac[4],host->mac[5]);
        if (host->ping_measured && host->icmp_responsive) snprintf(rtt,sizeof(rtt),"%u",(unsigned)host->rtt_ms);
        char vendor[193], hostname[385], vendor_json[196]="null", hostname_json[400]="";
        if (host->locally_administered || host->vendor_present) {
            json_escape(vendor,sizeof(vendor),host->locally_administered ? "Local/Private" : host->vendor, sizeof(host->vendor));
            snprintf(vendor_json,sizeof(vendor_json),"\"%s\"",vendor);
        }
        if (host->hostname_present) {
            json_escape(hostname,sizeof(hostname),host->hostname,sizeof(host->hostname));
            snprintf(hostname_json,sizeof(hostname_json),",\"hostname\":\"%s\"",hostname);
        }
        size=snprintf(body,sizeof(body),"%s{\"ip\":\"" IPSTR "\",\"mac\":%s,\"arp_present\":%s,\"ping_response\":%s,\"rtt_ms\":%s,"
            "\"ping_measured\":%s,\"vendor\":%s,\"locally_administered\":%s,\"reachable\":%s,\"latency_ms\":%s,\"discovery_source\":\"arp\",\"last_seen_ms\":%u%s}",
            i ? "," : "",IP2STR(&ip),mac,host->arp_present ? "true":"false",host->ping_measured ? (host->icmp_responsive ? "true":"false") : "null",rtt,
            host->ping_measured ? "true":"false",vendor_json,host->locally_administered ? "true":"false",(host->arp_present || host->icmp_responsive) ? "true":"false",rtt,
            (unsigned)host->last_seen_ms,hostname_json);
        if (size<0 || size>=sizeof(body)) return ESP_FAIL;
        err=httpd_resp_send_chunk(req,body,size);
    }
    if (err==ESP_OK) err=httpd_resp_send_chunk(req,"]}",2);
    if (err==ESP_OK) err=httpd_resp_send_chunk(req,NULL,0);
    return err;
}

/* One HTTP task owns scratch; snapshot lock is released before any formatting. */
static esp_err_t switch_info_json(httpd_req_t *req)
{
    static switch_info_status_t status;
    switch_info_get(&status);
    const switch_neighbor_t *n = &status.neighbor;
    const switch_info_counters_t *c = &status.counters;
    char body[1024];
    int size = snprintf(body, sizeof(body),
        "{\"available\":%s,\"setup_error\":%d,\"cdp_available\":%s,\"cdp_setup_error\":%d,\"valid\":%s,\"protocol\":%s,"
        "\"free_heap\":%" PRIu32 ",\"minimum_free_heap\":%" PRIu32 ",\"worker_stack_free_bytes\":%" PRIu32 ","
        "\"counters\":{\"candidates\":%" PRIu32 ",\"queued\":%" PRIu32 ",\"parsed\":%" PRIu32 ","
        "\"malformed\":%" PRIu32 ",\"oversize\":%" PRIu32 ",\"pool_full\":%" PRIu32 ",\"queue_full\":%" PRIu32 ","
        "\"stale_generation\":%" PRIu32 ",\"expired\":%" PRIu32 ",\"filter_failures\":%" PRIu32
        ",\"cdp_candidates\":%" PRIu32 ",\"cdp_parsed\":%" PRIu32 ",\"cdp_malformed\":%" PRIu32 "}",
        status.available ? "true" : "false", status.setup_error, status.cdp_available ? "true" : "false",
        status.cdp_setup_error, n->valid ? "true" : "false",
        !n->valid ? "null" : n->protocol == SWITCH_PROTOCOL_LLDP ? "\"LLDP\"" :
                            n->protocol == SWITCH_PROTOCOL_CDP ? "\"CDP\"" : "null",
        status.free_heap, status.minimum_free_heap, status.worker_stack_free_bytes,
        c->candidates, c->queued, c->parsed, c->malformed, c->oversize, c->pool_full, c->queue_full,
        c->stale_generation, c->expired, c->filter_failures, c->cdp_candidates, c->cdp_parsed, c->cdp_malformed);
    if (size < 0 || size >= sizeof(body)) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Switch Info overflow");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send_chunk(req, body, size);
    if (n->valid) {
        const struct { const char *key, *value; size_t capacity; } fields[] = {
            {"chassis_id", n->chassis_id, sizeof(n->chassis_id)},
            {"device_id", n->device_id, sizeof(n->device_id)},
            {"port_id", n->port_id, sizeof(n->port_id)},
            {"port_description", n->port_description, sizeof(n->port_description)},
            {"system_name", n->system_name, sizeof(n->system_name)},
            {"system_description", n->system_description, sizeof(n->system_description)},
            {"platform", n->platform, sizeof(n->platform)},
        };
        char escaped[6 * SWITCH_INFO_DESCRIPTION_SIZE + 1];
        for (unsigned i = 0; err == ESP_OK && i < sizeof(fields)/sizeof(fields[0]); ++i) {
            if (!fields[i].value[0]) continue; /* Absent fields omitted. */
            json_escape(escaped, sizeof(escaped), fields[i].value, fields[i].capacity);
            size = snprintf(body, sizeof(body), ",\"%s\":\"", fields[i].key);
            err = httpd_resp_send_chunk(req, body, size);
            if (err == ESP_OK) err = httpd_resp_send_chunk(req, escaped, strlen(escaped));
            if (err == ESP_OK) err = httpd_resp_send_chunk(req, "\"", 1);
        }
        size = snprintf(body, sizeof(body),
            ",\"ttl\":%u,\"receive_timestamp_us\":%" PRId64 ",\"expiration_us\":%" PRId64 ",\"link_generation\":%" PRIu32
            ",\"source_mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\"",
            n->ttl, n->receive_timestamp_us, n->expiration_us, n->link_generation,
            n->source_mac[0], n->source_mac[1], n->source_mac[2], n->source_mac[3], n->source_mac[4], n->source_mac[5]);
        if (err == ESP_OK) err = httpd_resp_send_chunk(req, body, size);
        if (n->management_ipv4_present && err == ESP_OK) {
            size = snprintf(body, sizeof(body), ",\"management_ipv4\":\"%u.%u.%u.%u\"", n->management_ipv4[0],
                            n->management_ipv4[1], n->management_ipv4[2], n->management_ipv4[3]);
            err = httpd_resp_send_chunk(req, body, size);
        }
        if (n->pvid_present && err == ESP_OK) {
            size = snprintf(body, sizeof(body), ",\"pvid\":%u", n->pvid);
            err = httpd_resp_send_chunk(req, body, size);
        }
        if (n->capabilities_present && err == ESP_OK) {
            size = snprintf(body, sizeof(body), ",\"supported_capabilities\":%u,\"enabled_capabilities\":%u",
                            n->supported_capabilities, n->enabled_capabilities);
            err = httpd_resp_send_chunk(req, body, size);
        }
        if (n->cdp_capabilities_present && err == ESP_OK) {
            size = snprintf(body, sizeof(body), ",\"cdp_capabilities\":%" PRIu32, n->cdp_capabilities);
            err = httpd_resp_send_chunk(req, body, size);
        }
        if (n->duplex_present && err == ESP_OK) {
            size = snprintf(body, sizeof(body), ",\"duplex\":\"%s\"", n->full_duplex ? "full" : "half");
            err = httpd_resp_send_chunk(req, body, size);
        }
        if (n->protocol == SWITCH_PROTOCOL_CDP && err == ESP_OK) {
            size = snprintf(body, sizeof(body), ",\"cdp_version\":%u", n->cdp_version);
            err = httpd_resp_send_chunk(req, body, size);
        }
    }
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, "}", 1);
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, NULL, 0);
    return err;
}


/* Single HTTP task owns this bounded snapshot, never the HTTP stack. */
static profinet_device_t pn_http_snapshot[PN_MAX_DEVICES];
static esp_err_t pn_json_text(httpd_req_t *req,const char *key,const char *value,size_t limit,bool present){
    char escaped[1447],prefix[80];
    int n=snprintf(prefix,sizeof(prefix),",\"%s\":%s",key,present?"\"":"null");
    if(n<0||(size_t)n>=sizeof(prefix))return ESP_FAIL;
    esp_err_t err=httpd_resp_send_chunk(req,prefix,n);
    if(present&&err==ESP_OK){json_escape(escaped,sizeof(escaped),value,limit);err=httpd_resp_send_chunk(req,escaped,strlen(escaped));
        if(err==ESP_OK)err=httpd_resp_send_chunk(req,"\"",1);}
    return err;
}
static esp_err_t profinet_json(httpd_req_t *req){
    profinet_status_t p;profinet_snapshot(&p,pn_http_snapshot);char body[1536];
    const pn_counters_t *c=&p.counters;
    int size=snprintf(body,sizeof(body),
        "{\"state\":\"%s\",\"count\":%u,\"duration_ms\":%" PRIu32 ",\"packets_sent\":%" PRIu32 ","
        "\"complete\":%s,\"partial\":%s,\"xid\":%" PRIu32 ",\"physical_epoch\":%" PRIu32 ",\"revision\":%" PRIu32 ","
        "\"free_heap\":%" PRIu32 ",\"minimum_free_heap\":%" PRIu32 ",\"worker_stack_free_bytes\":%" PRIu32 ",\"tx_error\":%d,"
        "\"counters\":{\"scan_attempts\":%" PRIu32 ",\"tx_posted\":%" PRIu32 ",\"tx_accepted\":%" PRIu32 ",\"tx_failed\":%" PRIu32 ",\"tx_callback_timeout\":%" PRIu32 ","
        "\"candidates\":%" PRIu32 ",\"accepted\":%" PRIu32 ",\"parsed\":%" PRIu32 ",\"malformed\":%" PRIu32 ",\"wrong_xid\":%" PRIu32 ",\"stale\":%" PRIu32 ","
        "\"oversize\":%" PRIu32 ",\"pool_full\":%" PRIu32 ",\"queue_full\":%" PRIu32 ",\"candidate_limit\":%" PRIu32 ",\"device_limit\":%" PRIu32 ",\"duplicate_blocks\":%" PRIu32 "},\"devices\":[",
        profinet_state_name(p.state),p.count,p.duration_ms,p.packets_sent,p.state==PN_COMPLETE?"true":"false",p.partial?"true":"false",p.xid,p.physical_epoch,p.revision,
        p.free_heap,p.minimum_free_heap,p.worker_stack_free_bytes,p.tx_error,c->scan_attempts,c->tx_posted,c->tx_accepted,c->tx_failed,c->tx_callback_timeout,
        c->candidates,c->accepted,c->parsed,c->malformed,c->wrong_xid,c->stale,c->oversize,c->pool_full,c->queue_full,c->candidate_limit,c->device_limit,c->duplicate_blocks);
    if(size<0||(size_t)size>=sizeof(body))return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"PROFINET overflow");
    httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t err=httpd_resp_send_chunk(req,body,size);
    for(unsigned i=0;err==ESP_OK&&i<p.count;++i){
        const profinet_device_t *d=&pn_http_snapshot[i];
        size=snprintf(body,sizeof(body),"%s{\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"flags\":%" PRIu32 ",\"last_seen_ms\":%" PRIu32,
            i?",":"",d->mac[0],d->mac[1],d->mac[2],d->mac[3],d->mac[4],d->mac[5],d->flags,d->last_seen_ms);
        if(size<0||(size_t)size>=sizeof(body))return ESP_FAIL;
        err=httpd_resp_send_chunk(req,body,size);
        if(err==ESP_OK)err=pn_json_text(req,"station_name",d->station_name,sizeof(d->station_name),d->flags&PN_NAME);
        if(err==ESP_OK)err=pn_json_text(req,"vendor_value",d->vendor_value,sizeof(d->vendor_value),d->flags&PN_VENDOR);
        if(err==ESP_OK)err=pn_json_text(req,"alias_name",d->alias_name,sizeof(d->alias_name),d->flags&PN_ALIAS);
        if(err!=ESP_OK)break;
        if(d->flags&PN_IP){size=snprintf(body,sizeof(body),",\"ip\":\"%u.%u.%u.%u\",\"netmask\":\"%u.%u.%u.%u\",\"gateway\":\"%u.%u.%u.%u\",\"ip_block_info\":%u,\"ip_conflict\":%s",
            d->ip[0],d->ip[1],d->ip[2],d->ip[3],d->subnet[0],d->subnet[1],d->subnet[2],d->subnet[3],d->gateway[0],d->gateway[1],d->gateway[2],d->gateway[3],d->ip_block_info,d->flags&PN_IP_CONFLICT?"true":"false");}
        else size=snprintf(body,sizeof(body),",\"ip\":null,\"netmask\":null,\"gateway\":null,\"ip_block_info\":null,\"ip_conflict\":null");
        if(size<0||(size_t)size>=sizeof(body))return ESP_FAIL;
        err=httpd_resp_send_chunk(req,body,size);
        const char *keys[]={"vendor_id","device_id","device_role","device_instance"};
        const unsigned values[]={d->vendor_id,d->device_id,d->device_role,d->device_instance};
        const uint32_t flags[]={PN_IDS,PN_IDS,PN_ROLE,PN_INSTANCE};
        for(unsigned k=0;err==ESP_OK&&k<4;++k){
            if(d->flags&flags[k])size=snprintf(body,sizeof(body),",\"%s\":%u",keys[k],values[k]);
            else size=snprintf(body,sizeof(body),",\"%s\":null",keys[k]);
            if(size<0||(size_t)size>=sizeof(body))return ESP_FAIL;
            err=httpd_resp_send_chunk(req,body,size);
        }
        if(err==ESP_OK){size=snprintf(body,sizeof(body),",\"station_name_truncated\":%s,\"vendor_value_truncated\":%s,\"alias_name_truncated\":%s,\"station_name_sanitized\":%s,\"vendor_value_sanitized\":%s,\"alias_name_sanitized\":%s}",
            d->flags&PN_NAME_TRUNC?"true":"false",d->flags&PN_VENDOR_TRUNC?"true":"false",d->flags&PN_ALIAS_TRUNC?"true":"false",
            d->flags&PN_NAME_SAN?"true":"false",d->flags&PN_VENDOR_SAN?"true":"false",d->flags&PN_ALIAS_SAN?"true":"false");
            if(size<0||(size_t)size>=sizeof(body))return ESP_FAIL;
            err=httpd_resp_send_chunk(req,body,size);}
    }
    if(err==ESP_OK)err=httpd_resp_send_chunk(req,"]}",2);
    if(err==ESP_OK){err=httpd_resp_send_chunk(req,NULL,0);}
    return err;
}

esp_err_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 12288; /* 4 KiB stream buffer plus ESP-IDF image verification. */
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 5;
    config.lru_purge_enable = true;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) return err;
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = index_page},
        {.uri = "/update", .method = HTTP_GET, .handler = index_page},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_json},
        {.uri = "/api/autotest", .method = HTTP_GET, .handler = autotest_json},
        {.uri = "/api/discovery", .method = HTTP_GET, .handler = discovery_json},
        {.uri = "/api/switch-info", .method = HTTP_GET, .handler = switch_info_json},
        {.uri = "/api/profinet", .method = HTTP_GET, .handler = profinet_json},
        {.uri = "/api/firmware", .method = HTTP_POST, .handler = firmware_update_upload},
    };
    for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) {
            httpd_stop(server);
            server = NULL;
            return err;
        }
    }
    return ESP_OK;
}
