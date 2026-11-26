#include "ui.h"
#include "board.h"
#include "buttons.h"
#include "display.h"
#include "access_config.h"
#include "services.h"
#include "status.h"
#include "discovery.h"
#include "dhcp_test.h"
#include "network_ping.h"
#include "ota_update.h"
#include "ota_http_policy.h"
#include "profinet.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"

enum { MENU, LINK_INFO, SWITCH_MENU, SWITCH_STATUS, SWITCH_NEIGHBOR, DEVICE_INFO, WEB_MENU, WEB_DETAILS, ABOUT, TESTS_MENU, DHCP_MENU, DHCP_RESULTS,
       SETTINGS, UPDATE_DETAILS, DIAGNOSTIC_REPORT, MANUAL_MENU, MANUAL_IP_EDIT, MANUAL_PREFIX_EDIT, MANUAL_GATEWAY_EDIT, MANUAL_GATEWAY_MENU, MANUAL_REVIEW, MANUAL_RESULTS,
       PING_MENU, PING_NO_IP, PING_CUSTOM, PING_REVIEW, PING_RESULTS,
       PROFINET_MENU, PROFINET_PROGRESS, PROFINET_RESULTS, PROFINET_DEVICE };
static const struct { const char *label; int page; } main_items[] = {
    {"Link & IP", LINK_INFO},
    {"Switch Info", SWITCH_MENU},
    {"PROFINET", PROFINET_MENU},
    {"Network Tests", TESTS_MENU},
    {"Device Info", DEVICE_INFO},
    {"Web Access", WEB_MENU},
    {"About", ABOUT},
    {"Settings", SETTINGS},
    {"Diagnostic Report", DIAGNOSTIC_REPORT},
};
static const int main_item_count = sizeof(main_items) / sizeof(main_items[0]);

static int page, selection, web_selection, switch_selection, neighbor_index, top;
static char lines[128][21];
static int line_count;
static int tests_selection, dhcp_selection;
static int settings_selection;
static manual_ipv4_t draft;
static int manual_selection, gateway_selection, edit_octet, review_selection;
static unsigned review_generation;
static wired_mode_t review_mode;
static bool review_dhcp_requesting;
static const char *manual_notice;
static uint32_t ping_custom;
static int ping_selection, ping_review_selection, ping_result_selection;
static bool ping_gateway;
static const char *ping_notice;
static unsigned profinet_selection, profinet_index;
static const char *profinet_notice;

void ui_draw_splash(int64_t elapsed_ms)
{
    display_clear();
    // RJ45 plug shell, shoulder, locking tab, and five stylized contacts.
    display_rect(40, 8, 48, 22, false, true);
    display_rect(48, 4, 32, 5, false, true);
    display_rect(56, 1, 16, 4, false, true);
    int lit = elapsed_ms / 180;
    if (lit > 5) lit = 5;
    for (int i = 0; i < 5; ++i)
        display_rect(45 + 8 * i, 12, 5, 13, i < lit, true);
    if (elapsed_ms >= 1050) display_text(16, 35, "Syndetis", 2, true);
    if (elapsed_ms >= 1350) display_text(7, 55, "ethernet field tool", 1, true);
}

static void add_line(const char *format, ...)
{
    char text[600];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    // Hard wrap runtime strings (including IDF versions) within 20 columns.
    size_t len = strlen(text);
    for (size_t pos = 0; pos < len && line_count < 128; pos += 20) {
        size_t count = len - pos;
        if (count > 20) count = 20;
        memcpy(lines[line_count], text + pos, count);
        lines[line_count++][count] = '\0';
    }
}

static void device_lines(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    line_count = 0;
    add_line("Board: WT32-ETH01"); // Board identity is configured, not autodetected.
    add_line("Chip model ID: %d", (int)chip.model);
    add_line("Cores: %u", chip.cores);
    add_line("Revision: %u.%u", chip.revision / 100, chip.revision % 100);
    add_line("FW: %s", esp_app_get_description()->version);
    add_line("IDF: %s", esp_get_idf_version());
    uint32_t flash_size;
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK)
        add_line("Flash: %" PRIu32 " KiB", flash_size / 1024);
    else add_line("Flash: read error");
    add_line("Heap: %" PRIu32 " B", esp_get_free_heap_size());
    add_line("Min heap: %" PRIu32 " B", esp_get_minimum_free_heap_size());
    add_line("Uptime: %" PRId64 " s", esp_timer_get_time() / 1000000);
    add_line("Reset reason: %d", (int)esp_reset_reason());
    uint8_t mac[6];
    add_line("Factory base MAC:");
    if (esp_efuse_mac_get_default(mac) == ESP_OK)
        add_line("%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    else add_line("MAC: read error");
    add_line("OLED: SH1106 128x64");
    add_line("I2C ACK: 0x%02X", display_address());
    add_line("SDA: GPIO%d", BOARD_SDA);
    add_line("SCL: GPIO%d", BOARD_SCL);
    add_line("UP: GPIO%d", BOARD_UP);
    add_line("DOWN: GPIO%d", BOARD_DOWN);
    add_line("OK: GPIO%d", BOARD_OK);
    add_line("Buttons: active LOW");
    add_line("10k external pullups");
    add_line("PHY: LAN8720A addr %d", BOARD_ETH_PHY_ADDRESS);
    add_line("MDC: GPIO%d", BOARD_ETH_MDC);
    add_line("MDIO: GPIO%d", BOARD_ETH_MDIO);
    add_line("Clock enable: GPIO%d", BOARD_ETH_CLOCK_ENABLE);
    add_line("Clock input: GPIO%d", BOARD_ETH_CLOCK_INPUT);
    add_line("UP/DOWN scroll");
    add_line("OK: back");
    add_line("Hold OK: back 500ms");
}

