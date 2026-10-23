// Jauges.cpp
#include "Jauges.h"
#include "Config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h" 
#include "esp_log.h"
#include <math.h> // REQUIS pour la fonction sqrtf() (racine carrée)

// Inclusion des fichiers C de l'API Texas Instruments
extern "C" {
    #include "ads1261.h"
    #include "hal.h"
    
    // Prototype de notre fonction d'aiguillage présente dans hal.c
    void _ads126x_set_active_device(uint8_t index);
    
    // AJOUT DE LA DÉCLARATION ICI pour que le C++ trouve InitSPI()
    void InitSPI(void); 
}

static const char *TAG = "JAUGE_APP";

#include "freertos/semphr.h"

// Déclaration d'un sémaphore global pour synchroniser la tâche
SemaphoreHandle_t drdy_semaphore = NULL;

// Fonction de l'interruption (s'exécute instantanément sur le flanc descendant)
static void IRAM_ATTR drdy_isr_handler(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    // On donne le sémaphore pour libérer immédiatement la tâche de lecture
    xSemaphoreGiveFromISR(drdy_semaphore, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

esp_err_t Jauges_init(void)
{
    ESP_LOGI(TAG, "Initialisation des jauges...");
    InitSPI(); 

    // Création du sémaphore de synchronisation
    drdy_semaphore = xSemaphoreCreateBinary();

    // Configuration du GPIO 34 (DataReady) avec interruption sur flanc descendant (NEGEDGE)
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_NEGEDGE; // Déclenchement sur flanc descendant uniquement
    io_conf.pin_bit_mask = (1ULL << DataReady);
    io_conf.mode = GPIO_MODE_INPUT;
    gpio_config(&io_conf);

    // Installer le service d'interruption global de l'ESP32 et lier notre handler
    gpio_install_isr_service(0);
    gpio_isr_handler_add((gpio_num_t)DataReady, drdy_isr_handler, NULL);

    for (int jauge_index = 0; jauge_index < 5; jauge_index++)
    {
        adcStartupRoutine(jauge_index);
    }
    
    return ESP_OK;
}


// ============================================================================
// Lecture d'une jauge par index
// ============================================================================

int32_t Jauges_read(int index)
{
    if (index < 0 || index >= 5) return 0;

    _ads126x_set_active_device(index);

    if (index == 0) {
        // La tâche s'arrête ici proprement. Elle ne consomme 0% de CPU.
        // Dès que la ligne DRDYn tombe à 0, l'interruption réveille cette ligne en moins de 2 microsecondes.
        if (xSemaphoreTake(drdy_semaphore, pdMS_TO_TICKS(1000)) == pdFALSE) {
            ESP_LOGW(TAG, "Timeout matériel : Aucun flanc DRDY détecté depuis 1 seconde");
        }
    }

    // On lance la lecture SPI immédiatement après le réveil
    uint8_t status = 0, dataVal[4] = {0}, crcVal = 0;
    int32_t raw_value = readData(&status, dataVal, &crcVal);
    
    return raw_value;
}

// La lecture de Jauges 0 est blocante
// Immédiatement après, on lit les autres jauges qui ont converti en parallèle
// Les échantillons sont déphasés de 22.25us chacun
esp_err_t Jauges_newValue(Jauges_t &Jauge, int32_t newValue)
{
    Jauge.adc_reading = newValue;
    // Extraction dynamique du gain réel configuré pour cette jauge précise
    Jauge.Gain_real = pga_valeurs_reelles[(uint8_t)Jauge.Gain];

    // --- CALCULS EN TEMPS RÉEL ---
    // 1. Conversion immédiate de la valeur brute en micro-déformations (µε)
    Jauge.uStrain = ((float)Jauge.adc_reading / (8388607.0f * Jauge.Gain_real)) * (2000000.0f / Jauge.G_F);

    // 2. Accumulation pour les calculs de statistiques à venir
    Jauge.Somme += Jauge.uStrain;
    Jauge.Somme_sq += (double)Jauge.uStrain * Jauge.uStrain;

    Jauge.Contrainte = (int)(Jauge.uStrain * Jauge.ModuleYoung / 1000);

    return ESP_OK;
}

// --- CALCULS STATISTIQUES FINAUX---
esp_err_t Jauges_calcStats(Jauges_t &Jauge)
{
    // 1. Calcul et écriture sécurisée de la moyenne
    Jauge.uStrain_avg = (float)(Jauge.Somme / 1000.0);

    // 2. Calcul et écriture sécurisée de la déviation standard
    float variance = (float)((Jauge.Somme_sq / 1000.0) - ((double)Jauge.uStrain_avg * Jauge.uStrain_avg));
    if (variance < 0) variance = 0; // Sécurité anti-arrondi mathématique
    Jauge.uStrain_StdDev = sqrtf(variance);

    return ESP_OK;
}

