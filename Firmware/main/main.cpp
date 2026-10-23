/*
 * ESP-IDF 6.1
 *
 * ESP32 Bluetooth A2DP Sink
 * Sortie audio I2S vers un PCM5122
 *
 * Connexions:
 *
 * ESP32 GPIO27 -> PCM5122 BCK
 * ESP32 GPIO26 -> PCM5122 LRCK / LCK / WS
 * ESP32 GPIO25 -> PCM5122 DIN
 * ESP32 GND    -> PCM5122 GND
 *
 * Le PCM5122 est utilisé comme DAC I2S.
 * Le décodage SBC est effectué par l'ESP32.
 */

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"

#include "esp_err.h"
#include "esp_log.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"

extern "C" {
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"

#include "bt_app_core_utils.h"
#include "bredr_app_common_utils.h"
}

/* --------------------------------------------------------------------------
 * Configuration générale
 * -------------------------------------------------------------------------- */

#ifndef CONFIG_EXAMPLE_LOCAL_DEVICE_NAME
#define CONFIG_EXAMPLE_LOCAL_DEVICE_NAME "ESP32_PCM5122"
#endif

static const char local_device_name[] =
    CONFIG_EXAMPLE_LOCAL_DEVICE_NAME;

static const char *TAG = "PCM5122_A2DP";

/* --------------------------------------------------------------------------
 * Configuration des broches I2S
 * -------------------------------------------------------------------------- */

static constexpr gpio_num_t PCM5122_BCLK_GPIO =
    GPIO_NUM_27;

static constexpr gpio_num_t PCM5122_LRCK_GPIO =
    GPIO_NUM_26;

static constexpr gpio_num_t PCM5122_DATA_GPIO =
    GPIO_NUM_25;

/* --------------------------------------------------------------------------
 * Configuration audio
 * -------------------------------------------------------------------------- */

static constexpr uint32_t DEFAULT_SAMPLE_RATE =
    44100;

static constexpr size_t AUDIO_STREAM_BUFFER_SIZE =
    32 * 1024;

static constexpr size_t AUDIO_STREAM_TRIGGER_SIZE =
    1024;

static constexpr size_t AUDIO_TASK_BUFFER_SIZE =
    2048;

static constexpr uint32_t AUDIO_TASK_STACK_SIZE =
    4096;

static constexpr UBaseType_t AUDIO_TASK_PRIORITY =
    5;

/* --------------------------------------------------------------------------
 * Événements internes
 * -------------------------------------------------------------------------- */

enum {
    BT_APP_EVT_STACK_UP = 0,
};

/* --------------------------------------------------------------------------
 * Variables globales
 * -------------------------------------------------------------------------- */

static i2s_chan_handle_t tx_chan = nullptr;

static StreamBufferHandle_t audio_stream_buffer =
    nullptr;

static uint32_t current_sample_rate =
    DEFAULT_SAMPLE_RATE;

static bool i2s_initialized = false;

/* --------------------------------------------------------------------------
 * Déclarations des fonctions privées
 * -------------------------------------------------------------------------- */

static void pcm5122_init(void);

static void pcm5122_set_sample_rate(
    uint32_t sample_rate);

static void pcm5122_audio_task(
    void *parameter);

static uint32_t get_a2dp_sample_rate(
    const esp_a2d_cb_param_t *param);

static void bt_app_dev_cb(
    esp_bt_dev_cb_event_t event,
    esp_bt_dev_cb_param_t *param);

static void bt_app_gap_cb(
    esp_bt_gap_cb_event_t event,
    esp_bt_gap_cb_param_t *param);

static void bt_app_a2d_cb(
    esp_a2d_cb_event_t event,
    esp_a2d_cb_param_t *param);

static void bt_app_a2d_data_cb(
    const uint8_t *data,
    uint32_t len);

static void bt_av_hdl_stack_evt(
    uint16_t event,
    void *p_param);

/* --------------------------------------------------------------------------
 * Initialisation I2S du PCM5122
 * -------------------------------------------------------------------------- */