static void dhcp_lines(bool title)
{
    dhcp_status_t d = dhcp_test_status();
    if (title) add_line(d.mode == WIRED_MANUAL || d.state == MANUAL_PROBING || d.state == MANUAL_CONFLICT ? "Manual IPv4" : "DHCP Test");
    add_line("Mode: %s", d.mode == WIRED_MANUAL ? "MANUAL IP" : d.mode == WIRED_DHCP ? "DHCP" : "NO IP");
    add_line("%s", dhcp_state_name(d.state));
    if (d.busy) add_line("Applying / cleanup");
    if (d.state == DHCP_REQUESTING || (d.mode == WIRED_DHCP && d.duration_ms))
        add_line("%s: %llu.%llu s", d.state == DHCP_REQUESTING ? "Elapsed" : "Acquisition",
                 (unsigned long long)(d.duration_ms / 1000), (unsigned long long)(d.duration_ms % 1000 / 100));
    if (d.active) {
        add_line("Tester IP: %s", d.ip);
        add_line("Mask: %s", d.mask);
        if (d.mode == WIRED_MANUAL) add_line("Prefix: /%u", d.prefix);
        add_line("Gateway: %s", d.gateway[0] ? d.gateway : "NONE");
        add_line("DNS: %s", d.mode == WIRED_MANUAL ? "Not configured" : d.dns[0][0] ? d.dns[0] : "Not supplied");
        if (d.mode != WIRED_MANUAL) add_line("DNS 2: %s", d.dns[1][0] ? d.dns[1] : "Not supplied");
        add_line("Address stays on Back");
    } else add_line("No current IPv4");
    if (d.state == DHCP_OVERLAP) add_line("Conflicts with hotspot 192.168.4.1/24");
    if (d.error) { add_line("%s", d.error_step); add_line("Error: 0x%x", (unsigned)d.error); }
    if (d.cleanup_error) {
        add_line("Cleanup: %s", d.cleanup_step);
        add_line("Cleanup error: 0x%x", (unsigned)d.cleanup_code);
        add_line("Retry Stop / Release");
    }
    if (d.state == MANUAL_PROBING) add_line("Back cancels probing");
    if (d.state == MANUAL_CONFLICT) add_line("Edit then APPLY again");
    if (d.mode == WIRED_MANUAL) add_line("Manual menu: USE DHCP");
    add_line("No reachability test");
}

static void link_lines(const ethernet_status_t *eth)
{
    line_count = 0;
    add_line("Link & IP");
    if (eth->state == ETH_ERROR) {
        add_line("Ethernet init error");
        add_line("%s", eth->error_step);
        add_line("Code: 0x%x", (unsigned)eth->error);
    } else if (eth->state == ETH_INITIALIZING) add_line("Ethernet starting");
    else add_line("Physical link: %s", eth->link_up ? "UP" : "DOWN");
    if (eth->link_up && eth->speed_mbps) add_line("Speed: %d Mbps", eth->speed_mbps);
    else add_line("Speed: unavailable");
    add_line("Duplex: %s", !eth->link_up || !eth->duplex ? "unavailable" : eth->duplex == 2 ? "full" : "half");
    dhcp_lines(false);
    add_line("Ethernet MAC:");
    if (eth->mac_valid)
        add_line("%02X:%02X:%02X:%02X:%02X:%02X", eth->mac[0], eth->mac[1], eth->mac[2], eth->mac[3], eth->mac[4], eth->mac[5]);
    else add_line("Unavailable");
    add_line("UP/DOWN scroll");
    add_line("OK: back");
    add_line("Hold OK: back 500ms");
}

static void web_lines(const web_status_t *web)
{
    line_count = 0;
    add_line("Web Access");
    if (web->state == WEB_ON) {
        add_line("SSID:");
        add_line("%s", web->ssid);
        add_line("Password:");
        add_line("%s", WEB_AP_PASSWORD);
        add_line("Browser URL:");
        add_line("%s", WEB_URL);
        add_line("WPA2-Personal");
        add_line("No dashboard login");
    } else {
        add_line(web->state == WEB_ERROR ? "Web Access error" : "Web Access off");
        if (web->error[0]) add_line("%s", web->error);
        add_line("HTTP: %s", web->http_running ? "still running" : "stopped");
        add_line("Hotspot: %s", web->ap_running ? "still running" : "stopped");
        if (web->http_running || web->ap_running) add_line("Use Retry cleanup");
    }
    add_line("UP/DOWN scroll");
    add_line("OK: back");
    add_line("Hold OK: back 500ms");
}

static void advertised_field(const char *label, const char *value, bool present)
{
    add_line("%s: %s", label, !present ? "Unavailable" : value[0] ? value : "(empty)");
}

static void switch_lines(void)
{
    line_count = 0;
    discovery_summary_t summary = discovery_summary();
    if (page == SWITCH_STATUS) {
        add_line("Switch Info");
        const char *state = status_get().ethernet.state != ETH_READY ? "Ethernet unavailable" :
                            summary.available ? neighbors_state_name(summary.state) : "Discovery error";
        add_line("%s", state);
        add_line("Passive LLDP/CDP");
        add_line("Observe: 30 seconds");
        add_line("Listening continues");
        add_line("Record capacity: %d", NEIGHBOR_CAPACITY);
        add_line("Capacity drops: %" PRIu32, summary.capacity_drops);
        add_line("Queue drops: %" PRIu32, summary.queue_drops);
        add_line("Limited frames: %" PRIu32, summary.limited_frames);
        add_line("Invalid frames: %" PRIu32, summary.invalid_frames);
    } else {
        static neighbor_t neighbor; // Bounded, outside the small UI task stack.
        if (!discovery_record(neighbor_index, &neighbor)) {
            add_line("Neighbor cleared");
            add_line("%s", neighbors_state_name(summary.state));
        } else {
            uint64_t now = esp_timer_get_time() / 1000;
            add_line("%s - %s", neighbor.protocol == PROTOCOL_LLDP ? "LLDP" : "CDP",
                     neighbor_expired(&neighbor, now) ? "Expired" : "Current");
            add_line("Source MAC:");
            add_line("%02X:%02X:%02X:%02X:%02X:%02X", neighbor.source[0], neighbor.source[1],
                     neighbor.source[2], neighbor.source[3], neighbor.source[4], neighbor.source[5]);
            advertised_field("Name", neighbor.name, neighbor.fields & FIELD_NAME);
            advertised_field("Chassis", neighbor.chassis, neighbor.fields & FIELD_CHASSIS);
            if (neighbor.fields & FIELD_CHASSIS) add_line("Chassis subtype: %u", neighbor.chassis_subtype);
            advertised_field("Port ID", neighbor.port, neighbor.fields & FIELD_PORT);
            if (neighbor.protocol == PROTOCOL_LLDP) add_line("Port subtype: %u", neighbor.port_subtype);
            advertised_field("Port description", neighbor.description, neighbor.fields & FIELD_DESCRIPTION);
            advertised_field(neighbor.management_kind == 2 ? "Advertised IP" : "Management IP",
                             neighbor.management, neighbor.fields & FIELD_MANAGEMENT);
            if (neighbor.management_kind == 2) add_line("CDP address TLV");
            const char *vlan = neighbor.protocol == PROTOCOL_LLDP ? "Port PVID" : "Native VLAN";
            if (neighbor.fields & FIELD_VLAN) add_line("%s: %u", vlan, neighbor.vlan);
            else add_line("%s: Unavailable", vlan);
            add_line("TTL: %u seconds", neighbor.ttl);
            add_line("Age: %" PRIu64 " seconds", now >= neighbor.received_ms ? (now - neighbor.received_ms) / 1000 : 0);
            if (neighbor.sanitized) add_line("Non-ASCII shown as ?");
            add_line("Advertised data only");
            add_line("IP is not tester IP");
            add_line("VLAN is not full list");
        }
    }
    add_line("UP/DOWN scroll");
    add_line("OK / hold OK: back");
}

