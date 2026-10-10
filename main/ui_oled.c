#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "board_pins.h"
#include "ui_oled.h"

#define OLED_ADDRESS 0x3C
#define OLED_WIDTH 128
#define OLED_PAGES 8
#define SH1106_RAM_WIDTH 132
#define SH1106_COLUMN_OFFSET 2
#define I2C_TIMEOUT_MS 100

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t device;
static uint8_t framebuffer[OLED_WIDTH * OLED_PAGES];

/* 5x7 column font. Lowercase labels are rendered as uppercase on this display. */
typedef struct { char character; uint8_t columns[5]; } glyph_t;
static const glyph_t font[] = {
    {'A', {0x7e,0x11,0x11,0x11,0x7e}},
    {'B', {0x7f,0x49,0x49,0x49,0x36}},
    {'C', {0x3e,0x41,0x41,0x41,0x22}},
    {'F', {0x7f,0x09,0x09,0x09,0x01}},
    {'G', {0x3e,0x41,0x49,0x49,0x7a}},
    {'J', {0x20,0x40,0x41,0x3f,0x01}},
    {'M', {0x7f,0x02,0x0c,0x02,0x7f}},
    {'Q', {0x3e,0x41,0x51,0x21,0x5e}},
    {'R', {0x7f,0x09,0x19,0x29,0x46}},
    {'V', {0x1f,0x20,0x40,0x20,0x1f}},
    {'W', {0x7f,0x20,0x18,0x20,0x7f}},
    {'X', {0x63,0x14,0x08,0x14,0x63}},
    {'Z', {0x61,0x51,0x49,0x45,0x43}},
    {'>', {0x00,0x41,0x22,0x14,0x08}},
    {'-', {0x08,0x08,0x08,0x08,0x08}},
    {'/', {0x20,0x10,0x08,0x04,0x02}},
    {'%', {0x63,0x13,0x08,0x64,0x63}},
    {'S', {0x46,0x49,0x49,0x49,0x31}},
    {'Y', {0x07,0x08,0x70,0x08,0x07}},
    {'N', {0x7f,0x02,0x04,0x08,0x7f}},
    {'D', {0x7f,0x41,0x41,0x22,0x1c}},
    {'E', {0x7f,0x49,0x49,0x49,0x41}},
    {'T', {0x01,0x01,0x7f,0x01,0x01}},
    {'I', {0x00,0x41,0x7f,0x41,0x00}},
    {'O', {0x3e,0x41,0x41,0x41,0x3e}},
    {'L', {0x7f,0x40,0x40,0x40,0x40}},
    {'K', {0x7f,0x08,0x14,0x22,0x41}},
    {'H', {0x7f,0x08,0x08,0x08,0x7f}},
    {'U', {0x3f,0x40,0x40,0x40,0x3f}},
    {'P', {0x7f,0x09,0x09,0x09,0x06}},
    {':', {0x00,0x36,0x36,0x00,0x00}},
    {'.', {0x00,0x60,0x60,0x00,0x00}},
    {'0', {0x3e,0x51,0x49,0x45,0x3e}},
    {'1', {0x00,0x42,0x7f,0x40,0x00}},
    {'2', {0x42,0x61,0x51,0x49,0x46}},
    {'3', {0x21,0x41,0x45,0x4b,0x31}},
    {'4', {0x18,0x14,0x12,0x7f,0x10}},
    {'5', {0x27,0x45,0x45,0x45,0x39}},
    {'6', {0x3c,0x4a,0x49,0x49,0x30}},
    {'7', {0x01,0x71,0x09,0x05,0x03}},
    {'8', {0x36,0x49,0x49,0x49,0x36}},
    {'9', {0x06,0x49,0x49,0x29,0x1e}},
};

static void draw_text(unsigned page, const char *text)
{
    unsigned x = 0;
    while (*text && page < OLED_PAGES && x + 5 <= OLED_WIDTH) {
        for (size_t i = 0; i < sizeof(font) / sizeof(font[0]); ++i) {
            if (font[i].character == toupper((unsigned char)*text)) {
                memcpy(&framebuffer[page * OLED_WIDTH + x], font[i].columns, 5);
                break;
            }
        }
        ++text;
        x += 6; /* A blank column, including spaces, separates characters. */
    }
}

