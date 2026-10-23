#pragma GCC diagnostic ignored "-Wdeprecated-enum-enum-conversion"
#pragma GCC diagnostic ignored "-Wenum-conversion"

#include "GuiEngine.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"
#include "esp_log.h"

#include "Config.h"
#include "LcdDriver.h"
#include "lvgl.h"

extern "C" {
    #include "ui.h"
}

// *************************************
// INCLUSION ET LIEN AVEC LA STRUCTURE DE VOS JAUGES
#include "Jauges.h"
extern Jauges_t Jauges[5];
// *************************************


static const char *TAG = "GuiEngine";

static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;
static esp_task_wdt_user_handle_t wdt_user_handle = NULL;

// 3. Callback pour fournir le temps système à LVGL (Placé avant GuiEngine_init)
static uint32_t my_tick(void) {
    return esp_timer_get_time() / 1000;
}

// 4. Callback du Watchdog (Placé avant GuiEngine_init)
static void watchdog_feeder_timer_cb(TimerHandle_t xTimer) {
    if (wdt_user_handle != NULL) {
        esp_task_wdt_reset_user(wdt_user_handle);
    }
}


//Fonction de mise à jour appelée automatiquement par LVGL (ex: toutes les 100ms)
void update_gui_labels(lv_timer_t * timer) {
    // 1. Définition de la fonction lambda de formatage
    auto update_label = [](lv_obj_t* label, const char* format, auto val) {
        if (label != NULL) {
            lv_label_set_text_fmt(label, format, val);
        }
    };

    // 2. Tableaux de pointeurs vers les labels générés par SquareLine Studio
    // Remplacer par les vrais noms de variables de vos jauges 1 à 5 s'ils diffèrent
    static lv_obj_t* const lbl_val[]  = { ui_lblVal1, ui_lblVal2, ui_lblVal3, ui_lblVal4, ui_lblVal5 };
    static lv_obj_t* const lbl_avg[]  = { ui_lblAvg1, ui_lblAvg2, ui_lblAvg3, ui_lblAvg4, ui_lblAvg5 };
    static lv_obj_t* const lbl_dev[]  = { ui_lblDev1, ui_lblDev2, ui_lblDev3, ui_lblDev4, ui_lblDev5 };
    static lv_obj_t* const lbl_mpa[]  = { ui_lblMPa1, ui_lblMPa2, ui_lblMPa3, ui_lblMPa4, ui_lblMPa5 };

    // 3. Boucle de mise à jour automatique pour les 5 jauges
    for (uint8_t i = 0; i < 5; i++) {
        update_label(lbl_val[i], "%.0fµε",   Jauges[i].uStrain);
        update_label(lbl_avg[i], "%.1fµε",   Jauges[i].uStrain_avg);
        update_label(lbl_dev[i], "%.2fµε",   Jauges[i].uStrain_StdDev);
        update_label(lbl_mpa[i], "%dMPa",    Jauges[i].Contrainte);
    }
}


// Supprime la flèche du dropdown
// Définit les valeurs par défault
void init_dropdown()
{
    // 1. Tableaux de pointeurs vers les dropdowns de vos 5 jauges
    static lv_obj_t* const drp_gain[] = { ui_DrpGain1, ui_DrpGain2, ui_DrpGain3, ui_DrpGain4, ui_DrpGain5 };
    static lv_obj_t* const drp_gf[]   = { ui_DrpGf1,   ui_DrpGf2,   ui_DrpGf3,   ui_DrpGf4,   ui_DrpGf5 };
    static lv_obj_t* const drp_mat[]  = { ui_DrpMat1,  ui_DrpMat2,  ui_DrpMat3,  ui_DrpMat4,  ui_DrpMat5 };

    // 2. Boucle d'initialisation pour les 5 jauges
    for (uint8_t i = 0; i < 5; i++) {
        // Sécurité : on vérifie que le widget existe bien avant d'appliquer les paramètres
        if (drp_gain[i] != NULL) {
            lv_dropdown_set_symbol(drp_gain[i], NULL);
            lv_dropdown_set_selected(drp_gain[i], 4); // Par défaut : Gain 16 (index 4)
        }

        if (drp_gf[i] != NULL) {
            lv_dropdown_set_symbol(drp_gf[i], NULL);
            lv_dropdown_set_selected(drp_gf[i], 1);   // Par défaut : GF 2.0 (index 1)
        }

        if (drp_mat[i] != NULL) {
            lv_dropdown_set_symbol(drp_mat[i], NULL);
            lv_dropdown_set_selected(drp_mat[i], 0);  // Par défaut : Premier matériau (index 0)
        }
    }
}


