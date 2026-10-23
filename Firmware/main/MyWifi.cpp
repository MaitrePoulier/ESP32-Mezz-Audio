#include <cstdio>
#include <cstdlib>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_err.h"
#include "MyWifi.h"
#include "nvs_flash.h"

static const char *TAG = "MyWifi";


void MyWifiInit(void)
{
    ESP_LOGI(TAG, "Initializing WiFi...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    // Initialize TCP/IP network interface
    ESP_ERROR_CHECK(esp_netif_init());

    // Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create default WiFi station interface
    esp_netif_create_default_wifi_sta();

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Configure WiFi as station
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // Start WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialized");
}


//void MyWifiScanTask(void *pvParameters)
void MyWifiScan(void)
{
    //(void)pvParameters;

    printf("Scanning WiFi networks...\r\n");

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi STA mode: %s",
                 esp_err_to_name(err));
        vTaskDelete(nullptr);
        return;
    }

    /*
     * Disconnect from an AP if currently connected.
     */
    esp_wifi_disconnect();

    vTaskDelay(pdMS_TO_TICKS(100));

    /*
     * Configure the scan.
     *
     * NULL SSID  -> scan all networks
     * NULL BSSID -> scan all APs
     * channel 0  -> all channels
     * show_hidden -> include hidden networks
     */
    wifi_scan_config_t scan_config = {};
        scan_config.ssid = nullptr;
        scan_config.bssid = nullptr;
        scan_config.channel = 0;
        scan_config.show_hidden = true;
        scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;

    /*
     * Start scan and wait for it to complete.
     */
    err = esp_wifi_scan_start(&scan_config, true);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s",
                 esp_err_to_name(err));
        vTaskDelete(nullptr);
        return;
    }

    /*
     * Get number of networks found.
     */
    uint16_t number = 0;

    err = esp_wifi_scan_get_ap_num(&number);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get AP count: %s",
                 esp_err_to_name(err));
        vTaskDelete(nullptr);
        return;
    }

    if (number == 0) {
        ESP_LOGI(TAG, "No networks found.");
        vTaskDelete(nullptr);
        return;
    }

    printf("%u networks found\r\n", number);

    /*
     * Allocate space for the AP records.
     */
    wifi_ap_record_t *records =
        static_cast<wifi_ap_record_t *>(
            calloc(number, sizeof(wifi_ap_record_t))
        );

    if (records == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate memory for AP records");
        vTaskDelete(nullptr);
        return;
    }

    /*
     * Get the actual AP records.
     */
    uint16_t max_records = number;

    err = esp_wifi_scan_get_ap_records(&max_records, records);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get AP records: %s",
                 esp_err_to_name(err));

        free(records);
        vTaskDelete(nullptr);
        return;
    }

    /*
     * Display results.
     */
    for (uint16_t i = 0; i < max_records; i++) {

        const char *security =
            (records[i].authmode == WIFI_AUTH_OPEN)
                ? "Open"
                : "Secured";

        printf(
            "%u: %s | RSSI: %d dBm | %s\r\n",
            i + 1,
            reinterpret_cast<const char *>(records[i].ssid),
            records[i].rssi,
            security
        );

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    free(records);
    printf("\r\nScan complete.\r\n");
}