static int web_items(const web_status_t *web, const char **items)
{
    if (web->state == WEB_ON) {
        items[0] = "Connection details"; items[1] = "Disable"; items[2] = "Back";
        return 3;
    }
    if (web->state == WEB_STARTING || web->state == WEB_STOPPING) {
        items[0] = web->state == WEB_STARTING ? "Starting access" : "Stopping access";
        items[1] = "Back";
        return 2;
    }
    if (web->state == WEB_ERROR) {
        items[0] = "Access error";
        items[1] = web->ap_running || web->http_running ? "Retry cleanup" : "Enable";
        items[2] = "Back";
        return 3;
    }
    items[0] = "Enable"; items[1] = "Back";
    return 2;
}

static void information_lines(const app_status_t *status)
{
    if (page == UPDATE_DETAILS) {
        ota_update_status_t u=ota_update_status();line_count=0;
        add_line("Firmware Update");add_line("%s",ota_state_name(u.state));add_line("%s",u.message);
        if(u.state==OTA_AUTHORIZED) {
            add_line("Update mode active");
            add_line("Wi-Fi: %s",status->web.ssid);add_line("Password:");add_line("%s",WEB_AP_PASSWORD);
            add_line("http://192.168.4.1:8080/update");
            dhcp_status_t d=dhcp_test_status();
            if(d.active) {add_line("Ethernet URL:");add_line("http://%s:8080/update",d.ip);}
            else {add_line("Ethernet: set Manual");add_line("IP or DHCP first");}
            add_line("Use application .bin");add_line("Local HTTP: no");add_line("encryption/signature");
        }
        if(u.total)add_line("Received: %u%%",(unsigned)(u.received*100/u.total));
        add_line("UP/DOWN: scroll");add_line("OK / hold: cancel");
    }
    else if (page == DEVICE_INFO) device_lines();
    else if (page == LINK_INFO) link_lines(&status->ethernet);
    else if (page == SWITCH_STATUS || page == SWITCH_NEIGHBOR) switch_lines();
    else if (page == WEB_DETAILS) web_lines(&status->web);
    else if (page == DIAGNOSTIC_REPORT) {
        line_count = 0;
        add_line("Diagnostic Report");
        if (status->web.state == WEB_ON) {
            add_line("%s", WEB_URL);
            add_line("Report on dashboard");
        } else {
            add_line("%s", status->web.state == WEB_OFF ? "Enable Web Access" : "Check Web Access");
            add_line("from the main menu");
        }
        add_line("OK / Hold OK: Back");
    }
    else if (page == DHCP_RESULTS || page == MANUAL_RESULTS) { line_count = 0; dhcp_lines(true); }
    else {
        line_count = 0;
        add_line("Syndetis");
        add_line("ethernet field tool");
        add_line("%s", esp_app_get_description()->version);
        add_line("OK / hold OK: back");
    }
}

void ui_init(void)
{
    page = MENU; selection = 0; web_selection = 0; top = 0;
    draft = (manual_ipv4_t){.ip = 0xc0a80afau, .prefix = 24};
    manual_selection = 0; manual_notice = NULL;
    ping_custom = 0xc0a80a01u; ping_selection = 0; ping_notice = NULL;
}

bool ui_editing(void)
{
    return page == MANUAL_IP_EDIT || page == MANUAL_GATEWAY_EDIT || page == MANUAL_PREFIX_EDIT || page == PING_CUSTOM;
}

static void edit_address(uint32_t *ip, bool up)
{
    unsigned shift = (3 - edit_octet) * 8;
    unsigned octet = (*ip >> shift) & 255;
    if (up && octet < 255) ++octet;
    if (!up && octet > 0) --octet;
    *ip = (*ip & ~(255u << shift)) | (octet << shift);
}

static void review_current(void)
{
    dhcp_status_t d = dhcp_test_status();
    review_generation = d.generation;
    review_mode = d.mode;
    review_dhcp_requesting = d.state == DHCP_REQUESTING;
}

