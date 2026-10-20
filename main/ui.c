#include "ui.h"
#include "board.h"
#include "buttons.h"
#include "display.h"
#include "access_config.h"
#include "services.h"
#include "status.h"
#include "discovery.h"
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

enum { MENU, LINK_INFO, SWITCH_MENU, SWITCH_STATUS, SWITCH_NEIGHBOR, DEVICE_INFO, WEB_MENU, WEB_DETAILS, ABOUT };
static int page, selection, web_selection, switch_selection, neighbor_index, top;
static char lines[128][21];
static int line_count;

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
    add_line("IP: Not configured");
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
    if (page == DEVICE_INFO) device_lines();
    else if (page == LINK_INFO) link_lines(&status->ethernet);
    else if (page == SWITCH_STATUS || page == SWITCH_NEIGHBOR) switch_lines();
    else if (page == WEB_DETAILS) web_lines(&status->web);
    else {
        line_count = 0;
        add_line("Syndetis");
        add_line("ethernet field tool");
        add_line("%s", esp_app_get_description()->version);
        add_line("OK / hold OK: back");
    }
}

void ui_init(void) { page = MENU; selection = 0; web_selection = 0; top = 0; }

void ui_input(uint8_t buttons)
{
    app_status_t status = status_get();
    if (buttons & BUTTON_BACK) {
        if (page == WEB_DETAILS) page = WEB_MENU;
        else if (page == SWITCH_STATUS || page == SWITCH_NEIGHBOR) page = SWITCH_MENU;
        else if (page != MENU) page = MENU;
        top = 0;
        return; // Back never enables/disables access or activates a selected item.
    }
    if (page == MENU) {
        if (buttons == BUTTON_UP && selection > 0) --selection;
        if (buttons == BUTTON_DOWN && selection < 4) ++selection;
        if (buttons == BUTTON_OK) {
            const int pages[] = {LINK_INFO, SWITCH_MENU, DEVICE_INFO, WEB_MENU, ABOUT};
            page = pages[selection];
            top = 0;
            if (page == WEB_MENU) web_selection = 0;
            if (page == SWITCH_MENU) switch_selection = 0;
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
            page = page == WEB_DETAILS ? WEB_MENU :
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
    if (page == MENU || page == WEB_MENU) {
        const char *main_items[] = {"Link & IP", "Switch Info", "Device Info", "Web Access", "About"};
        const char *web_menu_items[3];
        int count = page == MENU ? 5 : web_items(&status.web, web_menu_items);
        if (web_selection >= count) web_selection = count - 1;
        int selected_index = page == MENU ? selection : web_selection;
        int first = selected_index >= 4 ? selected_index - 3 : 0;
        for (int row = 0; row < 4 && first + row < count; ++row) {
            int i = first + row;
            bool selected = i == selected_index;
            if (selected) display_rect(0, 16 + 12 * row, 123, 12, true, true);
            display_text(3, 18 + 12 * row, page == MENU ? main_items[i] : web_menu_items[i], 1, !selected);
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