static esp_err_t commands(const uint8_t *bytes, size_t count)
{
    uint8_t packet[32] = {0x00}; /* Co=0, D/C=0: command stream. */
    if (count > sizeof(packet) - 1) return ESP_ERR_INVALID_SIZE;
    memcpy(packet + 1, bytes, count);
    return i2c_master_transmit(device, packet, count + 1, I2C_TIMEOUT_MS);
}

static esp_err_t write_page(unsigned page, unsigned column,
                            const uint8_t *pixels, size_t count)
{
    /* SH1106 page plus separate low/high column addresses, never 0x20/21/22. */
    const uint8_t address[] = {
        0xB0 | page, column & 0x0F, 0x10 | (column >> 4),
    };
    esp_err_t err = commands(address, sizeof(address));
    if (err != ESP_OK) return err;
    uint8_t packet[SH1106_RAM_WIDTH + 1] = {0x40}; /* D/C=1: RAM data. */
    if (count > SH1106_RAM_WIDTH) return ESP_ERR_INVALID_SIZE;
    memcpy(packet + 1, pixels, count);
    return i2c_master_transmit(device, packet, count + 1, I2C_TIMEOUT_MS);
}

static esp_err_t flush(void)
{
    for (unsigned page = 0; page < OLED_PAGES; ++page) {
        esp_err_t err = write_page(page, SH1106_COLUMN_OFFSET,
                                  &framebuffer[page * OLED_WIDTH], OLED_WIDTH);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

esp_err_t ui_oled_show_lines(const char *lines[8])
{
    if (!device) return ESP_ERR_INVALID_STATE;
    memset(framebuffer, 0, sizeof(framebuffer));
    for (unsigned page = 0; page < OLED_PAGES; ++page) {
        if (lines[page]) draw_text(page, lines[page]);
    }
    return flush();
}

esp_err_t ui_oled_init(void)
{
    if (bus) return ESP_ERR_INVALID_STATE;
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = OLED_SDA_GPIO,
        .scl_io_num = OLED_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_config, &bus);
    if (err != ESP_OK) return err;
    err = i2c_master_probe(bus, OLED_ADDRESS, I2C_TIMEOUT_MS);
    if (err != ESP_OK) goto fail;
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_ADDRESS,
        .scl_speed_hz = 100000,
    };
    err = i2c_master_bus_add_device(bus, &config, &device);
    if (err != ESP_OK) goto fail;
    /* SH1106 initialization: 64 rows, segment/COM remap, internal DC-DC.
     * No SSD1306 charge-pump (0x8D) or horizontal-addressing commands.
     * No reset pin is wired; the module supplies power-on reset.
     * Datasheet: https://www.displayfuture.com/Display/datasheet/controller/SH1106.pdf
     */
    vTaskDelay(pdMS_TO_TICKS(100));
    const uint8_t init[] = {
        0xAE,             /* Display off while configuring and clearing. */
        0xD5, 0x80,       /* Clock divider/oscillator. */
        0xA8, 0x3F,       /* Multiplex: 64 rows. */
        0xD3, 0x00, 0x40, /* Display offset 0, start line 0. */
        0xAD, 0x8B,       /* SH1106 internal DC-DC on. */
        0xA1, 0xC8,       /* Segment and COM scan orientation. */
        0xDA, 0x12,       /* Alternative COM configuration. */
        0x81, 0x80,       /* Contrast. */
        0xD9, 0x22,       /* Precharge period. */
        0xDB, 0x35,       /* VCOM deselect level. */
        0xA4, 0xA6,       /* RAM display, normal (not inverse). */
    };
    err = commands(init, sizeof(init));
    if (err != ESP_OK) goto fail;
    vTaskDelay(pdMS_TO_TICKS(100));
    /* Explicitly clear all 132 columns on all eight pages, including margins. */
    const uint8_t blank[SH1106_RAM_WIDTH] = {0};
    for (unsigned page = 0; page < OLED_PAGES; ++page) {
        err = write_page(page, 0, blank, sizeof(blank));
        if (err != ESP_OK) goto fail;
    }
    const uint8_t on = 0xAF;
    err = commands(&on, 1);
    if (err != ESP_OK) goto fail;

    return ESP_OK;
fail:
    if (device) { i2c_master_bus_rm_device(device); device = NULL; }
    i2c_del_master_bus(bus);
    bus = NULL;
    return err;
}
