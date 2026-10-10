#include "profinet.h"
#include "ui_profinet.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "network_status.h"
#include "firmware_update.h"
#include "ui_oled.h"
#include "ui_menu.h"
#include "autotest.h"
#include "discovery.h"
#include "discovery_name.h"
#include "switch_info.h"
#include "ui_switch_info.h"

static QueueHandle_t button_queue;
static bool display_available;
static const char *main_items[] = {"AutoTest", "Discover", "Switch Info", "Network Tools", "Industrial", "System"};
static const char *system_items[] = {"Status", "About", "Firmware Update", "Back"};
typedef enum { MAIN, SYSTEM, PLACEHOLDER, STATUS, ABOUT, CONFIRM_UPDATE, UPDATE, TEST_READY, TEST_RUNNING, TEST_RESULTS,
               DISC_READY, DISC_SCAN, DISC_RESULTS, DISC_LIST, DISC_DEVICE, SWITCH_INFO, INDUSTRIAL, PN_MENU, PN_SCAN, PN_RESULTS, PN_DEVICE } screen_t;
/* Owned only by the UI task. */
static screen_t screen;
static unsigned main_selection, system_selection;
static unsigned last_ota_revision;
static unsigned test_page;
static bool test_start_failed;
static bool discovery_start_failed;
static unsigned device_selection;
static unsigned switch_page;
static unsigned industrial_selection, pn_selection, pn_device_index, pn_page;
static pn_start_t pn_start_result;
static bool pn_back_pending;
static profinet_device_t pn_device_scratch;
/* UI-task-owned copy avoids adding a large snapshot to the 4 KiB task stack. */
static switch_info_status_t switch_snapshot;

void ui_menu_button(ui_button_t button, void *context)
{
    if (button_queue) xQueueSend(button_queue, &button, 0);
}