static void pcm5122_init(void)
{
    if (i2s_initialized) {
        return;
    }

    /*
     * Création du buffer entre le callback Bluetooth et la tâche I2S.
     *
     * Le callback Bluetooth ne doit pas rester bloqué pendant que le
     * périphérique I2S transmet les données.
     */
    audio_stream_buffer = xStreamBufferCreate(
        AUDIO_STREAM_BUFFER_SIZE,
        AUDIO_STREAM_TRIGGER_SIZE);

    if (audio_stream_buffer == nullptr) {
        ESP_LOGE(
            TAG,
            "Impossible de créer le buffer audio");

        abort();
    }

    /*
     * Création du canal I2S TX.
     *
     * L'ESP32 est maître I2S.
     */
    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(
            I2S_NUM_0,
            I2S_ROLE_MASTER);

    channel_config.dma_desc_num = 8;
    channel_config.dma_frame_num = 256;

    /*
     * Efface automatiquement le buffer DMA lorsqu'une sous-alimentation
     * audio se produit.
     */
    channel_config.auto_clear_after_cb = true;
    channel_config.auto_clear_before_cb = false;

    ESP_ERROR_CHECK(
        i2s_new_channel(
            &channel_config,
            &tx_chan,
            nullptr));

    /*
     * Configuration Philips I2S standard.
     *
     * Le flux PCM A2DP décodé est:
     *
     * - 16 bits signés
     * - stéréo
     * - canaux gauche/droite intercalés
     */
    i2s_std_config_t std_config = {
        .clk_cfg =
            I2S_STD_CLK_DEFAULT_CONFIG(
                DEFAULT_SAMPLE_RATE),

        .slot_cfg =
            I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                I2S_DATA_BIT_WIDTH_16BIT,
                I2S_SLOT_MODE_STEREO),

        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PCM5122_BCLK_GPIO,
            .ws   = PCM5122_LRCK_GPIO,
            .dout = PCM5122_DATA_GPIO,
            .din  = I2S_GPIO_UNUSED,

            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ESP_ERROR_CHECK(
        i2s_channel_init_std_mode(
            tx_chan,
            &std_config));

    ESP_ERROR_CHECK(
        i2s_channel_enable(tx_chan));

    /*
     * Création de la tâche chargée des écritures I2S.
     */
    const BaseType_t task_result =
        xTaskCreate(
            pcm5122_audio_task,
            "pcm5122_audio",
            AUDIO_TASK_STACK_SIZE,
            nullptr,
            AUDIO_TASK_PRIORITY,
            nullptr);

    if (task_result != pdPASS) {
        ESP_LOGE(
            TAG,
            "Impossible de créer la tâche audio");

        abort();
    }

    current_sample_rate = DEFAULT_SAMPLE_RATE;
    i2s_initialized = true;

    ESP_LOGI(
        TAG,
        "PCM5122 initialisé à %" PRIu32
        " Hz, 16 bits, stéréo",
        current_sample_rate);

    ESP_LOGI(
        TAG,
        "I2S: BCLK=%d, LRCLK=%d, DATA=%d",
        static_cast<int>(PCM5122_BCLK_GPIO),
        static_cast<int>(PCM5122_LRCK_GPIO),
        static_cast<int>(PCM5122_DATA_GPIO));
}

/* --------------------------------------------------------------------------
 * Modification de la fréquence d'échantillonnage
 * -------------------------------------------------------------------------- */

static void pcm5122_set_sample_rate(
    uint32_t sample_rate)
{
    if (!i2s_initialized) {
        return;
    }

    if (tx_chan == nullptr) {
        return;
    }

    if (sample_rate == 0) {
        return;
    }

    if (sample_rate == current_sample_rate) {
        return;
    }

    ESP_LOGI(
        TAG,
        "Changement de fréquence: %" PRIu32
        " -> %" PRIu32 " Hz",
        current_sample_rate,
        sample_rate);

    /*
     * Supprimer les échantillons associés à l'ancienne fréquence.
     */
    if (audio_stream_buffer != nullptr) {
        (void)xStreamBufferReset(
            audio_stream_buffer);
    }

    /*
     * Le canal doit être désactivé avant la reconfiguration.
     */
    esp_err_t err =
        i2s_channel_disable(tx_chan);

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Impossible de désactiver I2S: %s",
            esp_err_to_name(err));

        return;
    }

    i2s_std_clk_config_t clock_config =
        I2S_STD_CLK_DEFAULT_CONFIG(
            sample_rate);

    err = i2s_channel_reconfig_std_clock(
        tx_chan,
        &clock_config);

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Impossible de reconfigurer I2S: %s",
            esp_err_to_name(err));

        /*
         * Tentative de réactivation avec l'ancienne configuration.
         */
        const esp_err_t enable_result =
            i2s_channel_enable(tx_chan);

        if (enable_result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Impossible de réactiver I2S: %s",
                esp_err_to_name(enable_result));
        }

        return;
    }

    err = i2s_channel_enable(tx_chan);

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Impossible de réactiver I2S: %s",
            esp_err_to_name(err));

        return;
    }

    current_sample_rate = sample_rate;

    ESP_LOGI(
        TAG,
        "I2S configuré à %" PRIu32 " Hz",
        current_sample_rate);
}