static void manual_input(uint8_t buttons)
{
    if (buttons & BUTTON_BACK) {
        if (page == MANUAL_RESULTS) wired_manual_cancel();
        page = page == MANUAL_MENU ? TESTS_MENU : MANUAL_MENU;
        manual_notice = NULL; top = 0;
        return;
    }
    bool up = buttons == BUTTON_UP || buttons == BUTTON_UP_REPEAT;
    bool down = buttons == BUTTON_DOWN || buttons == BUTTON_DOWN_REPEAT;
    if (ui_editing()) {
        if (up || down) {
            if (page == MANUAL_PREFIX_EDIT) {
                if (up && draft.prefix < 30) ++draft.prefix;
                if (down && draft.prefix > 1) --draft.prefix;
            } else {
                uint32_t *ip = page == MANUAL_IP_EDIT ? &draft.ip : &draft.gateway;
                edit_address(ip, up);
            }
        }
        if (buttons == BUTTON_OK && (page == MANUAL_PREFIX_EDIT || ++edit_octet == 4)) page = MANUAL_MENU;
        return;
    }
    if (page == MANUAL_MENU) {
        if (buttons == BUTTON_UP && manual_selection > 0) --manual_selection;
        if (buttons == BUTTON_DOWN && manual_selection < 6) ++manual_selection;
        if (buttons == BUTTON_OK) {
            manual_notice = NULL;
            if (manual_selection < 2) { page = manual_selection ? MANUAL_PREFIX_EDIT : MANUAL_IP_EDIT; edit_octet = 0; }
            else if (manual_selection == 2) { page = MANUAL_GATEWAY_MENU; gateway_selection = 0; }
            else if (manual_selection == 3) { page = MANUAL_REVIEW; review_selection = 0; review_current(); }
            else if (manual_selection == 4) { page = MANUAL_RESULTS; top = 0; }
            else if (manual_selection == 5) {
                if (wired_use_dhcp()) { page = DHCP_MENU; dhcp_selection = 0; }
                else manual_notice = "Operation active";
            } else page = TESTS_MENU;
        }
    } else if (page == MANUAL_GATEWAY_MENU) {
        if (buttons == BUTTON_UP && gateway_selection > 0) --gateway_selection;
        if (buttons == BUTTON_DOWN && gateway_selection < 2) ++gateway_selection;
        if (buttons == BUTTON_OK) {
            if (!gateway_selection) { draft.gateway = 0; page = MANUAL_MENU; }
            else if (gateway_selection == 1) { page = MANUAL_GATEWAY_EDIT; edit_octet = 0; }
            else page = MANUAL_MENU;
        }
    } else if (page == MANUAL_REVIEW) {
        if (buttons == BUTTON_UP && review_selection > 0) --review_selection;
        if (buttons == BUTTON_DOWN && review_selection < 2) ++review_selection;
        if (buttons == BUTTON_OK) {
            if (review_selection) { page = MANUAL_MENU; manual_notice = NULL; }
            else {
                manual_notice = wired_manual_apply(&draft, review_generation);
                if (!manual_notice) { page = MANUAL_RESULTS; top = 0; }
                else review_current(); // A changed lease requires a fresh, explicit APPLY.
            }
        }
    } else if (page == MANUAL_RESULTS) {
        if (buttons == BUTTON_OK) { wired_manual_cancel(); page = MANUAL_MENU; top = 0; }
        else {
            line_count = 0; dhcp_lines(true);
            if (buttons == BUTTON_UP && top > 0) --top;
            if (buttons == BUTTON_DOWN && top + 4 < line_count) ++top;
        }
    }
}

static void ping_start_ui(void)
{
    if (!dhcp_test_status().active) { page = PING_NO_IP; ping_selection = 0; return; }
    ping_notice = network_ping_start(ping_gateway, ping_custom);
    if (!ping_notice) { page = PING_RESULTS; ping_result_selection = 0; }
    else if (page == PING_RESULTS) { page = ping_gateway ? PING_MENU : PING_REVIEW; ping_review_selection = 0; }
}

static void ping_input(uint8_t buttons)
{
    if (buttons & BUTTON_BACK) {
        if (page == PING_RESULTS) network_ping_cancel();
        page = page == PING_MENU || page == PING_NO_IP ? TESTS_MENU : PING_MENU;
        ping_notice = NULL; return;
    }
    if (page == PING_CUSTOM) {
        if (buttons == BUTTON_UP || buttons == BUTTON_UP_REPEAT) edit_address(&ping_custom, true);
        if (buttons == BUTTON_DOWN || buttons == BUTTON_DOWN_REPEAT) edit_address(&ping_custom, false);
        if (buttons == BUTTON_OK && ++edit_octet == 4) { page = PING_REVIEW; ping_review_selection = 0; }
    } else if (page == PING_RESULTS) {
        network_ping_status_t p = network_ping_status();
        if (!p.active) {
            if (buttons == BUTTON_UP) ping_result_selection = 0;
            if (buttons == BUTTON_DOWN) ping_result_selection = 1;
        }
        if (buttons == BUTTON_OK) {
            if (p.active) network_ping_cancel();
            else if (!ping_result_selection) ping_start_ui();
            else { page = PING_MENU; ping_notice = NULL; }
        }
    } else if (page == PING_REVIEW) {
        if (buttons == BUTTON_UP && ping_review_selection > 0) --ping_review_selection;
        if (buttons == BUTTON_DOWN && ping_review_selection < 2) ++ping_review_selection;
        if (buttons == BUTTON_OK) {
            if (!ping_review_selection) { ping_gateway = false; ping_start_ui(); }
            else if (ping_review_selection == 1) { page = PING_CUSTOM; edit_octet = 0; }
            else page = PING_MENU;
        }
    } else {
        if (buttons == BUTTON_UP && ping_selection > 0) --ping_selection;
        if (buttons == BUTTON_DOWN && ping_selection < 2) ++ping_selection;
        if (buttons == BUTTON_OK) {
            ping_notice = NULL;
            if (page == PING_NO_IP) {
                page = ping_selection == 0 ? MANUAL_MENU : ping_selection == 1 ? DHCP_MENU : TESTS_MENU;
                manual_selection = dhcp_selection = 0;
            } else if (ping_selection == 2) page = TESTS_MENU;
            else if (ping_selection == 1) { page = PING_CUSTOM; edit_octet = 0; }
            else { ping_gateway = true; ping_start_ui(); }
        }
    }
}

static void profinet_input(uint8_t buttons)
{
    dcp_summary_t scan = profinet_summary();
    if (buttons & BUTTON_BACK) {
        if (page == PROFINET_PROGRESS && dcp_active(scan.state)) profinet_cancel();
        page = page == PROFINET_MENU ? MENU :
               page == PROFINET_DEVICE || page == PROFINET_PROGRESS ? PROFINET_RESULTS : PROFINET_MENU;
        top = 0;
        return;
    }
    if (page == PROFINET_MENU) {
        if (buttons == BUTTON_UP && profinet_selection) --profinet_selection;
        if (buttons == BUTTON_DOWN && profinet_selection < 2) ++profinet_selection;
        if (buttons != BUTTON_OK) return;
        if (profinet_selection == 2) page = MENU;
        else if (profinet_selection == 1) { page = PROFINET_RESULTS; profinet_index = 0; }
        else {
            profinet_notice = profinet_start();
            if (!profinet_notice) { page = PROFINET_PROGRESS; profinet_index = 0; }
        }
    } else if (page == PROFINET_RESULTS) {
        if (buttons == BUTTON_UP && profinet_index) --profinet_index;
        if (buttons == BUTTON_DOWN && profinet_index + 1 < scan.count) ++profinet_index;
        if (buttons == BUTTON_OK && profinet_index < scan.count) { page = PROFINET_DEVICE; top = 0; }
    } else if (page == PROFINET_DEVICE) {
        if (buttons == BUTTON_UP && top) --top;
        if (buttons == BUTTON_DOWN && top + 3 < line_count) ++top;
    }
}