static void navigate(ui_button_t button, const ota_status_t *ota)
{
    if (ota->phase == OTA_RECEIVING || ota->phase == OTA_SUCCESS) return;
    if (screen == MAIN || screen == SYSTEM) {
        unsigned count = screen == MAIN ? 6 : 4;
        unsigned *selection = screen == MAIN ? &main_selection : &system_selection;
        if (button == UI_BUTTON_UP) *selection = (*selection + count - 1) % count;
        if (button == UI_BUTTON_DOWN) *selection = (*selection + 1) % count;
        if (button == UI_BUTTON_OK) {
            if (screen == MAIN) {
                screen = main_selection == 5 ? SYSTEM : main_selection == 0 ? TEST_READY : main_selection == 1 ? DISC_READY : main_selection == 2 ? SWITCH_INFO : main_selection == 4 ? INDUSTRIAL : PLACEHOLDER;
                if (screen == SWITCH_INFO) switch_page = 0;
                discovery_start_failed = false;
            }
            else {
                const screen_t pages[] = {STATUS, ABOUT, CONFIRM_UPDATE, MAIN};
                screen = pages[system_selection];
            }
        }
    } else if (screen == INDUSTRIAL || screen == PN_MENU) {
        unsigned count=screen==INDUSTRIAL ? 2 : 3;
        unsigned *selection=screen==INDUSTRIAL ? &industrial_selection : &pn_selection;
        if(button==UI_BUTTON_UP)*selection=(*selection+count-1)%count;
        if(button==UI_BUTTON_DOWN)*selection=(*selection+1)%count;
        if(button==UI_BUTTON_OK){
            if(screen==INDUSTRIAL)screen=industrial_selection ? MAIN : PN_MENU;
            else if(pn_selection==2)screen=INDUSTRIAL;
            else if(pn_selection==1)screen=PN_RESULTS;
            else {pn_start_result=profinet_start();if(pn_start_result==PN_START_OK){screen=PN_SCAN;pn_back_pending=false;}}
        }
    } else if (screen == PN_SCAN) {
        if(button==UI_BUTTON_OK){profinet_cancel();pn_back_pending=true;}
    } else if (screen == PN_RESULTS) {
        profinet_status_t p;profinet_get(&p);
        if(button==UI_BUTTON_OK&&p.state==PN_COMPLETE&&p.count){pn_device_index=pn_page=0;screen=PN_DEVICE;}
        else screen=PN_MENU;
    } else if (screen == PN_DEVICE) {
        profinet_status_t p;profinet_get(&p);
        if(button==UI_BUTTON_OK||!p.count)screen=PN_RESULTS;
        else {unsigned flat=pn_device_index*3+pn_page,total=p.count*3;
            flat=(flat+total+(button==UI_BUTTON_UP ? -1 : 1))%total;pn_device_index=flat/3;pn_page=flat%3;}
    } else if (screen == SWITCH_INFO) {
        if (button == UI_BUTTON_UP) switch_page = ui_switch_info_page(switch_page, -1);
        if (button == UI_BUTTON_DOWN) switch_page = ui_switch_info_page(switch_page, 1);
        if (button == UI_BUTTON_OK) screen = MAIN;
    } else if (screen == DISC_READY) {
        if (button != UI_BUTTON_OK) screen = MAIN;
        else {
            discovery_status_t d;
            network_status_t n;
            discovery_subnet_t subnet;
            discovery_get(&d); network_status_get(&n);
            if (d.state == DISC_RUNNING) screen = DISC_SCAN;
            else if (!n.link_up || !discovery_subnet(esp_netif_htonl(n.ipv4.ip.addr), esp_netif_htonl(n.ipv4.netmask.addr), &subnet) || !discovery_allowed(&subnet)) screen = MAIN;
            else if (discovery_start() == ESP_OK) screen = DISC_SCAN;
            else discovery_start_failed = true;
        }
    } else if (screen == DISC_SCAN) {
        if (button == UI_BUTTON_OK) screen = MAIN;
    } else if (screen == DISC_RESULTS) {
        discovery_status_t d; discovery_get(&d);
        if (button == UI_BUTTON_OK && d.state == DISC_COMPLETE) { device_selection=0; screen=DISC_LIST; }
        else screen=MAIN;
    } else if (screen == DISC_LIST) {
        discovery_status_t d; discovery_get(&d);
        unsigned count=d.found+1; /* Last row is always Back, including zero devices. */
        if (button == UI_BUTTON_UP) device_selection=(device_selection+count-1)%count;
        if (button == UI_BUTTON_DOWN) device_selection=(device_selection+1)%count;
        if (button == UI_BUTTON_OK) screen=device_selection==d.found ? DISC_RESULTS : DISC_DEVICE;
    } else if (screen == DISC_DEVICE) {
        discovery_status_t d; discovery_get(&d);
        if (button == UI_BUTTON_OK || !d.found) screen=DISC_LIST;
        else if (button == UI_BUTTON_UP) device_selection=(device_selection+d.found-1)%d.found;
        else if (button == UI_BUTTON_DOWN) device_selection=(device_selection+1)%d.found;
    } else if (screen == TEST_READY) {
        if (button == UI_BUTTON_OK) {
            autotest_status_t test;
            autotest_get(&test);
            test_start_failed = false;
            if (test.running || autotest_start() == ESP_OK) screen = TEST_RUNNING;
            else test_start_failed = true;
        } else screen = MAIN;
    } else if (screen == TEST_RUNNING) {
        if (button == UI_BUTTON_OK) screen = MAIN;
    } else if (screen == TEST_RESULTS) {
        if (button == UI_BUTTON_UP) test_page = (test_page + 6) % 7;
        if (button == UI_BUTTON_DOWN) test_page = (test_page + 1) % 7;
        if (button == UI_BUTTON_OK) screen = MAIN;
    } else if (screen == CONFIRM_UPDATE) {
        /* Explicit second OK, never the same event that entered the page. */
        if (button == UI_BUTTON_OK && display_available && firmware_update_authorize()) screen = UPDATE;
        else screen = SYSTEM;
    } else {
        screen = screen == PLACEHOLDER ? MAIN : SYSTEM;
    }
}

