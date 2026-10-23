#pragma GCC diagnostic ignored "-Wdeprecated-enum-enum-conversion"
#pragma GCC diagnostic ignored "-Wenum-conversion"

#include "LcdDriver.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h" 
#include "driver/spi_master.h" // Ajouté pour le support SPI complet
#include "esp_timer.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h" // Assure l'accès à esp_random()

#include <stdint.h>
#include <stddef.h>

#include "lvgl.h" 

static const char *TAG = "MHS4001";
static spi_device_handle_t s_spi = nullptr;
static spi_device_handle_t s_touch_spi = nullptr; // Handle pour le tactile
static uint16_t *s_dma_buffer = nullptr;
static bool s_initialized = false;


// ============================================================================
// DMA
// ============================================================================

// 40 lines:
// 480 * 40 * 2 = 38,400 bytes
#define LCD_DMA_LINES       10
#define LCD_DMA_PIXELS      480 // Exactement une ligne complète de l'écran MHS4001
#define LCD_DMA_BYTES       (LCD_DMA_PIXELS * sizeof(uint16_t))


// ============================================================================
// GPIO helpers
// ============================================================================

static inline void dc_command()
{
    gpio_set_level((gpio_num_t)TFT_DC, 0);
}

static inline void dc_data()
{
    gpio_set_level((gpio_num_t)TFT_DC, 1);
}

// ============================================================================
// SPI Write Primitive
// ============================================================================
static esp_err_t spi_write_16bits(uint16_t value)
{
    spi_transaction_t t = {};
    t.length = 16;
    t.tx_buffer = &value;
    return spi_device_transmit(s_spi, &t);
}

static esp_err_t write_command(uint8_t command)
{
    dc_command();
    const uint16_t tx = ((uint16_t)command) << 8;
    return spi_write_16bits(tx);
}

static esp_err_t write_data8(uint8_t data)
{
    dc_data();
    const uint16_t tx = ((uint16_t)data) << 8;
    return spi_write_16bits(tx);
}

// ============================================================================
// Primitives SPI (Tactile XPT2046)
// ============================================================================
static uint16_t xpt2046_read_cmd(uint8_t cmd)
{
    uint8_t data[3] = { cmd, 0x00, 0x00 };
    spi_transaction_t t = {};
    t.length = 24; // 3 octets
    t.tx_buffer = data;
    t.rx_buffer = data;

    gpio_set_level((gpio_num_t)TOUCH_CS, 0); // Activer le CS tactile
    spi_device_polling_transmit(s_touch_spi, &t);
    gpio_set_level((gpio_num_t)TOUCH_CS, 1); // Désactiver le CS tactile

    // Extraction de la valeur 12 bits reçue dans les octets 1 et 2
    return ((data[1] << 8) | data[2]) >> 3;
}


// ============================================================================
// ST7796 Initialization Sequence (Compact Table)
// ============================================================================

struct lcd_init_cmd_t {
    uint8_t  cmd;       // Code de la commande
    uint8_t  data[16];  // Arguments (maximum 16 octets)
    uint8_t  data_len;  // Nombre d'arguments (0 si aucun)
    uint16_t delay_ms;  // Délai d'attente après la commande (0 si aucun)
};

static const lcd_init_cmd_t st7796_init_sequence[] = {
    { 0x01, {0}, 0, 120 }, // Software reset + 120ms delay
    { 0x11, {0}, 0, 120 }, // Sleep out + 120ms delay
    
    { 0xF0, {0xC3}, 1, 0 },
    { 0xF0, {0x96}, 1, 0 },
    { 0x36, {0xE8}, 1, 0 }, // Memory access control (Paysage inversé 180°)
    { 0x3A, {0x55}, 1, 0 }, // RGB565 format
    { 0xB4, {0x01}, 1, 0 },
    { 0xB6, {0x80, 0x02, 0x3B}, 3, 0 },
    { 0xC1, {0x06}, 1, 0 },
    { 0xC2, {0xA7}, 1, 0 },
    { 0xC5, {0x18}, 1, 0 },
    { 0xE8, {0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33}, 8, 0 },
    { 0xE0, {0xF0, 0x09, 0x0B, 0x06, 0x04, 0x15, 0x2F, 0x54, 0x42, 0x3C, 0x17, 0x14, 0x18, 0x1B}, 14, 0 },
    { 0xE1, {0xE0, 0x09, 0x0B, 0x06, 0x04, 0x03, 0x2B, 0x43, 0x42, 0x3B, 0x16, 0x14, 0x17, 0x1B}, 14, 0 },
    { 0xF0, {0x3C}, 1, 0 },
    { 0xF0, {0x69}, 1, 0 },
    
    { 0x29, {0}, 0, 20 }   // Display ON + 20ms delay
};

