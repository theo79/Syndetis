#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "firmware_update.h"

#define AUTHORIZATION_US (300LL * 1000000)
#define PREFIX_SIZE (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static int64_t authorized_until;
static int64_t upload_deadline;
static ota_status_t state;
static const char *TAG = "SYNDETIS";

bool firmware_update_authorize(void)
{
    bool allowed;
    portENTER_CRITICAL(&lock);
    allowed = state.phase != OTA_RECEIVING && state.phase != OTA_SUCCESS;
    if (allowed) {
        authorized_until = esp_timer_get_time() + AUTHORIZATION_US;
        state.phase = OTA_IDLE;
        ++state.revision;
        state.percent = 0;
        snprintf(state.message, sizeof(state.message), "Enabled for 5 minutes");
    }
    portEXIT_CRITICAL(&lock);
    if (allowed) ESP_LOGI(TAG, "Firmware update physically authorized for 5 minutes");
    return allowed;
}

void firmware_update_status(ota_status_t *status)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    *status = state;
    status->unlocked = authorized_until > now && state.phase == OTA_IDLE;
    status->seconds_left = status->unlocked ? (authorized_until - now + 999999) / 1000000 : 0;
    portEXIT_CRITICAL(&lock);
}

static bool claim_upload(void)
{
    int64_t now = esp_timer_get_time();
    bool allowed;
    portENTER_CRITICAL(&lock);
    allowed = authorized_until > now && state.phase == OTA_IDLE;
    if (allowed) {
        upload_deadline = authorized_until;
        authorized_until = 0; /* Single-use: every attempt consumes the window. */
        state.phase = OTA_RECEIVING;
        ++state.revision;
        state.percent = 0;
        snprintf(state.message, sizeof(state.message), "Receiving firmware");
    }
    portEXIT_CRITICAL(&lock);
    return allowed;
}

static void set_result(ota_phase_t phase, const char *message, unsigned percent)
{
    portENTER_CRITICAL(&lock);
    state.phase = phase;
    ++state.revision;
    state.percent = percent;
    authorized_until = 0;
    snprintf(state.message, sizeof(state.message), "%s", message);
    portEXIT_CRITICAL(&lock);
}

static esp_err_t reject(httpd_req_t *req, const char *status, const char *message)
{
    set_result(OTA_FAILED, message, 0);
    ESP_LOGW(TAG, "OTA rejected: %s", message);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_sendstr(req, message);
    return ESP_FAIL; /* Close socket; an unread binary must not become another request. */
}

static bool header_valid(const uint8_t *prefix)
{
    esp_image_header_t image;
    esp_image_segment_header_t segment;
    esp_app_desc_t app;
    memcpy(&image, prefix, sizeof(image));
    memcpy(&segment, prefix + sizeof(image), sizeof(segment));
    memcpy(&app, prefix + sizeof(image) + sizeof(segment), sizeof(app));
    return image.magic == ESP_IMAGE_HEADER_MAGIC && image.chip_id == ESP_CHIP_ID_ESP32 &&
           image.segment_count > 0 && segment.data_len >= sizeof(app) &&
           app.magic_word == ESP_APP_DESC_MAGIC_WORD &&
           memcmp(app.project_name, "syndetis_finder", sizeof("syndetis_finder")) == 0 &&
           esp_ota_check_image_validity(ESP_PARTITION_TYPE_APP, &image, &app) == ESP_OK;
}

/* Timeouts, disconnects and window expiration all abort, without boot selection. */
static int receive(httpd_req_t *req, uint8_t *buffer, size_t size)
{
    for (unsigned retries = 0; retries < 3; ++retries) {
        if (esp_timer_get_time() >= upload_deadline) return -1;
        int received = httpd_req_recv(req, (char *)buffer, size);
        if (received != HTTPD_SOCK_ERR_TIMEOUT) return received;
    }
    return -1;
}