/* --------------------------------------------------------------------------
 * Tâche d'écriture vers le PCM5122
 * -------------------------------------------------------------------------- */

static void pcm5122_audio_task(
    void *parameter)
{
    (void)parameter;

    uint8_t audio_buffer[AUDIO_TASK_BUFFER_SIZE];

    while (true) {
        /*
         * Attendre que le callback Bluetooth place des données PCM
         * dans le StreamBuffer.
         */
        const size_t received =
            xStreamBufferReceive(
                audio_stream_buffer,
                audio_buffer,
                sizeof(audio_buffer),
                portMAX_DELAY);

        if (received == 0) {
            continue;
        }

        size_t total_written = 0;

        /*
         * i2s_channel_write() peut écrire moins de données que demandé.
         * Continuer jusqu'à ce que le bloc entier soit transmis.
         */
        while (total_written < received) {
            size_t bytes_written = 0;

            const esp_err_t err =
                i2s_channel_write(
                    tx_chan,
                    audio_buffer + total_written,
                    received - total_written,
                    &bytes_written,
                    portMAX_DELAY);

            if (err != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "Erreur d'écriture I2S: %s",
                    esp_err_to_name(err));

                break;
            }

            if (bytes_written == 0) {
                ESP_LOGW(
                    TAG,
                    "Aucune donnée écrite vers I2S");

                break;
            }

            total_written += bytes_written;
        }
    }
}

/* --------------------------------------------------------------------------
 * Détection de la fréquence SBC
 * -------------------------------------------------------------------------- */

static uint32_t get_a2dp_sample_rate(
    const esp_a2d_cb_param_t *param)
{
    if (param == nullptr) {
        return DEFAULT_SAMPLE_RATE;
    }

    if (param->audio_cfg.mcc.type !=
        ESP_A2D_MCT_SBC) {
        ESP_LOGW(
            TAG,
            "Codec A2DP non SBC: %d",
            static_cast<int>(
                param->audio_cfg.mcc.type));

        return DEFAULT_SAMPLE_RATE;
    }

    const uint8_t sample_frequency =
        param->audio_cfg
            .mcc
            .cie
            .sbc_info
            .samp_freq;

    if ((sample_frequency &
         ESP_A2D_SBC_CIE_SF_48K) != 0) {
        return 48000;
    }

    if ((sample_frequency &
         ESP_A2D_SBC_CIE_SF_44K) != 0) {
        return 44100;
    }

    if ((sample_frequency &
         ESP_A2D_SBC_CIE_SF_32K) != 0) {
        return 32000;
    }

    if ((sample_frequency &
         ESP_A2D_SBC_CIE_SF_16K) != 0) {
        return 16000;
    }

    ESP_LOGW(
        TAG,
        "Fréquence SBC inconnue: 0x%02X",
        static_cast<unsigned int>(
            sample_frequency));

    return DEFAULT_SAMPLE_RATE;
}

/* --------------------------------------------------------------------------
 * Callback Bluetooth Device
 * -------------------------------------------------------------------------- */

static void bt_app_dev_cb(
    esp_bt_dev_cb_event_t event,
    esp_bt_dev_cb_param_t *param)
{
    /*
     * Utilise le gestionnaire commun fourni dans l'exemple ESP-IDF 6.1.
     */
    bredr_app_dev_evt_def_hdl(
        event,
        param);
}

/* --------------------------------------------------------------------------
 * Callback Bluetooth GAP
 * -------------------------------------------------------------------------- */

static void bt_app_gap_cb(
    esp_bt_gap_cb_event_t event,
    esp_bt_gap_cb_param_t *param)
{
    /*
     * Gère notamment l'authentification et les événements GAP.
     */
    bredr_app_gap_evt_def_hdl(
        event,
        param);
}

/* --------------------------------------------------------------------------
 * Callback des événements A2DP
 * -------------------------------------------------------------------------- */