void ui_input(uint8_t buttons)
{
    app_status_t status = status_get();
    if(page==SETTINGS) {
        if(buttons & BUTTON_BACK || (buttons==BUTTON_OK && settings_selection==1)) {page=MENU;return;}
        if(buttons==BUTTON_UP)settings_selection=0;
        if(buttons==BUTTON_DOWN)settings_selection=1;
        if(buttons==BUTTON_OK && settings_selection==0) {ota_update_request_session();page=UPDATE_DETAILS;top=0;}
        return;
    }
    if(page==UPDATE_DETAILS && (buttons & BUTTON_BACK || buttons==BUTTON_OK)) {
        ota_update_cancel();page=SETTINGS;top=0;return;
    }
    if (page >= PROFINET_MENU && page <= PROFINET_DEVICE) { profinet_input(buttons); return; }
    if (page >= PING_MENU && page <= PING_RESULTS) { ping_input(buttons); return; }
    if (page >= MANUAL_MENU && page <= MANUAL_RESULTS) { manual_input(buttons); return; }
    if (buttons & (BUTTON_UP_REPEAT | BUTTON_DOWN_REPEAT)) return;
    if (buttons & BUTTON_BACK) {
        if (page == DHCP_RESULTS || page == DHCP_MENU) {
            dhcp_test_cancel_requesting();
            page = page == DHCP_RESULTS ? DHCP_MENU : TESTS_MENU;
        }
        else if (page == TESTS_MENU) page = MENU;
        else if (page == WEB_DETAILS) page = WEB_MENU;
        else if (page == SWITCH_STATUS || page == SWITCH_NEIGHBOR) page = SWITCH_MENU;
        else if (page != MENU) page = MENU;
        top = 0;
        return; // Back never enables/disables access or activates a selected item.
    }
    if (page == MENU) {
        if (buttons == BUTTON_UP && selection > 0) --selection;
        if (buttons == BUTTON_DOWN && selection + 1 < main_item_count) ++selection;
        if (buttons == BUTTON_OK) {
            page = main_items[selection].page;
            top = 0;
            if (page == SETTINGS) settings_selection = 0;
            if (page == WEB_MENU) web_selection = 0;
            if (page == SWITCH_MENU) switch_selection = 0;
            if (page == PROFINET_MENU) { profinet_selection = 0; profinet_notice = NULL; }
            if (page == TESTS_MENU) tests_selection = 0;
        }
    } else if (page == TESTS_MENU) {
        if (buttons == BUTTON_UP && tests_selection > 0) --tests_selection;
        if (buttons == BUTTON_DOWN && tests_selection < 3) ++tests_selection;
        if (buttons == BUTTON_OK) {
            page = tests_selection == 3 ? MENU : tests_selection == 2 ?
                (dhcp_test_status().active ? PING_MENU : PING_NO_IP) : tests_selection == 1 ? MANUAL_MENU : DHCP_MENU;
            ping_selection = 0; ping_notice = NULL;
            dhcp_selection = 0;
        }
    } else if (page == DHCP_MENU) {
        if (buttons == BUTTON_UP && dhcp_selection > 0) --dhcp_selection;
        if (buttons == BUTTON_DOWN && dhcp_selection < 3) ++dhcp_selection;
        if (buttons == BUTTON_OK) {
            if (dhcp_selection == 3) { dhcp_test_cancel_requesting(); page = TESTS_MENU; }
            else {
                if (dhcp_selection == 0) dhcp_test_request(true);
                if (dhcp_selection == 2) dhcp_test_request(false);
                page = DHCP_RESULTS; top = 0;
            }
        }
    } else if (page == SWITCH_MENU) {
        discovery_summary_t summary = discovery_summary();
        int slots[NEIGHBOR_CAPACITY], neighbors = 0;
        for (int i = 0; i < NEIGHBOR_CAPACITY; ++i) if (summary.used[i]) slots[neighbors++] = i;
        int count = neighbors + 2; // Status/details, records, Back.
        if (switch_selection >= count) switch_selection = count - 1;
        if (buttons == BUTTON_UP && switch_selection > 0) --switch_selection;
        if (buttons == BUTTON_DOWN && switch_selection + 1 < count) ++switch_selection;
        if (buttons == BUTTON_OK) {
            if (switch_selection == count - 1) page = MENU;
            else if (switch_selection == 0) page = SWITCH_STATUS;
            else { page = SWITCH_NEIGHBOR; neighbor_index = slots[switch_selection - 1]; }
            top = 0;
        }
    } else if (page == WEB_MENU) {
        const char *items[3];
        int count = web_items(&status.web, items);
        if (web_selection >= count) web_selection = count - 1;
        if (buttons == BUTTON_UP && web_selection > 0) --web_selection;
        if (buttons == BUTTON_DOWN && web_selection + 1 < count) ++web_selection;
        if (buttons == BUTTON_OK) {
            const char *item = items[web_selection];
            if (strcmp(item, "Back") == 0) page = MENU;
            else if (strcmp(item, "Enable") == 0) services_request_web(true);
            else if (strcmp(item, "Disable") == 0 || strcmp(item, "Retry cleanup") == 0)
                services_request_web(false);
            else if (strcmp(item, "Connection details") == 0 || strcmp(item, "Access error") == 0) {
                page = WEB_DETAILS;
                top = 0;
            }
        }
    } else {
        if (buttons == BUTTON_OK) {
            if (page == DHCP_RESULTS) dhcp_test_cancel_requesting();
            page = page == DHCP_RESULTS ? DHCP_MENU : page == WEB_DETAILS ? WEB_MENU :
                   page == SWITCH_STATUS || page == SWITCH_NEIGHBOR ? SWITCH_MENU : MENU;
            top = 0;
        }
        else {
            information_lines(&status);
            if (buttons == BUTTON_UP && top > 0) --top;
            if (buttons == BUTTON_DOWN && top + 4 < line_count) ++top;
        }
    }
}

static void scrollbar(int first, int total, int visible)
{
    if (total <= visible) return;
    const int track = 12 * visible;
    int thumb = track * visible / total;
    if (thumb < 3) thumb = 3;
    int y = 64 - track + (track - thumb) * first / (total - visible);
    display_rect(126, 64 - track, 1, track, true, true);
    display_rect(125, y, 3, thumb, true, true);
}

static void ipv4_text(char *out, size_t size, uint32_t ip)
{
    snprintf(out, size, "%u.%u.%u.%u", (unsigned)(ip >> 24), (unsigned)(ip >> 16 & 255),
             (unsigned)(ip >> 8 & 255), (unsigned)(ip & 255));
}

