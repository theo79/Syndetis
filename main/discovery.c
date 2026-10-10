#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "ping/ping_sock.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "network_status.h"
#include "firmware_update.h"
#include "diagnostic.h"
#include "discovery.h"
#include "oui_vendor.h"
#include "discovery_name.h"
#include "esp_system.h"

#define PROBES 4
#define TIMEOUT_MS 250
#define ARP_STEP_MS DISCOVERY_ARP_STEP_MS
#define ARP_SETTLE_MS DISCOVERY_ARP_SETTLE_MS
static SemaphoreHandle_t arp_done;
static portMUX_TYPE state_lock=portMUX_INITIALIZER_UNLOCKED;
static uint32_t run_token;
static uint32_t requested_revision;
static TaskHandle_t worker;
static volatile bool ready;
static discovery_status_t state;
static discovery_device_t devices[DISCOVERY_MAX_HOSTS];
/* Permanent storage: callbacks may finish after a bounded worker wait. */
static struct probe {
    SemaphoreHandle_t done;
    esp_ping_handle_t handle;
    discovery_device_t device;
    uint32_t revision, token;
} probes[PROBES];
static struct {
    bool pending;
    uint32_t revision, token;
    int index;
    bool reset;
    uint32_t target;
    err_t result;
    discovery_subnet_t subnet;
    bool issued[DISCOVERY_MAX_HOSTS];
} arp_job;

const char *discovery_state_name(discovery_state_t s)
{ static const char *names[] = {"idle", "running", "complete", "error"}; return s <= DISC_ERROR ? names[s] : "error"; }
void discovery_snapshot(discovery_status_t *s, discovery_device_t *out)
{
    if (!ready) { memset(s, 0, sizeof(*s)); return; }
    network_status_t network; network_status_get(&network);
    portENTER_CRITICAL(&state_lock);
    *s = state;
    if (s->state==DISC_COMPLETE && (!network.link_up || network.revision!=requested_revision)) {
        s->state=DISC_ERROR; s->found=0;
        snprintf(s->error,sizeof(s->error),"ABORTED_NETWORK_CHANGE");
    }
    if (out) memcpy(out, devices, s->found * sizeof(*out));
    portEXIT_CRITICAL(&state_lock);
}
void discovery_get(discovery_status_t *s) { discovery_snapshot(s, NULL); }
bool discovery_device(unsigned index, discovery_device_t *out)
{
    if (!ready || !out) return false;
    network_status_t network; network_status_get(&network);
    portENTER_CRITICAL(&state_lock);
    bool valid = index < state.found && network.link_up && network.revision==requested_revision;
    if (valid) *out = devices[index];
    portEXIT_CRITICAL(&state_lock);
    return valid;
}
bool discovery_hostname_result(uint32_t ip, const char *name, uint32_t token, uint32_t revision)
{
    network_status_t n; network_status_get(&n);
    bool active=diagnostic_current(token);
    portENTER_CRITICAL(&state_lock);
    bool accepted=discovery_name_apply(devices,state.found,ip,name,token,run_token,
        revision,n.revision,n.link_up && n.ipv4_ready,active && state.state==DISC_COMPLETE);
    if (accepted) ++state.hostname.resolved;
    portEXIT_CRITICAL(&state_lock);
    return accepted;
}
static bool interrupted(uint32_t revision)
{
    network_status_t n;
    ota_status_t ota;
    network_status_get(&n);
    firmware_update_status(&ota);
    return !diagnostic_accept(1,1,revision,n.revision,n.link_up) || ota.phase == OTA_RECEIVING || ota.phase == OTA_SUCCESS;
}
static const char *cancel_reason(void)
{
    network_status_t n;
    ota_status_t ota;
    network_status_get(&n);
    firmware_update_status(&ota);
    if (!n.link_up) return "ABORTED_LINK_DOWN";
    if (ota.phase==OTA_RECEIVING || ota.phase==OTA_SUCCESS) return "ABORTED_OTA";
    return "ABORTED_NETWORK_CHANGE";
}
static void success(esp_ping_handle_t h, void *arg)
{
    struct probe *p = arg;
    if (!diagnostic_current(p->token) || interrupted(p->revision)) return;
    ip_addr_t reply;
    esp_ping_get_profile(h, ESP_PING_PROF_IPADDR, &reply, sizeof(reply));
    /* Accept only the requested IPv4 source, not an unrelated raw-socket packet. */
    if (IP_IS_V4(&reply) && lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(&reply))) == p->device.ip) {
        p->device.icmp_responsive = true;
        esp_ping_get_profile(h, ESP_PING_PROF_TIMEGAP, &p->device.rtt_ms, sizeof(p->device.rtt_ms));
    }
}
static void ended(esp_ping_handle_t h, void *arg)
{ struct probe *p = arg; xSemaphoreGive(p->done); }