static void bt_app_a2d_cb(
    esp_a2d_cb_event_t event,
    esp_a2d_cb_param_t *param)
{
    if (param == nullptr) {
        ESP_LOGE(
            TAG,
            "Paramètre A2DP nul, événement=%d",
            static_cast<int>(event));

        return;
    }

    switch (event) {
    case ESP_A2D_PROF_STATE_EVT:
        ESP_LOGI(
            TAG,
            "Événement d'état du profil A2DP");
        break;

    case ESP_A2D_CONNECTION_STATE_EVT:
        switch (param->conn_stat.state) {
        case ESP_A2D_CONNECTION_STATE_CONNECTED:
            ESP_LOGI(
                TAG,
                "Source Bluetooth A2DP connectée");

            /*
             * Ne plus apparaître dans la recherche Bluetooth pendant
             * qu'une source est connectée.
             */
            ESP_ERROR_CHECK(
                esp_bt_gap_set_scan_mode(
                    ESP_BT_NON_CONNECTABLE,
                    ESP_BT_NON_DISCOVERABLE));
            break;

        case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
            ESP_LOGI(
                TAG,
                "Source Bluetooth A2DP déconnectée");

            if (audio_stream_buffer != nullptr) {
                (void)xStreamBufferReset(
                    audio_stream_buffer);
            }

            /*
             * Redevenir visible et connectable.
             */
            ESP_ERROR_CHECK(
                esp_bt_gap_set_scan_mode(
                    ESP_BT_CONNECTABLE,
                    ESP_BT_GENERAL_DISCOVERABLE));
            break;

        case ESP_A2D_CONNECTION_STATE_CONNECTING:
            ESP_LOGI(
                TAG,
                "Connexion Bluetooth A2DP en cours");
            break;

        case ESP_A2D_CONNECTION_STATE_DISCONNECTING:
            ESP_LOGI(
                TAG,
                "Déconnexion Bluetooth A2DP en cours");
            break;

        default:
            ESP_LOGW(
                TAG,
                "État de connexion A2DP inconnu: %d",
                static_cast<int>(
                    param->conn_stat.state));
            break;
        }
        break;

    case ESP_A2D_AUDIO_STATE_EVT:
        /*
         * La version ESP-IDF installée expose au minimum:
         *
         * ESP_A2D_AUDIO_STATE_STARTED
         * ESP_A2D_AUDIO_STATE_SUSPEND
         *
         * Aucun ESP_A2D_AUDIO_STATE_STOP ou STOPPED n'est utilisé ici.
         */
        if (param->audio_stat.state ==
            ESP_A2D_AUDIO_STATE_STARTED) {
            ESP_LOGI(
                TAG,
                "Lecture audio A2DP démarrée");
        } else if (
            param->audio_stat.state ==
            ESP_A2D_AUDIO_STATE_SUSPEND) {
            ESP_LOGI(
                TAG,
                "Lecture audio A2DP suspendue ou arrêtée");

            if (audio_stream_buffer != nullptr) {
                (void)xStreamBufferReset(
                    audio_stream_buffer);
            }
        } else {
            ESP_LOGI(
                TAG,
                "État audio A2DP reçu: %d",
                static_cast<int>(
                    param->audio_stat.state));
        }
        break;

    case ESP_A2D_AUDIO_CFG_EVT: {
        const uint32_t sample_rate =
            get_a2dp_sample_rate(param);

        ESP_LOGI(
            TAG,
            "Configuration A2DP reçue: %" PRIu32
            " Hz",
            sample_rate);

        pcm5122_set_sample_rate(
            sample_rate);
        break;
    }

    case ESP_A2D_SNK_PSC_CFG_EVT:
        ESP_LOGD(
            TAG,
            "Configuration de protection A2DP");
        break;

    case ESP_A2D_SNK_SET_DELAY_VALUE_EVT:
        ESP_LOGD(
            TAG,
            "Valeur de délai A2DP configurée");
        break;

    case ESP_A2D_SNK_GET_DELAY_VALUE_EVT:
        ESP_LOGD(
            TAG,
            "Valeur de délai A2DP reçue");
        break;

    case ESP_A2D_SEP_REG_STATE_EVT:
        ESP_LOGD(
            TAG,
            "État d'enregistrement SEP");
        break;

    default:
        ESP_LOGW(
            TAG,
            "Événement A2DP non traité: %d",
            static_cast<int>(event));
        break;
    }
}

/* --------------------------------------------------------------------------
 * Callback des données PCM A2DP
 * -------------------------------------------------------------------------- */