static void draw_address_editor(uint32_t ip)
{
    char text[4];
    for (int i = 0; i < 4; ++i) {
        int x = 2 + i * 24;
        if (i == edit_octet) display_rect(x, 31, 18, 11, true, true);
        snprintf(text, sizeof(text), "%3u", (unsigned)(ip >> ((3 - i) * 8) & 255));
        display_text(x, 33, text, 1, i != edit_octet);
        if (i < 3) display_text(x + 18, 33, ".", 1, true);
    }
}

static void ping_draw(void)
{
    if (page == PING_MENU && !dhcp_test_status().active) { page = PING_NO_IP; ping_selection = 0; }
    char text[21], address[16];
    if (page == PING_CUSTOM) {
        display_text(2, 17, "PING CUSTOM IP", 1, true);
        draw_address_editor(ping_custom);
        display_text(2, 46, "UP/DOWN: adjust", 1, true);
        display_text(2, 56, "OK next / Hold Back", 1, true);
    } else if (page == PING_RESULTS) {
        network_ping_status_t p = network_ping_status();
        ipv4_text(address, sizeof(address), p.target); snprintf(text, sizeof(text), "PING %s", address);
        display_text(2, 16, text, 1, true);
        if (p.active && p.valid && !p.error) snprintf(text, sizeof(text), "%u / 4 SENT R:%u", p.sent, p.replies);
        else if (!p.valid || p.error || !p.replies || p.completed != 4) snprintf(text, sizeof(text), "%.20s", p.detail ? p.detail : "Ready");
        else snprintf(text, sizeof(text), "%u / 4 REPLY", p.replies);
        display_text(2, 25, text, 1, true);
        if (p.valid && p.replies) snprintf(text, sizeof(text), "AVG %u MS", (unsigned)((p.rtt_sum_us / p.replies + 500) / 1000));
        else snprintf(text, sizeof(text), "AVG --");
        display_text(2, 34, text, 1, true);
        if (p.error) snprintf(text, sizeof(text), "ERR %d", p.error);
        else if (p.valid && p.sent) snprintf(text, sizeof(text), "LOSS %u%%", (p.sent - p.replies) * 100 / p.sent);
        else snprintf(text, sizeof(text), "LOSS --");
        display_text(2, 43, text, 1, true);
        display_text(2, 55, p.active ? "> CANCEL / Hold Back" : ping_result_selection ? "  AGAIN    > BACK" : "> AGAIN      BACK", 1, true);
    } else if (page == PING_REVIEW) {
        display_text(2, 16, "PING CUSTOM IP", 1, true);
        ipv4_text(address, sizeof(address), ping_custom); display_text(2, 29, address, 1, true);
        display_text(2, 42, ping_notice ? ping_notice : "Hold OK: Back", 1, true);
        const char *actions[] = {"START", "EDIT", "BACK"}; const int x[] = {2, 44, 80};
        for (int i = 0; i < 3; ++i) {
            bool selected = ping_review_selection == i;
            if (selected) display_rect(x[i], 53, (int)strlen(actions[i]) * 6, 11, true, true);
            display_text(x[i], 55, actions[i], 1, !selected);
        }
    } else {
        const char *rows[] = {page == PING_NO_IP ? "SET MANUAL IP" : "Ping gateway",
                              page == PING_NO_IP ? "DHCP TEST" : "Custom IP", "Back"};
        display_text(2, 16, ping_notice ? ping_notice : page == PING_NO_IP ? "NO WIRED IP" : "PING", 1, true);
        for (int i = 0; i < 3; ++i) {
            bool selected = ping_selection == i;
            if (selected) display_rect(0, 28 + i * 12, 123, 12, true, true);
            display_text(3, 30 + i * 12, rows[i], 1, !selected);
        }
    }
}

static void manual_draw(void)
{
    char text[21], address[16];
    if (ui_editing()) {
        display_text(2, 17, page == MANUAL_IP_EDIT ? "YOUR IP" : page == MANUAL_PREFIX_EDIT ? "PREFIX" : "GATEWAY (0 = NONE)", 1, true);
        if (page == MANUAL_PREFIX_EDIT) {
            snprintf(text, sizeof(text), "/%u", draft.prefix);
            display_text(2, 32, text, 2, true);
        } else {
            uint32_t ip = page == MANUAL_IP_EDIT ? draft.ip : draft.gateway;
            draw_address_editor(ip);
        }
        display_text(2, 46, "UP/DOWN: adjust", 1, true);
        display_text(2, 56, "OK next / Hold Back", 1, true);
    } else if (page == MANUAL_REVIEW) {
        display_text(2, 16, review_mode == WIRED_DHCP || review_dhcp_requesting ? "REPLACE DHCP?" :
                     review_mode == WIRED_MANUAL ? "REPLACE MANUAL IP?" : "REVIEW MANUAL IP", 1, true);
        ipv4_text(address, sizeof(address), draft.ip);
        snprintf(text, sizeof(text), "%s/%u", address, draft.prefix);
        display_text(2, 25, text, 1, true);
        if (draft.gateway) ipv4_text(address, sizeof(address), draft.gateway);
        else snprintf(address, sizeof(address), "NONE");
        snprintf(text, sizeof(text), "GW %s", address);
        display_text(2, 34, text, 1, true);
        display_text(2, 43, manual_notice ? manual_notice : "Hold OK: cancel", 1, true);
        const char *actions[] = {"APPLY", "EDIT", "BACK"};
        const int x[] = {2, 44, 80};
        for (int i = 0; i < 3; ++i) {
            bool selected = i == review_selection;
            if (selected) display_rect(x[i], 53, (int)strlen(actions[i]) * 6, 11, true, true);
            display_text(x[i], 55, actions[i], 1, !selected);
        }
    } else {
        char labels[7][21];
        int count, selected;
        if (page == MANUAL_GATEWAY_MENU) {
            snprintf(labels[0], 21, "NONE"); snprintf(labels[1], 21, "Edit gateway"); snprintf(labels[2], 21, "Back");
            count = 3; selected = gateway_selection;
        } else {
            snprintf(labels[0], 21, "YOUR IP"); snprintf(labels[1], 21, "PREFIX /%u", draft.prefix);
            if (draft.gateway) ipv4_text(address, sizeof(address), draft.gateway);
            else snprintf(address, sizeof(address), "NONE");
            snprintf(labels[2], 21, "GW %s", address);
            snprintf(labels[3], 21, "REVIEW"); snprintf(labels[4], 21, "Status / Results");
            snprintf(labels[5], 21, "USE DHCP"); snprintf(labels[6], 21, "Back");
            count = 7; selected = manual_selection;
        }
        display_text(2, 16, manual_notice ? manual_notice : "MANUAL IP", 1, true);
        int first = selected >= 3 ? selected - 2 : 0;
        for (int row = 0; row < 3 && first + row < count; ++row) {
            bool chosen = first + row == selected;
            if (chosen) display_rect(0, 28 + row * 12, 123, 12, true, true);
            display_text(3, 30 + row * 12, labels[first + row], 1, !chosen);
        }
        scrollbar(first, count, 3);
    }
}