static void render(char rows[8][22], const network_status_t *net, const ota_status_t *ota)
{
    char ip[16];
    snprintf(ip, sizeof(ip), IPSTR, IP2STR(&net->ipv4.ip));
    memset(rows, 0, 8 * 22);
    switch (screen) {
    case INDUSTRIAL:
        snprintf(rows[0],22,"INDUSTRIAL");snprintf(rows[2],22,"%c PROFINET",industrial_selection==0?'>':' ');snprintf(rows[3],22,"%c Back",industrial_selection==1?'>':' ');break;
    case PN_MENU: {
        const char *items[]={"Scan","Results","Back"};snprintf(rows[0],22,"PROFINET");snprintf(rows[1],22,"Identify only");
        for(unsigned i=0;i<3;++i)snprintf(rows[2+i],22,"%c %s",pn_selection==i?'>':' ',items[i]);
        const char *errors[]={"Ready","Unavailable","Link down","Diagnostic busy","Cooldown: wait 10s"};
        snprintf(rows[5],22,"%.21s",net->link_up?errors[pn_start_result]:"Link down");break;
    }
    case PN_SCAN:
    case PN_RESULTS: {
        profinet_status_t p;profinet_get(&p);snprintf(rows[0],22,"PROFINET");snprintf(rows[1],22,"Identify only");
        const char *states[]={"Ready","Collecting replies","Complete","Cancelled","Network changed","TX error"};
        snprintf(rows[2],22,"%.21s",pn_back_pending&&p.state==PN_RUNNING?"Cancelling...":states[p.state]);
        if(p.state==PN_COMPLETE)snprintf(rows[3],22,p.count?"Devices: %u":"No replies observed",p.count);
        if(p.partial)snprintf(rows[4],22,"PARTIAL: replies lost");
        if(p.state==PN_NETWORK_CHANGED)snprintf(rows[4],22,"Scan again");
        snprintf(rows[7],22,p.state==PN_RUNNING?"OK: CANCEL":p.count?"OK: DEVICES":"OK: BACK");break;
    }
    case PN_DEVICE: {
        profinet_status_t p;profinet_get(&p);
        if(profinet_device(pn_device_index,&pn_device_scratch))ui_profinet_render(rows,pn_page,pn_device_index,p.count,&pn_device_scratch);
        else {snprintf(rows[0],22,"PROFINET");snprintf(rows[2],22,"Network changed");snprintf(rows[7],22,"OK: BACK");}break;
    }
    case SWITCH_INFO:
        switch_info_get(&switch_snapshot);
        if (!switch_snapshot.available || !switch_snapshot.neighbor.valid) switch_page = 0;
        ui_switch_info_render(rows, switch_page, switch_snapshot.available,
                              switch_snapshot.setup_error, &switch_snapshot.neighbor);
        break;
    case DISC_READY: {
        discovery_subnet_t s;
        snprintf(rows[0],22,"DISCOVER");
        bool valid=net->link_up && discovery_subnet(esp_netif_htonl(net->ipv4.ip.addr),esp_netif_htonl(net->ipv4.netmask.addr),&s);
        if (!valid) snprintf(rows[2],22,net->link_up ? "No IPv4" : "Ethernet link down");
        else if (!discovery_allowed(&s)) { snprintf(rows[2],22,"Network too large"); snprintf(rows[3],22,"/24 max"); }
        else {
            esp_ip4_addr_t network={.addr=esp_netif_htonl(s.network)};
            snprintf(rows[2],22,IPSTR "/%u",IP2STR(&network),s.prefix);
            snprintf(rows[3],22,"Up to %u hosts",(unsigned)s.candidates);
            if (discovery_start_failed) snprintf(rows[5],22,"Diagnostic busy/error");
            snprintf(rows[6],22,"UP/DOWN: BACK");
        }
        snprintf(rows[7],22,valid && discovery_allowed(&s) ? "OK: START" : "OK: BACK");
        break;
    }
    case DISC_SCAN:
    case DISC_RESULTS: {
        discovery_status_t d; discovery_get(&d);
        snprintf(rows[0],22,"DISCOVER");
        if (screen == DISC_SCAN) {
            snprintf(rows[2],22,d.enriching ? "Checking hosts..." : "Scanning...");
            snprintf(rows[3],22,"%u / %u",(unsigned)(d.enriching ? d.checked : d.processed),(unsigned)(d.enriching ? d.found : d.subnet.candidates));
            snprintf(rows[5],22,"Found: %u",(unsigned)d.found);
            snprintf(rows[7],22,"OK: BACK");
        } else if (d.state == DISC_COMPLETE) {
            snprintf(rows[2],22,"Found: %u",(unsigned)d.found);
            snprintf(rows[3],22,"Time: %u.%us",(unsigned)(d.duration_ms/1000),(unsigned)(d.duration_ms%1000/100));
            snprintf(rows[6],22,"UP/DOWN: BACK");
            snprintf(rows[7],22,"OK: DEVICES");
        } else {
            for (unsigned i=0; i<4 && i*21<strlen(d.error); ++i) snprintf(rows[2+i],22,"%.21s",d.error+i*21);
            snprintf(rows[7],22,"OK: BACK");
        }
        break;
    }
    case DISC_LIST: {
        discovery_status_t d; discovery_get(&d);
        snprintf(rows[0],22,"DEVICES %u",(unsigned)d.found);
        unsigned first=device_selection<5 ? 0 : device_selection-4;
        for (unsigned i=0; i<5 && first+i<=d.found; ++i) {
            unsigned index=first+i; char marker=index==device_selection ? '>' : ' ';
            discovery_device_t host;
            if (index==d.found) snprintf(rows[2+i],22,"%c Back",marker);
            else if (discovery_device(index,&host)) {
                esp_ip4_addr_t ip={.addr=esp_netif_htonl(host.ip)};
                snprintf(rows[2+i],22,"%c " IPSTR,marker,IP2STR(&ip));
            }
        }
        snprintf(rows[7],22,"UP/DOWN OK: SELECT");
        break;
    }
    case DISC_DEVICE: {
        discovery_device_t d;
        discovery_status_t status; discovery_get(&status);
        snprintf(rows[0],22,"DEVICE %u/%u",device_selection+1,(unsigned)status.found);
        if (discovery_device(device_selection,&d)) {
            esp_ip4_addr_t ip={.addr=esp_netif_htonl(d.ip)};
            char name[64];
            if (d.hostname_present && discovery_name_valid(d.hostname,name)) {
                snprintf(rows[1],22,"%.21s",name);
                snprintf(rows[2],22,IPSTR,IP2STR(&ip));
            } else {
                snprintf(rows[1],22,IPSTR,IP2STR(&ip));
                snprintf(rows[2],22,"MAC:");
            }
            if (d.mac_valid) snprintf(rows[3],22,"%02X:%02X:%02X:%02X:%02X:%02X",d.mac[0],d.mac[1],d.mac[2],d.mac[3],d.mac[4],d.mac[5]);
            else snprintf(rows[3],22,"Unknown");
            snprintf(rows[4],22,"%.21s",d.locally_administered ? "Local/Private" : d.vendor_present ? d.vendor : "Unknown");
            if (d.icmp_responsive) snprintf(rows[5],22,"RTT: %u ms",(unsigned)d.rtt_ms);
            else snprintf(rows[5],22,d.ping_measured ? "ICMP: NO REPLY" : "ICMP: N/A");
        }
        snprintf(rows[6],22,"UP/DOWN: DEVICE");
        snprintf(rows[7],22,"OK: BACK");
        break;
    }
    case MAIN: {
        snprintf(rows[0], 22, "SYNDETIS");
        unsigned first = main_selection < 5 ? 0 : main_selection - 4;
        for (unsigned i = 0; i < 5; ++i) {
            unsigned item = first + i;
            snprintf(rows[2 + i], 22, "%c %s", item == main_selection ? '>' : ' ', main_items[item]);
        }
        snprintf(rows[7], 22, "UP/DOWN  OK: SELECT");
        break;
    }
    case SYSTEM:
        snprintf(rows[0], 22, "SYSTEM");
        for (unsigned i = 0; i < 4; ++i) snprintf(rows[2 + i], 22, "%c %s", i == system_selection ? '>' : ' ', system_items[i]);
        break;
    case PLACEHOLDER:
        snprintf(rows[0], 22, "%s", main_items[main_selection]);
        snprintf(rows[2], 22, "Coming soon");
        snprintf(rows[7], 22, "OK: BACK");
        break;
    case STATUS:
        snprintf(rows[0], 22, "STATUS");
        snprintf(rows[2], 22, "ETH: %s", net->link_up ? "UP" : "DOWN");
        snprintf(rows[3], 22, "IP:");
        snprintf(rows[4], 22, "%s", net->ipv4.ip.addr ? ip : "-");
        snprintf(rows[7], 22, "OK: BACK");
        break;
    case ABOUT:
        snprintf(rows[0], 22, "SYNDETIS");
        snprintf(rows[2], 22, "v%.*s", 19, esp_app_get_description()->version);
        snprintf(rows[4], 22, "by T.Anastopoulos");
        snprintf(rows[7], 22, "OK: BACK");
        break;
    case TEST_READY:
        snprintf(rows[0], 22, "AUTOTEST");
        snprintf(rows[2], 22, test_start_failed ? "Unavailable" : "Ready");
        snprintf(rows[4], 22, "OK to start");
        snprintf(rows[7], 22, "UP/DOWN: BACK");
        break;
    case TEST_RUNNING:
    case TEST_RESULTS: {
        autotest_status_t test;
        autotest_get(&test);
        const autotest_result_t *r = &test.last;
        const network_status_t *n = &r->network;
        if (screen == TEST_RUNNING) {
            snprintf(rows[0], 22, "AUTOTEST");
            snprintf(rows[2], 22, "Testing...");
            snprintf(rows[4], 22, "%.21s", test.progress);
            snprintf(rows[7], 22, "OK: BACK");
            break;
        }
        snprintf(rows[7], 22, "UP/DOWN PAGE OK BACK");
        switch (test_page) {
        case 0:
            snprintf(rows[0], 22, "AUTOTEST RESULT: %s", autotest_grade_name(r->overall));
            snprintf(rows[2], 22, "LINK     %s", autotest_grade_name(r->checks[AT_CHECK_LINK]));
            snprintf(rows[3], 22, "DHCP     %s", autotest_grade_name(r->checks[AT_CHECK_DHCP]));
            snprintf(rows[4], 22, "GATEWAY  %s", autotest_grade_name(r->checks[AT_CHECK_GATEWAY]));
            snprintf(rows[5], 22, "DNS      %s", autotest_grade_name(r->checks[AT_CHECK_DNS]));
            break;
        case 1:
            snprintf(rows[0], 22, "LINK: %s", autotest_grade_name(r->checks[AT_CHECK_NEGOTIATION]));
            snprintf(rows[2], 22, "Ethernet %s", n->link_up ? "UP" : "DOWN");
            if (n->speed_known) snprintf(rows[3], 22, "%u Mbps", n->speed_mbps);
            else snprintf(rows[3], 22, "Speed unknown");
            snprintf(rows[4], 22, "%s", !n->duplex_known ? "Duplex unknown" : n->full_duplex ? "Full Duplex" : "Half Duplex");
            break;
        case 2:
            snprintf(rows[0], 22, "NETWORK: %s", autotest_grade_name(r->checks[AT_CHECK_IPV4]));
            snprintf(rows[2], 22, "IP:");
            snprintf(rows[3], 22, IPSTR, IP2STR(&n->ipv4.ip));
            snprintf(rows[4], 22, "GW:");
            snprintf(rows[5], 22, IPSTR, IP2STR(&n->ipv4.gw));
            break;
        case 3:
            snprintf(rows[0], 22, "DHCP: %s", autotest_grade_name(r->checks[AT_CHECK_DHCP]));
            snprintf(rows[2], 22, "Client: %s", n->dhcp_running ? "STARTED" : "NOT STARTED");
            snprintf(rows[4], 22, "Mask:");
            snprintf(rows[5], 22, IPSTR, IP2STR(&n->ipv4.netmask));
            break;
        case 4:
            snprintf(rows[0], 22, "GATEWAY: %s", autotest_grade_name(r->checks[AT_CHECK_GATEWAY]));
            if (r->ping_measured) {
                snprintf(rows[2], 22, "TX %u RX %u Loss %u%%", (unsigned)r->ping_sent, (unsigned)r->ping_received, (unsigned)r->loss_percent);
                if ((unsigned)r->ping_received) {
                    snprintf(rows[3], 22, "Avg: %u ms", (unsigned)r->ping_avg_ms);
                    snprintf(rows[4], 22, "Min: %u Max: %u ms", (unsigned)r->ping_min_ms, (unsigned)r->ping_max_ms);
                } else snprintf(rows[3], 22, "No replies");
            } else snprintf(rows[2], 22, "No measurements");
            break;
        case 5:
            snprintf(rows[0], 22, "DNS: %s", autotest_grade_name(r->checks[AT_CHECK_DNS]));
            snprintf(rows[2], 22, IPSTR, IP2STR(&n->dns[0]));
            snprintf(rows[3], 22, IPSTR, IP2STR(&n->dns[1]));
            snprintf(rows[4], 22, "Resolution: %s", autotest_grade_name(r->checks[AT_CHECK_RESOLUTION]));
            snprintf(rows[5], 22, "%.21s", r->dns_hostname[0] ? r->dns_hostname : "No target configured");
            break;
        case 6:
            snprintf(rows[0], 22, "AUTOTEST NOTES");
            snprintf(rows[1], 22, "Uptime: %u seconds", (unsigned)r->finished_uptime_s);
            for (unsigned i = 0; i < 4 && i * 21 < strlen(r->note); ++i)
                snprintf(rows[2+i], 22, "%.21s", r->note+i*21);
            break;
        }
        break;
    }
    case CONFIRM_UPDATE:
        snprintf(rows[0], 22, "FIRMWARE UPDATE");
        snprintf(rows[2], 22, "Enable for 5 min?");
        snprintf(rows[4], 22, "OK: CONFIRM");
        snprintf(rows[6], 22, "UP/DOWN: CANCEL");
        break;
    case UPDATE:
        if (ota->phase == OTA_RECEIVING) {
            snprintf(rows[0], 22, "UPDATING");
            snprintf(rows[2], 22, "Firmware...");
            snprintf(rows[4], 22, "%u%%", ota->percent);
            snprintf(rows[7], 22, "DO NOT POWER OFF");
        } else if (ota->phase == OTA_SUCCESS) {
            snprintf(rows[0], 22, "UPDATE OK");
            snprintf(rows[3], 22, "Rebooting...");
        } else if (ota->phase == OTA_FAILED) {
            snprintf(rows[0], 22, "UPDATE FAILED");
            snprintf(rows[2], 22, "%.21s", ota->message);
            if (strlen(ota->message) > 21) snprintf(rows[3], 22, "%.21s", ota->message + 21);
            snprintf(rows[5], 22, "See browser for error");
            snprintf(rows[7], 22, "OK: BACK");
        } else {
            snprintf(rows[0], 22, "FIRMWARE UPDATE");
            snprintf(rows[2], 22, ota->unlocked ? "ENABLED - %u SEC" : "LOCKED", ota->seconds_left);
            snprintf(rows[3], 22, "%s", net->ipv4.ip.addr ? ip : "Waiting for DHCP");
            snprintf(rows[5], 22, ota->unlocked ? "Open browser" : "Enable from System");
            snprintf(rows[7], 22, "OK: BACK");
        }
        break;
    }
}