static esp_err_t st7796_init()
{
    esp_err_t err;
    size_t cmd_num = sizeof(st7796_init_sequence) / sizeof(lcd_init_cmd_t);

    for (size_t i = 0; i < cmd_num; i++) {
        err = write_command(st7796_init_sequence[i].cmd);
        if (err != ESP_OK) return err;

        for (size_t d = 0; d < st7796_init_sequence[i].data_len; d++) {
            err = write_data8(st7796_init_sequence[i].data[d]);
            if (err != ESP_OK) return err;
        }

        if (st7796_init_sequence[i].delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(st7796_init_sequence[i].delay_ms));
        }
    }
    return ESP_OK;
}




// ============================================================================
// Window / Geometry management
// ============================================================================
esp_err_t LcdDriver_set_window(int x0, int y0, int x1, int y1)
{
    esp_err_t err;
    err = write_command(0x2A); if (err != ESP_OK) return err;
    err = write_data8(x0 >> 8); err = write_data8(x0 & 0xFF);
    err = write_data8(x1 >> 8); err = write_data8(x1 & 0xFF);

    err = write_command(0x2B); if (err != ESP_OK) return err;
    err = write_data8(y0 >> 8); err = write_data8(y0 & 0xFF);
    err = write_data8(y1 >> 8); err = write_data8(y1 & 0xFF);

    return write_command(0x2C);
}