static bool progress_frame_valid;
static ota_state_t progress_frame_state;
static unsigned progress_frame_percent;
static char progress_frame_message[96];
static bool progress_page(const ota_update_status_t *u)
{
    return page==UPDATE_DETAILS && (u->state==OTA_RECEIVING || u->state==OTA_VALIDATING ||
        u->state==OTA_READY || u->state==OTA_FAILED || u->state==OTA_CANCELLED);
}
bool ui_refresh_needed(void)
{
    ota_update_status_t u=ota_update_status();
    if(!progress_page(&u))return true;
    return !progress_frame_valid || u.state!=progress_frame_state ||
        ota_update_percent(&u)!=progress_frame_percent || strcmp(u.message,progress_frame_message);
}
static void draw_update_progress(const ota_update_status_t *u)
{
    unsigned percent=ota_update_percent(u);char label[24];
    if(u->state==OTA_FAILED || u->state==OTA_CANCELLED) {
        display_text(2,18,u->state==OTA_FAILED?"Update failed":"Cancelled",1,true);
        const char *reason=strstr(u->message,"validation")?"Invalid image":u->message;
        snprintf(label,sizeof(label),"%.20s",reason);display_text(2,34,label,1,true);
        display_text(2,50,"OK: back",1,true);
    } else {
        if(u->state==OTA_READY)snprintf(label,sizeof(label),"Update complete");
        else if(u->state==OTA_VALIDATING || !strcmp(u->message,"Verifying..."))snprintf(label,sizeof(label),"Verifying...");
        else if(!u->received)snprintf(label,sizeof(label),"Preparing update...");
        else snprintf(label,sizeof(label),"Uploading %u%%",percent);
        display_text(2,18,label,1,true);display_rect(2,32,124,10,false,true);
        if(percent)display_rect(4,34,(int)(120u*percent/100u),6,true,true);
        display_text(2,50,u->state==OTA_READY?"Rebooting...":"OK / hold: cancel",1,true);
    }
    progress_frame_valid=true;progress_frame_state=u->state;progress_frame_percent=percent;
    snprintf(progress_frame_message,sizeof(progress_frame_message),"%s",u->message);
}
static void profinet_draw(void)
{
    dcp_summary_t scan = profinet_summary();
    char label[21];
    if (page == PROFINET_PROGRESS) {
        if (dcp_active(scan.state)) {
            uint64_t now = esp_timer_get_time() / 1000;
            unsigned seconds = scan.state == DCP_STARTING ? DCP_WINDOW_MS / 1000 :
                scan.deadline_ms > now ? (scan.deadline_ms - now + 999) / 1000 : 0;
            display_text(2, 18, "Discovering...", 1, true);
            snprintf(label, sizeof(label), "Devices found: %u", scan.count);
            display_text(2, 30, label, 1, true);
            snprintf(label, sizeof(label), "Remaining: %u s", seconds);
            display_text(2, 42, label, 1, true);
            display_text(2, 54, "Hold OK: Cancel", 1, true);
            return;
        }
        page = PROFINET_RESULTS;
    }
    if (page == PROFINET_MENU) {
        const char *items[] = {"Start discovery", "View results", "Back"};
        for (unsigned i = 0; i < 3; ++i) {
            bool selected = i == profinet_selection;
            if (selected) display_rect(0, 16 + 12 * i, 123, 12, true, true);
            display_text(3, 18 + 12 * i, items[i], 1, !selected);
        }
        display_text(2, 54, profinet_notice ? profinet_notice : "PROFINET DCP", 1, true);
    } else if (page == PROFINET_RESULTS) {
        snprintf(label, sizeof(label), "%u %s", scan.count,
                 !scan.valid && scan.started_ms ? "historical" : dcp_partial(&scan) ? "found - partial" : "devices found");
        if (scan.overflow) {
            if (scan.valid) snprintf(label, sizeof(label), "%u found - overflow", scan.count);
            else snprintf(label, sizeof(label), "%u historical - full", scan.count);
        }
        display_text(2, 18, label, 1, true);
        if (!scan.count) {
            display_text(2, 30, scan.started_ms ? "No devices found" : "No discovery yet", 1, true);
            display_text(2, 42, dcp_state_name(scan.state), 1, true);
            display_text(2, 54, "Hold OK: Back", 1, true);
            return;
        }
        if (profinet_index >= scan.count) profinet_index = scan.count - 1;
        unsigned first = profinet_index >= 3 ? profinet_index - 2 : 0;
        for (unsigned i = first; i < scan.count && i < first + 3; ++i) {
            dcp_device_t device;
            if (!profinet_device(scan.xid, i, &device)) break;
            if (device.name[0]) snprintf(label, sizeof(label), "%.20s", device.name);
            else snprintf(label, sizeof(label), "%02X:%02X:%02X:%02X:%02X:%02X",
                          device.mac[0], device.mac[1], device.mac[2], device.mac[3], device.mac[4], device.mac[5]);
            bool selected = i == profinet_index;
            if (selected) display_rect(0, 28 + 12 * (i - first), 123, 12, true, true);
            display_text(3, 30 + 12 * (i - first), label, 1, !selected);
        }
        scrollbar(first, scan.count, 3);
    } else {
        dcp_device_t d;
        line_count = 0;
        if (profinet_device(scan.xid, profinet_index, &d)) {
            add_line("Station name:");
            add_line("%s", !(d.fields & DCP_NAME) ? "Unavailable" : d.name[0] ? d.name : "Empty");
            add_line("Source MAC:");
            add_line("%02X:%02X:%02X:%02X:%02X:%02X", d.mac[0], d.mac[1], d.mac[2], d.mac[3], d.mac[4], d.mac[5]);
            add_line("Device type:");
            add_line("%s", !(d.fields & DCP_VENDOR) ? "Unavailable" : d.vendor[0] ? d.vendor : "Empty");
            if (d.fields & DCP_IP) {
                add_line("IPv4:"); add_line("%u.%u.%u.%u", d.ip[0], d.ip[1], d.ip[2], d.ip[3]);
                add_line("Mask:"); add_line("%u.%u.%u.%u", d.mask[0], d.mask[1], d.mask[2], d.mask[3]);
                add_line("Gateway:"); add_line("%u.%u.%u.%u", d.gateway[0], d.gateway[1], d.gateway[2], d.gateway[3]);
                add_line("IP status: 0x%04X", d.ip_info);
                add_line("%s", dcp_ip_status_name(d.ip_info));
                if (d.ip_info & 0x80) add_line("Reported IP conflict");
            } else add_line("IPv4: unavailable");
            if (d.fields & DCP_ID) { add_line("Vendor ID: 0x%04X", d.vendor_id); add_line("Device ID: 0x%04X", d.device_id); }
            else add_line("IDs: unavailable");
            if (d.fields & DCP_ROLE) add_line("Role flags: 0x%02X", d.role);
            else add_line("Role: unavailable");
            if (d.duplicate_name) add_line("Duplicate name");
            if (d.duplicate_ip) add_line("Duplicate IPv4");
            if (d.text_limited) add_line("Text limited");
        } else add_line("Device unavailable");
        add_line("Hold OK: Back");
        display_text(2, 18, scan.valid ? "PROFINET device" : "Historical device", 1, true);
        if (top + 3 > line_count) top = line_count > 3 ? line_count - 3 : 0;
        for (int i = 0; i < 3 && top + i < line_count; ++i)
            display_text(2, 30 + 12 * i, lines[top + i], 1, true);
        scrollbar(top, line_count, 3);
    }
}