static void menu_task(void *arg)
{
    char previous[8][22] = {0}, rows[8][22];
    for (;;) {
        network_status_t net;
        ota_status_t ota;
        firmware_update_status(&ota);
        if (ota.revision != last_ota_revision && ota.phase != OTA_IDLE) screen = UPDATE;
        last_ota_revision = ota.revision;
        ui_button_t button;
        if (xQueueReceive(button_queue, &button, pdMS_TO_TICKS(200)) == pdTRUE) navigate(button, &ota);
        network_status_get(&net);
        firmware_update_status(&ota);
        /* OTA status always takes precedence over navigation. */
        if (ota.phase == OTA_RECEIVING || ota.phase == OTA_SUCCESS) screen = UPDATE;
        autotest_status_t test;
        autotest_get(&test);
        if (screen == TEST_RUNNING && !test.running && test.last.completed) { screen = TEST_RESULTS; test_page = 0; }
        discovery_status_t discovery;
        discovery_get(&discovery);
        if (screen == DISC_SCAN && discovery.state != DISC_RUNNING) screen = DISC_RESULTS;
        profinet_status_t pn;profinet_get(&pn);
        if(screen==PN_SCAN&&pn.state!=PN_RUNNING)screen=pn_back_pending?PN_MENU:PN_RESULTS;
        render(rows, &net, &ota);
        if (display_available && memcmp(rows, previous, sizeof(rows)) != 0) {
            const char *lines[8];
            for (unsigned i = 0; i < 8; ++i) lines[i] = rows[i];
            esp_err_t err = ui_oled_show_lines(lines);
            if (err != ESP_OK) {
                ESP_LOGW("SYNDETIS", "OLED unavailable: %s", esp_err_to_name(err));
                /* Preserve Ethernet/HTTP; don't authorize an invisible confirmation. */
                display_available = false;
            }
            memcpy(previous, rows, sizeof(rows));
        }
    }
}

esp_err_t ui_menu_init(bool display_ready)
{
    display_available = display_ready;
    button_queue = xQueueCreate(12, sizeof(ui_button_t));
    if (!button_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(menu_task, "ui_menu", 4096, NULL, 1, NULL) != pdPASS) {
        vQueueDelete(button_queue);
        button_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