// ============================================================================
// Driver Initialization (Écran + Tactile)
// ============================================================================
esp_err_t LcdDriver_init(void)
{
    if (s_initialized) return ESP_OK;

    ESP_LOGI(TAG, "MHS4001 %dx%d, SPI=%d MHz", SCREEN_WIDTH, SCREEN_HEIGHT, SPI_FREQUENCY / 1000000);

    // Initialisation GPIO sans TFT_CS
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << TFT_DC) | (1ULL << TFT_RST) | (1ULL << TOUCH_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    
    ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)TFT_DC, 1));
    ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)TOUCH_CS, 1));

    ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)TFT_RST, 0)); 
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)TFT_RST, 1)); 
    vTaskDelay(pdMS_TO_TICKS(120));

    //Initialisation du Bus SPI Commun
    spi_bus_config_t bus_config = {};
    bus_config.sclk_io_num = (gpio_num_t)TFT_SCLK;
    bus_config.mosi_io_num = (gpio_num_t)TFT_MOSI;
    bus_config.miso_io_num = (gpio_num_t)TFT_MISO;
    bus_config.quadwp_io_num = -1;
    bus_config.quadhd_io_num = -1;
    bus_config.max_transfer_sz = 480 * 320 * sizeof(uint16_t); 

    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));

    //Périphérique Écran
    spi_device_interface_config_t dev_config = {};
    dev_config.clock_speed_hz = SPI_FREQUENCY; 
    dev_config.mode = 0;
    dev_config.spics_io_num = (gpio_num_t)TFT_CS; 
    dev_config.queue_size = 10;                  
    dev_config.command_bits = 0;
    dev_config.address_bits = 0;
    dev_config.flags = SPI_DEVICE_NO_DUMMY;      

    ESP_ERROR_CHECK(spi_bus_add_device(LCD_HOST, &dev_config, &s_spi));

    // Périphérique Tactile au même bus (2 MHz max conseillé)
    spi_device_interface_config_t touch_config = {};
    touch_config.clock_speed_hz = 2 * 1000000; 
    touch_config.mode = 0;
    touch_config.spics_io_num = -1; // Géré manuellement avec TOUCH_CS pour éviter les conflits
    touch_config.queue_size = 1;

    ESP_ERROR_CHECK(spi_bus_add_device(LCD_HOST, &touch_config, &s_touch_spi));

    s_dma_buffer = (uint16_t *)heap_caps_malloc(LCD_DMA_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (s_dma_buffer == nullptr) {
        spi_bus_remove_device(s_spi);
        spi_bus_free(LCD_HOST);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Starting ST7796 initialization...");
    ESP_ERROR_CHECK(st7796_init());

    s_initialized = true;
    ESP_LOGI(TAG, "MHS4001 & XPT2046 initialization complete");
    
    return ESP_OK;
}

// ============================================================================
// Callback de lecture tactile pour LVGL
// ============================================================================
void LcdDriver_touch_read(void * indev, void * data)
{
    if (!s_initialized) return;

    lv_indev_data_t * lv_data = (lv_indev_data_t *)data;

    // Variables statiques légères
    static int32_t last_x = 0;
    static int32_t last_y = 0;
    static uint8_t valid_samples = 0;

    // 1. Lecture brute de la puce XPT2046
    uint16_t x_raw = xpt2046_read_cmd(0xD0);
    uint16_t y_raw = xpt2046_read_cmd(0x90);

    // 2. Détection immédiate du relâchement
    if (x_raw < 150 || x_raw > 3950 || y_raw < 150 || y_raw > 3950) {
        valid_samples = 0;
        lv_data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    // Anchors de calibration
    const int32_t RAW_TL_X = 3920; 
    const int32_t RAW_BR_X = 280;  
    const int32_t RAW_TL_Y = 270;  
    const int32_t RAW_BR_Y = 3750; 

    // MAPPING DIRECT
    int32_t lcd_x = ((int32_t)(y_raw - RAW_TL_Y) * SCREEN_WIDTH) / (RAW_BR_Y - RAW_TL_Y);
    int32_t lcd_y = ((int32_t)(x_raw - RAW_TL_X) * SCREEN_HEIGHT) / (RAW_BR_X - RAW_TL_X);

    // Limites de sécurité
    if (lcd_x < 0) lcd_x = 0;
    if (lcd_x >= SCREEN_WIDTH) lcd_x = SCREEN_WIDTH - 1;
    if (lcd_y < 0) lcd_y = 0;
    if (lcd_y >= SCREEN_HEIGHT) lcd_y = SCREEN_HEIGHT - 1;

    // 3. Anti-rebond de transition (Debounce)
    // On exige simplement 2 lectures consécutives proches l'une de l'autre 
    // pour éliminer le bruit impulsionnel de démarrage.
    if (valid_samples == 0) {
        last_x = lcd_x;
        last_y = lcd_y;
        valid_samples = 1;
        
        // On conserve l'état précédent le temps de confirmer au prochain cycle (dans 5ms)
        lv_data->state = (lv_data->state == LV_INDEV_STATE_PRESSED) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        return;
    } 
    
    // Vérification de la cohérence du déplacement (évite les sauts de pixels géants)
    if (abs(lcd_x - last_x) > 40 || abs(lcd_y - last_y) > 40) {
        // Le point a sauté trop loin d'un coup (bruit) : on ignore cet échantillon
        last_x = lcd_x;
        last_y = lcd_y;
        return;
    }

    // Le signal est valide et stable ! On met à jour les coordonnées
    last_x = lcd_x;
    last_y = lcd_y;

    // Transmission instantanée à LVGL
    lv_data->point.x = (int16_t)lcd_x;
    lv_data->point.y = (int16_t)lcd_y;
    lv_data->state = LV_INDEV_STATE_PRESSED;
}



void LcdDriver_touch_calibrate(void)
{
    if (!s_initialized) return;

    // On suspend les timers de LVGL pour libérer totalement le bus SPI3 de l'écran
    lv_timer_enable(false);

    uint16_t x_raw = 0, y_raw = 0;
    uint16_t x_sample[2] = {0};
    uint16_t y_sample[2] = {0};

    ESP_LOGI(TAG, "=== DÉBUT DE LA CALIBRATION TACTILE ===");
    
    // -------------------------------------------------------------
    // ÉTAPE 1 : Coin Supérieur Gauche (Théorique 0,0)
    // -------------------------------------------------------------
    printf("\n👉 ÉTAPE 1: Appuyez et maintenez le COIN SUPÉRIEUR GAUCHE...\n");
    // Attendre que l'utilisateur appuie
    while (1) {
        x_raw = xpt2046_read_cmd(0xD0);
        y_raw = xpt2046_read_cmd(0x90);
        if (x_raw > 150 && x_raw < 3950 && y_raw > 150 && y_raw < 3950) {
            x_sample[0] = x_raw;
            y_sample[0] = y_raw;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    // Attendre le relâchement
    while (xpt2046_read_cmd(0xD0) > 150) { vTaskDelay(pdMS_TO_TICKS(50)); }
    printf("✅ OK! Données mémorisées pour le coin 1.\n");

    vTaskDelay(pdMS_TO_TICKS(1000)); // Petite pause de confort

    // -------------------------------------------------------------
    // ÉTAPE 2 : Coin Inférieur Droit (Théorique SCREEN_WIDTH, SCREEN_HEIGHT)
    // -------------------------------------------------------------
    printf("\n👉 ÉTAPE 2: Appuyez et maintenez le COIN INFÉRIEUR DROIT...\n");
    // Attendre que l'utilisateur appuie
    while (1) {
        x_raw = xpt2046_read_cmd(0xD0);
        y_raw = xpt2046_read_cmd(0x90);
        if (x_raw > 150 && x_raw < 3950 && y_raw > 150 && y_raw < 3950) {
            x_sample[1] = x_raw;
            y_sample[1] = y_raw;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    // Attendre le relâchement
    while (xpt2046_read_cmd(0xD0) > 150) { vTaskDelay(pdMS_TO_TICKS(50)); }
    printf("✅ OK! Données mémorisées pour le coin 2.\n");

    // -------------------------------------------------------------
    // AFFICHAGE DES RÉSULTATS
    // -------------------------------------------------------------
    printf("\n==================================================\n");
    printf("   RÉSULTATS DE LA CALIBRATION POUR LE CODE       \n");
    printf("==================================================\n");
    printf("Remplacez les variables min/max dans LcdDriver.cpp :\n\n");
    printf("int32_t x_min = %d;\n", x_sample[0]);
    printf("int32_t x_max = %d;\n", x_sample[1]);
    printf("int32_t y_min = %d;\n", y_sample[0]);
    printf("int32_t y_max = %d;\n", y_sample[1]);
    printf("==================================================\n\n");

    // On réactive le rafraîchissement d'affichage normal
    lv_timer_enable(true);
}


uint16_t LcdDriver_get_random_bright_color(void)
{
    // Génère des canaux avec une valeur minimale pour garantir la luminosité
    uint8_t r = (console_random() % 24) + 8;  // Rouge : de 8 à 31
    uint8_t g = (console_random() % 48) + 16; // Vert  : de 16 à 63
    uint8_t b = (console_random() % 24) + 8;  // Bleu  : de 8 à 31

    // Assemble les canaux au format RGB565 (RRRRRGGGGGGBBBBB)
    return (uint16_t)((r << 11) | (g << 5) | b);
}

void my_flush_cb(void * disp, const void * area, uint8_t * px_map)
{
    lv_display_t * lv_disp = (lv_display_t *)disp;
    const lv_area_t * lv_area = (const lv_area_t *)area;

    uint16_t * pixels = (uint16_t *)px_map;
    int32_t w = (lv_area->x2 - lv_area->x1 + 1);
    int32_t h = (lv_area->y2 - lv_area->y1 + 1);
    uint32_t total_pixels = w * h;

    static spi_transaction_t trans_active;
    static bool spi_busy = false;

    // 1. SYNCHRONISATION ASYNCHRONE
    if (spi_busy) {
        spi_transaction_t *r_trans;
        spi_device_get_trans_result(s_spi, &r_trans, portMAX_DELAY);
        spi_busy = false;

        // LA STRATÉGIE PARFAITE : On cède le contrôle au planificateur FreeRTOS 
        // pendant une fraction de microseconde. Le Watchdog global se rafraîchit, 
        // l'erreur "task not found" disparaît, et le bus SPI reste saturé à 19 FPS.
        taskYIELD(); 
    }

    // 2. Configurer la zone géométrique sur le ST7796
    LcdDriver_set_window(lv_area->x1, lv_area->y1, lv_area->x2, lv_area->y2);
    dc_data();

    // 3. Préparer la nouvelle transaction DMA
    memset(&trans_active, 0, sizeof(spi_transaction_t));
    trans_active.length = total_pixels * sizeof(uint16_t) * 8; 
    trans_active.tx_buffer = pixels;                           

    // 4. Lancement du transfert SPI en arrière-plan (DMA asynchrone)
    esp_err_t err = spi_device_queue_trans(s_spi, &trans_active, portMAX_DELAY);
    if (err == ESP_OK) {
        spi_busy = true;
    } else {
        ESP_LOGE("SPI", "DMA Queue failed: %s", esp_err_to_name(err));
    }

    // 5. Libérer immédiatement le moteur LVGL pour préparer le buffer suivant
    lv_display_flush_ready(lv_disp);
}