// 5. Fonction principale d'initialisation
void GuiEngine_init(void)
{
    ESP_LOGI(TAG, "Initialisation du moteur graphique...");

    // 1. Initialisation matérielle de l'écran et du tactile via votre driver
    ESP_ERROR_CHECK(LcdDriver_init());

    // 2. Calcul et allocation des tampons de rendu (Double Buffer SRAM Interne Rapide)
    uint32_t buf_pixel_count = SCREEN_WIDTH * (SCREEN_HEIGHT / 10); 
    uint32_t buf_byte_size = buf_pixel_count * sizeof(lv_color_t);

    buf1 = (lv_color_t *)heap_caps_aligned_alloc(32, buf_byte_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    buf2 = (lv_color_t *)heap_caps_aligned_alloc(32, buf_byte_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

    if (buf1 == nullptr || buf2 == nullptr) {
        ESP_LOGE(TAG, "Erreur Critique : Allocation mémoire interne SRAM échouée !");
        while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    // 3. Configuration de base de la bibliothèque LVGL
    lv_init();
    lv_tick_set_cb(my_tick); // Maintenant, le compilateur connaît my_tick !
    
    // 4. Enregistrement de l'affichage v9.5
    lv_display_t * disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT); 
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_flush_cb(disp, (lv_display_flush_cb_t)my_flush_cb);
    lv_display_set_buffers(disp, buf1, buf2, buf_byte_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    // 5. Enregistrement du périphérique d'entrée tactile
    lv_indev_t * indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, (lv_indev_read_cb_t)LcdDriver_touch_read);

    // 6. Gestion de la sécurité matérielle (Watchdog ESP-IDF v6)
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 30000, 
        .idle_core_mask = (1 << 0), // Surveille le Core 0
        .trigger_panic = true
    };
    esp_task_wdt_reconfigure(&wdt_config);
    esp_task_wdt_add_user("gui_wdt_user", &wdt_user_handle);

    // Lancement du timer logiciel FreeRTOS pour nourrir le WDT automatiquement
    TimerHandle_t wdt_timer = xTimerCreate("wdt_feeder", pdMS_TO_TICKS(1000), pdTRUE, (void*)0, watchdog_feeder_timer_cb);
    if (wdt_timer != NULL) {
        xTimerStart(wdt_timer, 0);
    }

    // 7. Création de la tâche asynchrone dédiée au rendu graphique (Épinglée au Core 1)
    xTaskCreatePinnedToCore(
        [](void* pvParameters) {
            
            ui_init(); 

            init_dropdown();

            // Crée un timer LVGL qui rafraîchit les textes toutes les 100 ms
            lv_timer_create(update_gui_labels, 100, NULL);
            
            while (1) {
                lv_timer_handler();
                vTaskDelay(pdMS_TO_TICKS(5)); 
            }
        },
        "LVGL_Render",
        8192, // Pile de 8 Ko, parfaite pour absorber les matrices de calculs sans crash
        NULL,               
        5,                  
        NULL,               
        1                   
    );

    ESP_LOGI(TAG, "Moteur graphique LVGL démarré (En attente d'une interface ou d'un benchmark)");
}