void ui_draw(void)
{
    app_status_t status = status_get();
    display_clear();
    display_rect(0, 0, 128, 12, true, true);
    display_text(2, 2, "Syndetis", 1, false);
    const char *eth_indicator = status.ethernet.state == ETH_ERROR ? "ERR" :
        status.ethernet.state == ETH_INITIALIZING ? "E.." : status.ethernet.link_up ? "EUP" : "EDN";
    display_text(56, 2, eth_indicator, 1, false);
    if (status.web.state == WEB_ON || status.web.state == WEB_STOPPING)
        display_text(86, 2, "WEB ON", 1, false);
    else if (status.web.state == WEB_ERROR) display_text(86, 2, "WEB ER", 1, false);
    else if (status.web.state == WEB_STARTING) display_text(86, 2, "WEB ..", 1, false);
    ota_update_status_t update=ota_update_status();
    if(progress_page(&update)) {draw_update_progress(&update);return;}
    progress_frame_valid=false;
    if(page==SETTINGS) {
        const char *items[]={"Firmware Update","Back"};
        for(int i=0;i<2;++i) {
            bool selected=settings_selection==i;
            if(selected)display_rect(0,16+12*i,123,12,true,true);
            display_text(3,18+12*i,items[i],1,!selected);
        }
    }
    else if (page >= PROFINET_MENU && page <= PROFINET_DEVICE) profinet_draw();
    else if (page >= PING_MENU && page <= PING_RESULTS) ping_draw();
    else if (page >= MANUAL_MENU && page < MANUAL_RESULTS) manual_draw();
    else if (page == MENU || page == WEB_MENU || page == TESTS_MENU || page == DHCP_MENU) {
        const char *web_menu_items[3];
        const char *tests_items[] = {"DHCP Test", "Manual IP", "PING", "Back"};
        const char *dhcp_items[] = {"Start", "Status / Results", "Stop / Release", "Back"};
        int count = page == MENU ? main_item_count : page == TESTS_MENU ? 4 : page == DHCP_MENU ? 4 : web_items(&status.web, web_menu_items);
        if (web_selection >= count) web_selection = count - 1;
        int selected_index = page == MENU ? selection : page == TESTS_MENU ? tests_selection : page == DHCP_MENU ? dhcp_selection : web_selection;
        int first = selected_index >= 4 ? selected_index - 3 : 0;
        for (int row = 0; row < 4 && first + row < count; ++row) {
            int i = first + row;
            bool selected = i == selected_index;
            if (selected) display_rect(0, 16 + 12 * row, 123, 12, true, true);
            display_text(3, 18 + 12 * row, page == MENU ? main_items[i].label : page == TESTS_MENU ? tests_items[i] : page == DHCP_MENU ? dhcp_items[i] : web_menu_items[i], 1, !selected);
        }
        scrollbar(first, count, 4);
    } else if (page == SWITCH_MENU) {
        discovery_summary_t summary = discovery_summary();
        const char *labels[] = {"Link down", "Waiting for advert", "No advert after 30s", "Received/current", "Expired"};
        bool drops = summary.capacity_drops || summary.queue_drops || summary.limited_frames;
        display_text(2, 16, status.ethernet.state != ETH_READY ? "Ethernet unavailable" :
                     !summary.available ? "Discovery error" : drops ? "Limits - see status" : labels[summary.state], 1, true);
        int slots[NEIGHBOR_CAPACITY], neighbors = 0;
        for (int i = 0; i < NEIGHBOR_CAPACITY; ++i) if (summary.used[i]) slots[neighbors++] = i;
        int count = neighbors + 2;
        if (switch_selection >= count) switch_selection = count - 1;
        int first = switch_selection >= 3 ? switch_selection - 2 : 0;
        for (int row = 0; row < 3 && first + row < count; ++row) {
            int i = first + row;
            char label[21];
            if (!i) snprintf(label, sizeof(label), "Status/details");
            else if (i == count - 1) snprintf(label, sizeof(label), "Back");
            else {
                int slot = slots[i - 1];
                snprintf(label, sizeof(label), "%s %c %s", summary.protocols[slot] == PROTOCOL_LLDP ? "LLDP" : "CDP",
                         '1' + slot, summary.expired[slot] ? "Expired" : "Current");
            }
            bool selected = i == switch_selection;
            if (selected) display_rect(0, 28 + 12 * row, 123, 12, true, true);
            display_text(3, 30 + 12 * row, label, 1, !selected);
        }
        scrollbar(first, count, 3);
    } else {
        information_lines(&status);
        if (top + 4 > line_count) top = line_count > 4 ? line_count - 4 : 0;
        for (int row = 0; row < 4 && top + row < line_count; ++row)
            display_text(2, 18 + 12 * row, lines[top + row], 1, true);
        scrollbar(top, line_count, 4);
    }
}
