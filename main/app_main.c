#include "ethernet_rx.h"
#include "profinet.h"
/*
 * Syndetis firmware foundation: WT32-ETH01 Ethernet, UI and OTA
 * Target: ESP32 (WT32-ETH01, LAN8720 PHY, RMII), ESP-IDF v5.3+
 */

#include <assert.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_eth.h"
#include "esp_eth_phy_lan87xx.h"
#include "esp_eth_mac_esp.h"
#include "esp_eth_netif_glue.h"
#include "esp_eth_phy.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "ui_buttons.h"
#include "ui_oled.h"
#include "ui_menu.h"
#include "network_status.h"
#include "web_server.h"
#include "firmware_update.h"
#include "esp_app_desc.h"
#include "autotest.h"
#include "discovery.h"
#include "switch_info.h"

static const char *TAG = "SYNDETIS";

/* WT32-ETH01 Ethernet pins */
#define ETH_PHY_ADDR       1
#define ETH_PHY_POWER_GPIO GPIO_NUM_16  /* Enables the external 50 MHz oscillator */
#define ETH_MDC_GPIO       GPIO_NUM_23
#define ETH_MDIO_GPIO      GPIO_NUM_18
#define ETH_REF_CLK_GPIO GPIO_NUM_0

void app_main(void)
{
    ESP_LOGI(TAG, "Syndetis %s", esp_app_get_description()->version);

    /* NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* TCP/IP stack and default event loop */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /*
     * WT32-ETH01: GPIO16 is not a PHY reset line, it powers the external
     * 50 MHz oscillator. It must be high and the clock stable BEFORE the
     * EMAC is initialized.
     */
    gpio_config_t pwr_cfg = {
        .pin_bit_mask = 1ULL << ETH_PHY_POWER_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&pwr_cfg));
    ESP_ERROR_CHECK(gpio_set_level(ETH_PHY_POWER_GPIO, 1));
    vTaskDelay(pdMS_TO_TICKS(100));

    /* MAC (ESP32 internal EMAC, RMII) */
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac_config.smi_gpio.mdc_num = ETH_MDC_GPIO;
    emac_config.smi_gpio.mdio_num = ETH_MDIO_GPIO;
    /* The oscillator drives the PHY and the ESP32: REF_CLK is an input on GPIO0 */
    emac_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    emac_config.clock_config.rmii.clock_gpio = ETH_REF_CLK_GPIO;
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);
    assert(mac != NULL);

    /* PHY (LAN8720 via the lan87xx driver), address 1, no reset pin wired */
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = ETH_PHY_ADDR;
    phy_config.reset_gpio_num = -1;
    esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_config);
    assert(phy != NULL);

    /* Ethernet driver */
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &eth_handle));

    /* Netif + glue */
    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&netif_config);
    assert(eth_netif != NULL);
    esp_eth_netif_glue_handle_t eth_glue = esp_eth_new_netif_glue(eth_handle);
    assert(eth_glue != NULL);
    /* Publish link/IP generation before the SDK netif event handlers run.
     * SDK link-down processing may wait for TCP/IP; UI cancellation must not. */
    ESP_ERROR_CHECK(network_status_init(eth_handle));
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, eth_glue));

    /* Optional passive receiver: attachment owns RX until this succeeds.
     * Install only before start; discovery failure must not abort networking. */
    ret = switch_info_init(eth_handle, eth_netif);
    if (ret != ESP_OK) ESP_LOGW(TAG, "Switch Info unavailable: %s", esp_err_to_name(ret));

    ret = ethernet_rx_init(eth_handle, eth_netif);
    if (ret != ESP_OK) ESP_LOGW(TAG, "Shared RX unavailable: %s", esp_err_to_name(ret));
    else {
        ret = profinet_init(eth_handle);
        if (ret != ESP_OK) ESP_LOGW(TAG, "PROFINET unavailable: %s", esp_err_to_name(ret));
    }

    /* Start Ethernet */
    ESP_ERROR_CHECK(esp_eth_start(eth_handle));

    /* Core self-test: NVS, netif/event loop, MAC/PHY driver and HTTP startup.
     * No Ethernet cable, DHCP, OLED or button success is required.
     */
    ESP_ERROR_CHECK(web_server_start());
    ESP_ERROR_CHECK(firmware_update_confirm_boot());

    ret = autotest_init();
    if (ret != ESP_OK) ESP_LOGW(TAG, "AutoTest unavailable: %s", esp_err_to_name(ret));
    ret = discovery_init();
    if (ret != ESP_OK) ESP_LOGW(TAG, "Discovery unavailable: %s", esp_err_to_name(ret));

    /* Optional UI failures never abort Ethernet or HTTP. */
    ret = ui_oled_init();
    bool display_ready = ret == ESP_OK;
    if (!display_ready) ESP_LOGW(TAG, "OLED unavailable: %s", esp_err_to_name(ret));
    ret = ui_menu_init(display_ready);
    if (ret != ESP_OK) ESP_LOGW(TAG, "Menu unavailable: %s", esp_err_to_name(ret));
    ret = ui_buttons_init(ui_menu_button, NULL);
    if (ret != ESP_OK) ESP_LOGW(TAG, "Buttons unavailable: %s", esp_err_to_name(ret));
}
