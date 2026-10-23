#pragma GCC diagnostic ignored "-Wdeprecated-enum-enum-conversion"
#pragma GCC diagnostic ignored "-Wenum-conversion"

#include "Console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "Config.h"

#include "esp_console.h"
#include "ESP32.h"
#include "MyWifi.h"
#include "lvgl.h"
#include "LcdDriver.h"
#include "GuiEngine.h" // Requis pour lancer les démos graphiques

// Séquences d'étoiles colorées pour l'introduction
static void print_colored_stars(int count) {
    for (int i = 0; i < count; i++) {
        int color_index = (abs(console_random()) % 216) + 16; 
        printf("\033[38;5;%dm*", color_index);
    }
    printf(AINSIrst); 
}

void console_displayIntro(void) {
    printf("\033[2J\033[H"); // Efface l'écran du terminal et replace le curseur en haut
    print_colored_stars(57); printf("\r\n");
    printf("   Use Tera Term in ANSI mode to support up/down arrows \r\n");
    printf("           Look nice with Consolas Font in 14           \r\n");
    printf("                  ESP32-WROOM-32E-N8R2                    \r\n");
    print_colored_stars(57); printf("\r\n");
}

void console_test(void) {
    print_colored_stars(57); printf("\r\n");
    print_colored_stars(57); printf("\r\n");
    print_colored_stars(57); printf("\r\n");
    printf(BRED "       Hey this is the color red, and it's bold! \r\n" AINSIrst);
    printf(RED  "               If " BLU " you" YEL " are" GRN " bored" CYN " do" MAG " this! \r\n" AINSIrst); 
    printf(BRED "               If " BBLU " you" BYEL " are" BGRN " bored" BCYN " do" BMAG " this! \r\n" AINSIrst); 
    printf(URED "               If " UBLU " you" UYEL " are" UGRN " bored" UCYN " do" UMAG " this! \r\n" AINSIrst);
    print_colored_stars(57); printf("\r\n");
    print_colored_stars(57); printf("\r\n");
    print_colored_stars(57); printf("\r\n"); 
}

// ============================================================================
// GESTIONNAIRES DE COMMANDES (HANDLERS)
// ============================================================================

int handler_colors(int argc, char **argv) {
    console_test();
    return 0;
}

int handler_restart(int argc, char **argv) {
    esp_restart();
    return 0;
}

int handler_detail(int argc, char **argv) {
    ESP32_detail();
    return 0;
}

int handler_wifi(int argc, char **argv) {
    MyWifiScan();
    return 0;
}

int handler_crash(int argc, char **argv) {
    ESP32_crash();
    return 0;
}

int handler_temp(int argc, char **argv) {
    printf("Internal Temperature: %.1fC\r\n", ESP32_ReadTemp());
    return 0;
}

int handler_lvglVersion(int argc, char **argv) {
    printf("LVGL version %d.%d.%d\r\n", lv_version_major(), lv_version_minor(), lv_version_patch());
    return 0;
}

int handler_touchCal(int argc, char **argv) {
    LcdDriver_touch_calibrate();
    return 0;
}



// ============================================================================
// ENREGISTREMENT DES COMMANDES DANS LE REPERTOIRE ESP-IDF
// ============================================================================

void register_custom_commands(void) {
    const esp_console_cmd_t colors_cmd = {
        .command = "colors", .help = "Test the color in the ANSI console",
        .hint = nullptr, .func = &handler_colors, .argtable = nullptr, .func_w_context = nullptr, .context = nullptr
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&colors_cmd));

    const esp_console_cmd_t restart_cmd = {
        .command = "restart", .help = "Restart the ESP32-WROOM-32E-N8R2",
        .hint = nullptr, .func = &handler_restart, .argtable = nullptr, .func_w_context = nullptr, .context = nullptr
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&restart_cmd));

    const esp_console_cmd_t detail_cmd = {
        .command = "detail", .help = "give detail about the ESP32-S3 uCtrl used",
        .hint = nullptr, .func = &handler_detail, .argtable = nullptr, .func_w_context = nullptr, .context = nullptr
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&detail_cmd));

    const esp_console_cmd_t wifi_cmd = {
        .command = "wifi", .help = "Scan all wifi SSID and give the strengh of the signal",
        .hint = nullptr, .func = &handler_wifi, .argtable = nullptr, .func_w_context = nullptr, .context = nullptr
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&wifi_cmd));

    const esp_console_cmd_t crash_cmd = {
        .command = "crash", .help = "List the cause of the last reset",
        .hint = nullptr, .func = &handler_crash, .argtable = nullptr, .func_w_context = nullptr, .context = nullptr
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&crash_cmd));

    const esp_console_cmd_t temp_cmd = {
        .command = "temp", .help = "Give the internal temperature of the ESP32",
        .hint = nullptr, .func = &handler_temp, .argtable = nullptr, .func_w_context = nullptr, .context = nullptr
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&temp_cmd));

    const esp_console_cmd_t LvglVersion_cmd = {
        .command = "lvglVersion", .help = "Give the version of the LVGL library",
        .hint = nullptr, .func = &handler_lvglVersion, .argtable = nullptr, .func_w_context = nullptr, .context = nullptr
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&LvglVersion_cmd));

    const esp_console_cmd_t touchCal_cmd = {
        .command = "touchCal", .help = "Calibrate the touch screen",
        .hint = nullptr, .func = &handler_touchCal, .argtable = nullptr, .func_w_context = nullptr, .context = nullptr
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&touchCal_cmd));
}

// ============================================================================
// INITIALISATION DE LA CONSOLE INTERACTIVE (REPL) - EXPOSÉE POUR LE MAIN
// ============================================================================

extern "C" void console_init(void) {
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    
    repl_config.prompt = BGRN CONFIG_IDF_TARGET ">" AINSIrst;
    repl_config.max_cmdline_length = CONFIG_CONSOLE_MAX_COMMAND_LINE_LENGTH;

    esp_console_register_help_command();
    console_displayIntro();
    register_custom_commands();

    ESP_ERROR_CHECK(esp_console_new_repl_stdio(&repl_config, &repl));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

void console_printBin(int32_t num) {
    // Loop through  24 bits, starting from the Most Significant Bit (MSB)
    for (int i = 23; i >= 0; i--) {
        // Shift the bit to the 1st position and check if it is 1 or 0
        int bit = (num >> i) & 1;
        printf("%d", bit);
        
        //Add a space every 4 bits for easier reading
        if (i % 4 == 0) {
            printf(" ");
        }
    }
}

void console_printStats(const Jauges_t &Jauge, int index)
{
    // Affichages de débogage brute inchangés
    printf("J%d: 0x%08lX ", index, Jauge.adc_reading);
    console_printBin(Jauge.adc_reading);
    printf(" %ld ", Jauge.adc_reading);
    printf("%2.8fV ", ((float)Jauge.adc_reading / MAX_ADC) * (V_EXC / Jauge.Gain_real));
    printf("%8.0f\xC2\xB5\xCE\xB5 \r\n", Jauge.uStrain);

    // Affichage des statistiques calculées
    printf("J%d -> Avg: %8.1f | StdDev: %5.2f \xC2\xB5\xCE\xB5\r\n", 
            index, Jauge.uStrain_avg, Jauge.uStrain_StdDev);
}