esp_err_t firmware_update_upload(httpd_req_t *req)
{
    if (!claim_upload()) {
        ESP_LOGW(TAG, "Firmware upload attempted while locked");
        portENTER_CRITICAL(&lock);
        if (state.phase != OTA_RECEIVING && state.phase != OTA_SUCCESS) {
            state.phase = OTA_FAILED;
            ++state.revision;
            snprintf(state.message, sizeof(state.message), "Update locked. Enable from System.");
        }
        portEXIT_CRITICAL(&lock);
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_set_hdr(req, "Connection", "close");
        httpd_resp_sendstr(req, "Firmware update locked. Enable it from the Syndetis device.");
        return ESP_FAIL;
    }
    char content_type[48];
    char upload_header[16];
    /* Custom header and exact content type prevent cross-site HTML form uploads.
     * No CORS permission is granted; the embedded page sends a same-origin request.
     */
    if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) != ESP_OK ||
        strcmp(content_type, "application/octet-stream") != 0 ||
        httpd_req_get_hdr_value_str(req, "X-Syndetis-Upload", upload_header, sizeof(upload_header)) != ESP_OK ||
        strcmp(upload_header, "1") != 0) {
        return reject(req, "415 Unsupported Media Type", "Use the Syndetis browser page to upload an application .bin.");
    }
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!running || !target || target->address == running->address ||
        target->type != ESP_PARTITION_TYPE_APP) {
        return reject(req, "500 Internal Server Error", "No inactive OTA application partition available.");
    }
    if (req->content_len < PREFIX_SIZE || req->content_len > target->size) {
        return reject(req, "400 Bad Request", "Image is empty, too short, or exceeds the OTA slot.");
    }
    uint8_t buffer[4096];
    size_t prefix_received = 0;
    while (prefix_received < PREFIX_SIZE) {
        int n = receive(req, buffer + prefix_received, PREFIX_SIZE - prefix_received);
        if (n <= 0) return reject(req, "408 Request Timeout", "Upload interrupted or authorization expired.");
        prefix_received += n;
    }
    if (!header_valid(buffer)) {
        return reject(req, "400 Bad Request", "Expected an ESP32 syndetis_finder application binary.");
    }
    esp_ota_handle_t handle;
    esp_err_t err = esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (err != ESP_OK) return reject(req, "500 Internal Server Error", esp_err_to_name(err));
    size_t written = PREFIX_SIZE;
    err = esp_ota_write(handle, buffer, written);
    while (err == ESP_OK && written < req->content_len) {
        size_t remaining = req->content_len - written;
        int n = receive(req, buffer, remaining < sizeof(buffer) ? remaining : sizeof(buffer));
        if (n <= 0) { err = ESP_ERR_TIMEOUT; break; }
        err = esp_ota_write(handle, buffer, n);
        if (err == ESP_OK) {
            written += n;
            portENTER_CRITICAL(&lock);
            state.percent = written * 100 / req->content_len;
            portEXIT_CRITICAL(&lock);
        }
    }
    if (err != ESP_OK || written != req->content_len || esp_timer_get_time() >= upload_deadline) {
        esp_ota_abort(handle);
        return reject(req, "400 Bad Request", err == ESP_OK ? "Authorization expired." : esp_err_to_name(err));
    }
    err = esp_ota_end(handle); /* Frees handle on both success and failure. */
    if (err != ESP_OK) return reject(req, "400 Bad Request", esp_err_to_name(err));
    if (esp_timer_get_time() >= upload_deadline) {
        return reject(req, "408 Request Timeout", "Authorization expired before image validation completed.");
    }
    err = esp_ota_set_boot_partition(target); /* Only after complete image validation. */
    if (err != ESP_OK) return reject(req, "500 Internal Server Error", esp_err_to_name(err));
    set_result(OTA_SUCCESS, "Update OK. Rebooting...", 100);
    ESP_LOGI(TAG, "OTA validated; rebooting into %s", target->label);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_sendstr(req, "Update OK. Rebooting... Reopen this page after the device restarts.");
    vTaskDelay(pdMS_TO_TICKS(1500)); /* Let the response and OLED success screen finish. */
    esp_restart();
    return ESP_OK;
}

esp_err_t firmware_update_confirm_boot(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t image_state;
    if (!running) return ESP_ERR_NOT_FOUND;
    esp_err_t err = esp_ota_get_state_partition(running, &image_state);
    /* Initial serial boot uses erased otadata; no pending image exists yet. */
    if (err == ESP_ERR_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    if (image_state == ESP_OTA_IMG_PENDING_VERIFY) {
        err = esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "Startup self-test / OTA confirmation: %s", esp_err_to_name(err));
    }
    return err;
}
