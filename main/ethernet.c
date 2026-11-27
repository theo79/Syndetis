#include "ethernet.h"
#include "board.h"
#include "status.h"
#include "discovery.h"
#include "dhcp_test.h"
#include "receive_dispatch.h"
#include <stdio.h>
#include <stdlib.h>
#include "driver/gpio.h"
#include "esp_eth.h"
#include "esp_eth_phy_lan87xx.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static esp_eth_handle_t driver;

static esp_err_t receive_frame(esp_eth_handle_t handle, uint8_t *buffer,
                               uint32_t length, void *context)
{
    (void)handle; (void)length; (void)context;
    ethernet_dispatch(buffer, length);
    return ESP_OK;
}

static void ethernet_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;
    if (!data || *(esp_eth_handle_t *)data != driver) return;
    if (id == ETHERNET_EVENT_CONNECTED) {
        eth_speed_t speed;
        eth_duplex_t duplex;
        // These GET ioctls copy cached negotiated values; no PHY polling here.
        int mbps = 0, mode = 0;
        if (esp_eth_ioctl(driver, ETH_CMD_G_SPEED, &speed) == ESP_OK)
            mbps = speed == ETH_SPEED_100M ? 100 : speed == ETH_SPEED_10M ? 10 : 0;
        if (esp_eth_ioctl(driver, ETH_CMD_G_DUPLEX_MODE, &duplex) == ESP_OK)
            mode = duplex == ETH_DUPLEX_FULL ? 2 : 1;
        status_ethernet_link(true, mbps, mode);
        discovery_link(true);
        dhcp_test_link(true);
        profinet_link(true);
    } else if (id == ETHERNET_EVENT_DISCONNECTED || id == ETHERNET_EVENT_STOP) {
        profinet_link(false);
        status_ethernet_link(false, 0, 0);
        discovery_link(false);
        dhcp_test_link(false);
    }
}

esp_err_t ethernet_start(void)
{
    const char *step = "Clock enable";
    esp_err_t err;
    ethernet_status_t state = {.state = ETH_INITIALIZING};
    esp_eth_mac_t *mac = NULL;
    esp_eth_phy_t *phy = NULL;
    esp_event_handler_instance_t event_instance = NULL;
    const gpio_config_t power = {
        .pin_bit_mask = 1ULL << BOARD_ETH_CLOCK_ENABLE,
        .mode = GPIO_MODE_OUTPUT,
    };
    if ((err = gpio_config(&power)) != ESP_OK) goto failed;
    if ((err = gpio_set_level(BOARD_ETH_CLOCK_ENABLE, 1)) != ESP_OK) goto failed;
    vTaskDelay(pdMS_TO_TICKS(10));

    step = "MAC allocation";
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t emac = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac.smi_gpio.mdc_num = BOARD_ETH_MDC;
    emac.smi_gpio.mdio_num = BOARD_ETH_MDIO;
    emac.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    emac.clock_config.rmii.clock_gpio = BOARD_ETH_CLOCK_INPUT;
    mac = esp_eth_mac_new_esp32(&emac, &mac_config);
    if (!mac) { err = ESP_ERR_NO_MEM; goto failed; }
    step = "PHY allocation";
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = BOARD_ETH_PHY_ADDRESS;
    phy_config.reset_gpio_num = -1; // Board nRST is hardware controlled, never GPIO5.
    phy = esp_eth_phy_new_lan87xx(&phy_config);
    if (!phy) { err = ESP_ERR_NO_MEM; goto failed; }
    step = "Driver install";
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    step = "Discovery worker";
    if ((err = discovery_init()) != ESP_OK) goto failed;
    config.stack_input = receive_frame;
    config.check_link_period_ms = 500;
    step = "Driver install";
    if ((err = esp_eth_driver_install(&config, &driver)) != ESP_OK) goto failed;
    // LLDP and CDP use multicast destinations that aren't the local MAC.
    // Supported by the pinned ESP32 EMAC: accept multicast, retain unicast filtering.
    step = "Multicast filter";
    bool all_multicast = true;
    if ((err = esp_eth_ioctl(driver, ETH_CMD_S_ALL_MULTICAST, &all_multicast)) != ESP_OK) goto failed;
    step = "Read MAC";
    if ((err = esp_eth_ioctl(driver, ETH_CMD_G_MAC_ADDR, state.mac)) != ESP_OK) goto failed;
    state.mac_valid = true;
    // Failure of the optional test does not stop hardware-proven passive discovery.
    dhcp_test_init(driver, state.mac);
    step = "Event registration";
    if ((err = esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                 ethernet_event, NULL, &event_instance)) != ESP_OK) goto failed;
    // Set ready before start so an immediate link event cannot be overwritten.
    state.state = ETH_READY;
    status_set_ethernet(&state);
    step = "Driver start";
    if ((err = esp_eth_start(driver)) != ESP_OK) goto failed;
    profinet_init(driver, state.mac);
    ESP_LOGI("ethernet", "LAN8720 monitoring started in passive mode; DHCP requires explicit Start");
    return ESP_OK;

failed:
    profinet_link(false);
    discovery_link(false);
    // No successfully started Ethernet driver is torn down during web operations.
    if (event_instance) esp_event_handler_instance_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, event_instance);
    if (driver) {
        esp_err_t cleanup = esp_eth_driver_uninstall(driver);
        if (cleanup == ESP_OK) driver = NULL;
        else ESP_LOGE("ethernet", "Driver cleanup: %s", esp_err_to_name(cleanup));
    }
    if (!driver) {
        if (phy) phy->del(phy);
        if (mac) mac->del(mac);
        gpio_set_level(BOARD_ETH_CLOCK_ENABLE, 0);
    }
    state.state = ETH_ERROR;
    state.link_up = false;
    state.speed_mbps = state.duplex = 0;
    state.error = err;
    snprintf(state.error_step, sizeof(state.error_step), "%s", step);
    status_set_ethernet(&state);
    ESP_LOGE("ethernet", "%s: %s", step, esp_err_to_name(err));
    return err;
}
