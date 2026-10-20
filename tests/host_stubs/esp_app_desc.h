#ifndef TEST_ESP_APP_DESC_H
#define TEST_ESP_APP_DESC_H
typedef struct { char version[32]; } esp_app_desc_t;
static inline const esp_app_desc_t *esp_app_get_description(void) {
    static const esp_app_desc_t desc = {"0.3.0"}; return &desc;
}
#endif
