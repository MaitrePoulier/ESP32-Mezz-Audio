// hal.c
#include "hal.h"
#include "Config.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "HAL_ADS1261";

// Variables globales partagées pour la gestion multi-jauges
spi_device_handle_t ads_spi_handles[5] = {NULL};
uint8_t current_active_device_index = 0;
const uint8_t cs_pins[5] = {SPI_CS1, SPI_CS2, SPI_CS3, SPI_CS4, SPI_CS5};

void _ads126x_set_active_device(uint8_t index) {
    if (index < 5) {
        current_active_device_index = index;
    }
}

void InitSPI(void) {
    static bool is_spi_initialized = false;
    if (is_spi_initialized) return;

    esp_err_t ret;

    for (int i = 0; i < 5; i++) {
        gpio_reset_pin((gpio_num_t)cs_pins[i]);
        gpio_set_direction((gpio_num_t)cs_pins[i], GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)cs_pins[i], 1); 
    }

    spi_bus_config_t buscfg;
    memset(&buscfg, 0, sizeof(spi_bus_config_t));
    buscfg.miso_io_num = SPI_MISO;
    buscfg.mosi_io_num = SPI_MOSI;
    buscfg.sclk_io_num = SPI_SCLK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = 32;

    ret = spi_bus_initialize(JAUGES_HOST, &buscfg, 0);//Disable DMA
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Echec de l'initialisation du bus SPI: %s", esp_err_to_name(ret));
        return;
    }

    spi_device_interface_config_t devcfg;
    memset(&devcfg, 0, sizeof(spi_device_interface_config_t));
    devcfg.clock_speed_hz = JAUGES_FREQUENCY; 
    devcfg.mode = 1;          
    devcfg.queue_size = 1;
    devcfg.flags = 0;
    devcfg.spics_io_num = -1; 

    for (int i = 0; i < 5; i++) {
        ret = spi_bus_add_device(JAUGES_HOST, &devcfg, &ads_spi_handles[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Echec de l'ajout de la jauge virtuelle %d: %s", i, esp_err_to_name(ret));
        }
    }

    is_spi_initialized = true;
}

uint8_t spiSendReceiveByte(uint8_t data) {
    if (current_active_device_index >= 5 || ads_spi_handles[current_active_device_index] == NULL) {
        return 0;
    }

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    
    // SPI_TRANS_USE_RXDATA et TXDATA forcent l'utilisation de la mémoire interne directe de l'ESP32
    // Évite TOUT problème d'alignement mémoire ou de DMA sur les petits paquets
    t.flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA;
    t.length = 8; 
    t.tx_data[0] = data;

	//Met la tâche en pause, donne le contrôle à l'ISR de FreeRTOS, puis réveille la tâche (Génère la latence de 44usec)
    //esp_err_t ret = spi_device_transmit(ads_spi_handles[current_active_device_index], &t);
	//Bloque le CPU de l'ESP32 pendant l'envoi matériel brut sans jamais donner la main à FreeRTOS. Pour des petits paquets (comme \(5\) ou \(6\) octets pour l'ADC)
	esp_err_t ret = spi_device_polling_transmit(ads_spi_handles[current_active_device_index], &t);

    if (ret != ESP_OK) {
        return 0;
    }

    return t.rx_data[0];
}

// Fonction requise par le code de TI pour envoyer/recevoir plusieurs octets d'un coup
void SPI_SendReceive(uint8_t *ulDataTx, uint8_t *ulDataRx, uint8_t byteLength) {
    if (byteLength == 0) return;
    if (current_active_device_index >= 5 || ads_spi_handles[current_active_device_index] == NULL) return;

    // 1. Abaisser manuellement le CS de la jauge active
    setCS(0);

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    
    t.length = byteLength * 8; // Longueur totale du bloc en BITS (ex: 5 octets = 40 bits)
    t.tx_buffer = ulDataTx;    // Pointeur direct vers tout le tableau à envoyer
    t.rx_buffer = ulDataRx;    // Pointeur direct vers le tableau de réception

    // 2. Transmettre tout le bloc d'octets d'un seul coup (Burst) sans aucun temps mort matériel
    esp_err_t ret = spi_device_polling_transmit(ads_spi_handles[current_active_device_index], &t);

    // 3. Relever le CS
    setCS(1);

    if (ret != ESP_OK) {
        handleSPIerror(ulDataTx, ulDataRx, byteLength, "BURST_TRANSFER");
    }
}


void setCS(bool state) {
    uint8_t pin = cs_pins[current_active_device_index];
    
    // Si state == false (0) -> TI veut sélectionner la puce -> Niveau BAS (0)
    // Si state == true (1) -> TI veut libérer la puce -> Niveau HAUT (1)
    gpio_set_level((gpio_num_t)pin, state ? 1 : 0);
}


// Implémentation de la fonction de diagnostic demandée par TI
void handleSPIerror(uint8_t *DataTx, uint8_t *DataRx, uint8_t byteLength, const char *command) {
    ESP_LOGW(TAG, "Erreur SPI détectée lors de la commande: %s", command);
}

/* --- Fonctions Stubs obligatoires pour compiler ads1261.c --- */
void delay_ms(uint32_t time_ms) {
    if (time_ms > 0) vTaskDelay(pdMS_TO_TICKS(time_ms));
}

void delay_us(uint32_t delay) {
    esp_rom_delay_us(delay);
}

bool pollForDRDY(uint32_t timeout) {
    // Exemple basique de scrutation de la broche DataReady
    while (timeout > 0) {
        if (gpio_get_level((gpio_num_t)DataReady) == 0) return true;
        vTaskDelay(pdMS_TO_TICKS(1));
        timeout--;
    }
    return false;
}

void setPWDN(bool state) {}
void setRESET(void) {}
void setSTART(bool state) {}
void enableExternalClock(bool state) {}
void toggleRESET(void) {}
void toggleSTART(void) {}
void setFault(bool state) {}