/* TCP/IP-thread only. Reset old cache once, then copy stable observations every
 * paced request. Raw etharp_request does not allocate pending cache entries. */
static void lookup_arp(void *arg)
{
    struct netif *netif = diagnostic_current(arp_job.token) && !interrupted(arp_job.revision) ? netif_get_by_index((u8_t)arp_job.index) : NULL;
    arp_job.result=ERR_IF;
    if (netif) {
        if (arp_job.reset) etharp_cleanup_netif(netif);
        for (size_t i=0; i<ARP_TABLE_SIZE; ++i) {
            ip4_addr_t *ip;
            struct netif *owner;
            struct eth_addr *mac;
            if (!etharp_get_entry(i,&ip,&owner,&mac) || owner!=netif) continue;
            uint32_t host=lwip_ntohl(ip4_addr_get_u32(ip));
            if (!discovery_observation_allowed(&arp_job.subnet,arp_job.issued,host)) continue;
            portENTER_CRITICAL(&state_lock);
            discovery_observe(devices,&state.found,host,mac->addr);
            for (unsigned j=0;j<state.found;++j) if (devices[j].ip==host) {
                devices[j].last_seen_ms=esp_timer_get_time()/1000; break;
            }
            portEXIT_CRITICAL(&state_lock);
        }
        arp_job.result=ERR_OK;
        if (arp_job.target) {
            ip4_addr_t ip;
            ip4_addr_set_u32(&ip,lwip_htonl(arp_job.target));
            arp_job.result=etharp_request(netif,&ip);
            if (arp_job.result==ERR_OK) arp_job.issued[arp_job.target-arp_job.subnet.first]=true;
        }
    }
    xSemaphoreGive(arp_done);
}
static const char *arp_step(uint32_t revision, uint32_t target, bool reset)
{
    arp_job.target=target; arp_job.reset=reset; arp_job.pending=true;
    if (tcpip_try_callback(lookup_arp,NULL)!=ERR_OK) {
        arp_job.pending=false; return "ARP callback queue unavailable";
    }
    int64_t deadline=esp_timer_get_time()+1000000;
    while (esp_timer_get_time()<deadline) {
        if (xSemaphoreTake(arp_done,pdMS_TO_TICKS(10))==pdTRUE) {
            arp_job.pending=false;
            if (interrupted(revision)) return cancel_reason();
            return arp_job.result==ERR_OK ? NULL : "ARP request failed";
        }
        if (interrupted(revision)) return cancel_reason();
    }
    return "ARP callback timeout";
}
static bool pause_checked(uint32_t revision, unsigned ms)
{
    int64_t deadline=esp_timer_get_time()+(int64_t)ms*1000;
    while (esp_timer_get_time()<deadline) {
        if (interrupted(revision)) return false;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return !interrupted(revision);
}
static int compare_device(const void *a,const void *b)
{ return discovery_ip_compare(((const discovery_device_t *)a)->ip,((const discovery_device_t *)b)->ip); }
static void finish(const char *error)
{
    uint32_t finished_ms=esp_timer_get_time()/1000;
    portENTER_CRITICAL(&state_lock);
    state.state = error ? DISC_ERROR : DISC_COMPLETE;
    state.completion_uptime_ms = finished_ms;
    state.duration_ms = state.completion_uptime_ms - state.start_uptime_ms;
    if (error) {
        strncpy(state.error,error,sizeof(state.error)-1);
        state.error[sizeof(state.error)-1]=0;
        state.found = 0; /* Never publish an aborted scan as a successful result. */
    }
    portEXIT_CRITICAL(&state_lock);
    ESP_LOGI("DISCOVERY","completed %lu hosts in %lu ms",(unsigned long)state.found,(unsigned long)state.duration_ms);
    ESP_LOGI("DISCOVERY","free heap %lu; worker stack watermark %u bytes",(unsigned long)esp_get_free_heap_size(),(unsigned)uxTaskGetStackHighWaterMark(NULL));
    if (!error) {
        uint32_t token=run_token, revision=requested_revision;
        portENTER_CRITICAL(&state_lock);
        state.hostname.enriching=state.found!=0;
        unsigned count=state.found;
        portEXIT_CRITICAL(&state_lock);
        discovery_hostname_status_t names=discovery_names_run(token,revision,count);
        portENTER_CRITICAL(&state_lock);
        if (run_token==token) state.hostname=names;
        portEXIT_CRITICAL(&state_lock);
        ESP_LOGI("DISCOVERY","hostname enrichment %lu attempted, %lu resolved, %lu timeout, %lu failed in %lu ms%s",
            (unsigned long)names.attempted,(unsigned long)names.resolved,(unsigned long)names.timeout,
            (unsigned long)names.failed,(unsigned long)names.duration_ms,names.cancelled ? "; cancelled":"");
        ESP_LOGI("DISCOVERY","hostname free heap %lu; minimum free heap %lu; worker stack watermark %u bytes",
            (unsigned long)esp_get_free_heap_size(),(unsigned long)esp_get_minimum_free_heap_size(),(unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
    diagnostic_release(run_token);
    bool pending=arp_job.pending;
    for (unsigned i=0;i<PROBES;++i) pending=pending || probes[i].handle!=NULL;
    ESP_LOGI("DISCOVERY", "%s; coordinator released%s",error ? error : "Scan complete",pending ? "; late callback storage quarantined" : "; sessions retired");
}
static void scan(void)
{
    int64_t scan_deadline=esp_timer_get_time()+120000000;
    network_status_t n;
    network_status_get(&n);
    if (n.revision!=requested_revision || !n.link_up) { finish(cancel_reason());return; }
    discovery_subnet_t subnet = {0};
    bool valid = discovery_subnet(lwip_ntohl(n.ipv4.ip.addr), lwip_ntohl(n.ipv4.netmask.addr), &subnet);
    portENTER_CRITICAL(&state_lock);
    state.subnet = subnet;
    portEXIT_CRITICAL(&state_lock);
    if (!n.link_up) { finish("Ethernet link down"); return; }
    if (!valid) { finish("No valid IPv4 configuration"); return; }
    if (!discovery_allowed(&subnet)) { finish("Network too large: active discovery limited to 254 hosts (/24 max)"); return; }
    esp_netif_t *eth = esp_netif_get_handle_from_ifkey("ETH_DEF");
    int index = eth ? esp_netif_get_netif_impl_index(eth) : -1;
    if (index <= 0 || index > 255) { finish("Ethernet interface unavailable"); return; }
    /* Reap late jobs without reusing their storage while callbacks are pending. */
    if (arp_job.pending) {
        if (xSemaphoreTake(arp_done, 0) != pdTRUE) { finish("ARP callback still pending"); return; }
        arp_job.pending = false;
    }
    bool reaped = false, icmp_busy = false;
    for (unsigned i=0; i<PROBES; ++i) if (probes[i].handle) {
        if (xSemaphoreTake(probes[i].done, 0) != pdTRUE) { icmp_busy=true; continue; }
        esp_ping_delete_session(probes[i].handle);
        probes[i].handle = NULL;
        reaped = true;
    }
    if (reaped && !pause_checked(n.revision,1100)) { finish(cancel_reason()); return; }
    arp_job.index=index; arp_job.revision=n.revision; arp_job.token=run_token;
    arp_job.subnet=subnet;
    memset(arp_job.issued,0,sizeof(arp_job.issued));
    const char *arp_error=arp_step(n.revision,0,true);
    if (arp_error) { finish(arp_error); return; }
    int64_t phase_start=esp_timer_get_time();
    for (uint32_t target=subnet.first; subnet.candidates && target<=subnet.last; ++target) {
        if (esp_timer_get_time()>=scan_deadline) { finish("Scan time limit reached (120 seconds)"); return; }
        if (!discovery_candidate(&subnet,target)) continue;
        arp_error=arp_step(n.revision,target,false);
        if (arp_error) { finish(arp_error); return; }
        portENTER_CRITICAL(&state_lock);
        ++state.processed;
        portEXIT_CRITICAL(&state_lock);
        /* Pace from callback completion, so delayed queue service cannot
         * compress two on-wire requests into a catch-up burst. Replies remain
         * pipelined; this is emission pacing, not a per-target reply wait. */
        if (!pause_checked(n.revision,ARP_STEP_MS)) { finish(cancel_reason()); return; }
    }
    ESP_LOGI("DISCOVERY","ARP sweep %lld ms",(esp_timer_get_time()-phase_start)/1000);
    phase_start=esp_timer_get_time();
    while (esp_timer_get_time()-phase_start<ARP_SETTLE_MS*1000) {
        arp_error=arp_step(n.revision,0,false);
        if (arp_error) { finish(arp_error); return; }
        if (!pause_checked(n.revision,ARP_STEP_MS)) { finish(cancel_reason()); return; }
    }
    arp_error=arp_step(n.revision,0,false);
    if (arp_error) { finish(arp_error); return; }
    ESP_LOGI("DISCOVERY","ARP settle %lld ms",(esp_timer_get_time()-phase_start)/1000);
    phase_start=esp_timer_get_time();
    portENTER_CRITICAL(&state_lock);
    qsort(devices,state.found,sizeof(devices[0]),compare_device);
    unsigned hosts=state.found;
    state.enriching=true;
    portEXIT_CRITICAL(&state_lock);
    int64_t vendor_start=esp_timer_get_time();
    for (unsigned i=0;i<hosts;++i) {
        if (interrupted(n.revision)) { finish(cancel_reason()); return; }
        discovery_device_t enriched;
        portENTER_CRITICAL(&state_lock);
        enriched=devices[i];
        portEXIT_CRITICAL(&state_lock);
        enriched.vendor_present=enriched.mac_valid && oui_lookup_vendor(enriched.mac,enriched.vendor,sizeof(enriched.vendor));
        portENTER_CRITICAL(&state_lock);
        devices[i]=enriched;
        portEXIT_CRITICAL(&state_lock);
    }
    ESP_LOGI("DISCOVERY","vendor lookup %lld us for %u hosts",esp_timer_get_time()-vendor_start,hosts);
    unsigned processed = 0;
    const char *icmp_error=icmp_busy ? "Ping cleanup still pending" : NULL;
    bool retired=false;
    while (processed < hosts) {
        if (icmp_error) break; /* Do not retry a failing engine for every host. */
        if (interrupted(n.revision)) { finish(cancel_reason()); return; }
        if (esp_timer_get_time()>=scan_deadline) { icmp_error="ICMP time limit reached"; break; }
        unsigned count=0;
        bool completed[PROBES]={0};
        const char *error = NULL;
        int64_t deadline = esp_timer_get_time() + 1500000;
        while (count<PROBES && processed+count<hosts) {
            uint32_t target = devices[processed+count].ip;
            struct probe *p = &probes[count];
            p->device = devices[processed+count];
            p->revision=n.revision; p->token=run_token;
            esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
            cfg.count=1; cfg.timeout_ms=TIMEOUT_MS; cfg.interval_ms=0;
            cfg.data_size=8; cfg.task_stack_size=3072; cfg.interface=index;
            ip_addr_set_ip4_u32(&cfg.target_addr, lwip_htonl(target));
            esp_ping_callbacks_t cb = {.cb_args=p, .on_ping_success=success, .on_ping_end=ended};
            p->handle=NULL;
            esp_err_t setup=esp_ping_new_session(&cfg, &cb, &p->handle);
            if (setup != ESP_OK) {
                p->handle=NULL; /* SDK only publishes a handle on success. */
                portENTER_CRITICAL(&state_lock); ++state.icmp_setup_failures; portEXIT_CRITICAL(&state_lock);
                error="Ping session creation failed"; break;
            }
            if (esp_ping_start(p->handle) != ESP_OK) {
                esp_ping_delete_session(p->handle); p->handle=NULL; retired=true;
                portENTER_CRITICAL(&state_lock); ++state.icmp_setup_failures; portEXIT_CRITICAL(&state_lock);
                error="Ping session start failed"; break;
            }
            ++count;
        }
        for (unsigned i=0; i<count; ++i) {
            struct probe *p=&probes[i];
            bool done=xSemaphoreTake(p->done,0)==pdTRUE;
            while (!done && esp_timer_get_time()<deadline) {
                if (xSemaphoreTake(p->done, pdMS_TO_TICKS(50)) == pdTRUE) { done=true; break; }
                if (interrupted(n.revision)) { error=cancel_reason(); break; }
            }
            if (!done) {
                /* One stop per live session; quarantine until its end callback. */
                esp_ping_stop(p->handle);
                done=xSemaphoreTake(p->done, pdMS_TO_TICKS(500)) == pdTRUE;
                error=error ? error : "Ping completion timeout";
            }
            if (done) {
                uint32_t sent=0;
                p->device.ping_measured=esp_ping_get_profile(p->handle,ESP_PING_PROF_REQUEST,&sent,sizeof(sent))==ESP_OK && sent>0;
                if (p->device.ping_measured) p->device.packet_loss=p->device.icmp_responsive ? 0 : 100;
                else { p->device.icmp_responsive=false; p->device.rtt_ms=0; p->device.packet_loss=0; if (!error) error="Ping transmitted no probes"; }
                completed[i]=true;
                esp_ping_delete_session(p->handle); p->handle=NULL; retired=true;
            }
        }
        /* Publish each completed session even when a later setup fails. Never
         * copy callback-owned records from still-pending sessions. */
        if (interrupted(n.revision)) { finish(cancel_reason()); return; }
        portENTER_CRITICAL(&state_lock);
        for (unsigned i=0; i<count; ++i) if (completed[i]) {
            devices[processed+i]=probes[i].device;
            if (devices[processed+i].ping_measured) ++state.icmp_measured;
        }
        processed+=count;
        state.checked=processed;
        portEXIT_CRITICAL(&state_lock);
        /* Preserve the proven 1000-ms asynchronous ping-task retirement margin. */
        if (retired && !pause_checked(n.revision,1100)) { finish(cancel_reason()); return; }
        retired=false;
        if (error) { icmp_error=error; break; }
    }
    portENTER_CRITICAL(&state_lock);
    state.icmp_unmeasured=state.found-state.icmp_measured;
    portEXIT_CRITICAL(&state_lock);
    if (icmp_error) ESP_LOGW("DISCOVERY","ICMP unavailable (%s); preserving %u ARP hosts; hostname enrichment continuing",icmp_error,hosts);
    ESP_LOGI("DISCOVERY","ICMP enrichment %lld ms",(esp_timer_get_time()-phase_start)/1000);
    if (interrupted(n.revision)) finish(cancel_reason());
    else finish(NULL); /* Results sorted numerically before enrichment. */
}
static void task(void *arg)
{
    for (;;) {
        /* Idle request wait only, with no state lock or reservation held. */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        scan();
    }
}
esp_err_t discovery_start(void)
{
    if (!ready) return ESP_ERR_INVALID_STATE;
    network_status_t n;
    network_status_get(&n);
    if (!n.link_up) return ESP_ERR_INVALID_STATE;
    uint32_t token=diagnostic_acquire();
    if (!token) return ESP_ERR_INVALID_STATE;
    run_token=token;
    requested_revision=n.revision;
    uint32_t started_ms=esp_timer_get_time()/1000;
    portENTER_CRITICAL(&state_lock);
    state=(discovery_status_t){.state=DISC_RUNNING, .start_uptime_ms=started_ms};
    portEXIT_CRITICAL(&state_lock);
    xTaskNotifyGive(worker);
    return ESP_OK;
}
esp_err_t discovery_init(void)
{
    if (ready) return ESP_ERR_INVALID_STATE;
    arp_done=xSemaphoreCreateBinary();
    bool ok=arp_done;
    for (unsigned i=0; i<PROBES; ++i) { probes[i].done=xSemaphoreCreateBinary(); ok=ok && probes[i].done; }
    if (ok && xTaskCreate(task, "discovery", 4096, NULL, 2, &worker)==pdPASS) { ready=true; return ESP_OK; }
    for (unsigned i=0; i<PROBES; ++i) if (probes[i].done) { vSemaphoreDelete(probes[i].done); probes[i].done=NULL; }
    if (arp_done) { vSemaphoreDelete(arp_done); arp_done=NULL; }
    return ESP_ERR_NO_MEM;
}