static void bt_app_a2d_data_cb(
    const uint8_t *data,
    uint32_t len)
{
    if (data == nullptr) {
        return;
    }

    if (len == 0) {
        return;
    }

    if (audio_stream_buffer == nullptr) {
        return;
    }

    /*
     * Ne pas bloquer le callback Bluetooth.
     *
     * La durée d'attente est zéro tick. Si le StreamBuffer est plein,
     * les données excédentaires sont abandonnées plutôt que de bloquer
     * la pile Bluetooth.
     */
    const size_t bytes_sent =
        xStreamBufferSend(
            audio_stream_buffer,
            data,
            static_cast<size_t>(len),
            0);

    /*
     * Aucun log dans le callback audio.
     *
     * Un log exécuté plusieurs dizaines ou centaines de fois par seconde
     * pourrait perturber la transmission audio.
     */
    (void)bytes_sent;
}

/* --------------------------------------------------------------------------
 * Démarrage de la pile A2DP
 * -------------------------------------------------------------------------- */

static void bt_av_hdl_stack_evt(
    uint16_t event,
    void *p_param)
{
    (void)p_param;

    ESP_LOGD(
        TAG,
        "%s, événement=%u",
        __func__,
        static_cast<unsigned int>(event));

    switch (event) {
    case BT_APP_EVT_STACK_UP:
        ESP_ERROR_CHECK(
            esp_bt_gap_set_device_name(
                local_device_name));

        ESP_ERROR_CHECK(
            esp_bt_dev_register_callback(
                bt_app_dev_cb));

        ESP_ERROR_CHECK(
            esp_bt_gap_register_callback(
                bt_app_gap_cb));

        ESP_ERROR_CHECK(
            esp_a2d_register_callback(
                bt_app_a2d_cb));

        /*
         * Initialiser le profil A2DP Sink.
         */
        ESP_ERROR_CHECK(
            esp_a2d_sink_init());

        /*
         * Enregistrer le callback recevant les échantillons PCM décodés.
         *
         * Le décodage SBC est effectué par Bluedroid sur l'ESP32.
         */
        ESP_ERROR_CHECK(
            esp_a2d_sink_register_data_callback(
                bt_app_a2d_data_cb));

        /*
         * Obtenir la valeur de délai par défaut.
         */
        //ESP_ERROR_CHECK(
           // esp_a2d_sink_get_delay_value());

        /*
         * Demander le nom de périphérique configuré.
         */
        ESP_ERROR_CHECK(
            esp_bt_gap_get_device_name());

        /*
         * Rendre l'ESP32 visible et connectable.
         */
        ESP_ERROR_CHECK(
            esp_bt_gap_set_scan_mode(
                ESP_BT_CONNECTABLE,
                ESP_BT_GENERAL_DISCOVERABLE));

        ESP_LOGI(
            TAG,
            "Bluetooth A2DP prêt");

        ESP_LOGI(
            TAG,
            "Nom Bluetooth: %s",
            local_device_name);
        break;

    default:
        ESP_LOGW(
            TAG,
            "Événement de pile Bluetooth non traité: %u",
            static_cast<unsigned int>(event));
        break;
    }
}

/* --------------------------------------------------------------------------
 * Point d'entrée principal
 * -------------------------------------------------------------------------- */

extern "C" void app_main(void)
{
    ESP_LOGI(
        TAG,
        "Démarrage du récepteur Bluetooth PCM5122");

    /*
     * Initialiser la sortie I2S avant que Bluetooth commence à fournir
     * des données PCM.
     */
    pcm5122_init();

    /*
     * Initialise:
     *
     * - NVS
     * - contrôleur Bluetooth
     * - Bluedroid
     * - Bluetooth Classic BR/EDR
     */
    ESP_ERROR_CHECK(
        bredr_app_common_init());

    /*
     * Démarre la tâche de traitement Bluetooth de l'exemple.
     */
    bt_app_task_start_up();

    /*
     * Lance l'initialisation du profil A2DP.
     */
    const bool dispatch_result =
        bt_app_work_dispatch(
            bt_av_hdl_stack_evt,
            BT_APP_EVT_STACK_UP,
            nullptr,
            0,
            nullptr,
            nullptr);

    if (!dispatch_result) {
        ESP_LOGE(
            TAG,
            "Impossible d'envoyer "
            "BT_APP_EVT_STACK_UP");

        abort();
    }
}
