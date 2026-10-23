#include "ESP32.h"

#include <cstdio>
#include <cinttypes>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/sens_reg.h"        // The updated register file


void ESP32_detail(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    const char *model;

    switch (chip_info.model)
    {
        case CHIP_ESP32:
            model = "ESP32";
            break;

        case CHIP_ESP32S2:
            model = "ESP32-S2";
            break;

        case CHIP_ESP32S3:
            model = "ESP32-S3";
            break;

        case CHIP_ESP32C3:
            model = "ESP32-C3";
            break;

        case CHIP_ESP32C2:
            model = "ESP32-C2";
            break;

        case CHIP_ESP32C6:
            model = "ESP32-C6";
            break;

        case CHIP_ESP32H2:
            model = "ESP32-H2";
            break;

        default:
            model = "Unknown";
            break;
    }

    printf("Chip Model:          %s\r\n", model);
    printf("Chip Revision:       %d\r\n", chip_info.revision);

    printf("Chip Frequency:      %d MHz\r\n",
           CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);


    uint8_t mac[6];

    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK)
    {
        printf("WiFi MAC:            "
               "%02X:%02X:%02X:%02X:%02X:%02X\r\n",
               mac[0], mac[1], mac[2],
               mac[3], mac[4], mac[5]);
    }

    printf("Available heap:      %" PRIu32 " Bytes\r\n",
           esp_get_free_heap_size());

    printf("Free PSRAM:          %zu Bytes\r\n",
           heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    printf("Largest free block:  %zu Bytes\r\n",
           heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    printf("Largest PSRAM block: %zu Bytes\r\n",
           heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));

    printf("ESP-IDF version:     %s\r\n",
           esp_get_idf_version());

}


void ESP32_crash(void)
{
    printf("Reset reason: ");

    switch (esp_reset_reason())
    {
        case ESP_RST_UNKNOWN:
            printf("Unknown\r\n");
            break;

        case ESP_RST_POWERON:
            printf("Power-on\r\n");
            break;

        case ESP_RST_EXT:
            printf("External reset\r\n");
            break;

        case ESP_RST_SW:
            printf("Software reset\r\n");
            break;

        case ESP_RST_PANIC:
            printf("Exception/panic\r\n");
            break;

        case ESP_RST_INT_WDT:
            printf("Interrupt watchdog\r\n");
            break;

        case ESP_RST_TASK_WDT:
            printf("Task watchdog\r\n");
            break;

        case ESP_RST_WDT:
            printf("Other watchdog\r\n");
            break;

        case ESP_RST_DEEPSLEEP:
            printf("Wake from deep sleep\r\n");
            break;

        case ESP_RST_BROWNOUT:
            printf("Brownout reset\r\n");
            break;

        case ESP_RST_SDIO:
            printf("SDIO reset\r\n");
            break;

        default:
            printf("Unknown reason\r\n");
            break;
    }
}


//ESP32-WROOM-32E-N8R2
float ESP32_ReadTemp(void)
{
    // 1. Initialize clock divider and power up settings
    SET_PERI_REG_BITS(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_CLK_DIV, 10, SENS_TSENS_CLK_DIV_S);
    SET_PERI_REG_BITS(SENS_SAR_MEAS_WAIT2_REG, SENS_FORCE_XPD_SAR, 3, SENS_FORCE_XPD_SAR_S);
    
    // Reset power state flags
    CLEAR_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_POWER_UP);
    CLEAR_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_DUMP_OUT);
    
    // Power up and force measurement execution
    SET_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_POWER_UP_FORCE);
    SET_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_POWER_UP);
    
    // Wait for sensor hardware to stabilize
    vTaskDelay(100);
    
    // Signal a dump of the measurement result out to the register
    SET_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_DUMP_OUT);
    vTaskDelay(5);

    // 2. Read the raw 8-bit value from the slave address register
    uint32_t raw_val = GET_PERI_REG_BITS2(SENS_SAR_SLAVE_ADDR3_REG, SENS_TSENS_OUT, SENS_TSENS_OUT_S);

    // 3. Convert raw hardware Fahrenheit approximations into Celsius
    float fahrenheit = (float)raw_val;
    float celsius = (fahrenheit - 32.0f) / 1.8f;

    return celsius;
}
