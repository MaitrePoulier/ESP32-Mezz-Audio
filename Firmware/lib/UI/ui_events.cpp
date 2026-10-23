#include "ui_cpp_wrapper.h" 
#include "ui_events.h"
#include <stdlib.h> 

// Déclaration de votre fonction de sélection de périphérique si elle n'est pas dans vos headers
extern "C" void _ads126x_set_active_device(uint8_t index);

extern "C" {

    extern Jauges_t Jauges[5];

    // =========================================================================
    // 🛠️ FONCTIONS LOGIQUES GÉNÉRIQUES WITH CHIP SELECT SELECTION
    // =========================================================================

    static void handle_calibrate(uint8_t jauge_index)
    {
        // 2. Envoyer la commande d'étalonnage protégée par Mutex
        sendCommand(OPCODE_SYOCAL, jauge_index);

        printf("Jauge #%d - Étalonnage (Calibrate) exécuté\r\n", jauge_index + 1);
    }

    static void handle_pga_change(lv_obj_t * dropdown, uint8_t jauge_index)
    {
        uint16_t index = lv_dropdown_get_selected(dropdown);
        
        // Mettre à jour la structure locale
        Jauges[jauge_index].Gain = (PGA_t)index;
        Jauges[jauge_index].Gain_real = 1 << index;

        // 1. Activer physiquement le bon CS/Puce pour cette jauge
        _ads126x_set_active_device(jauge_index);

        // 2. Appliquer le gain sur le registre de la puce active
        ads1261_set_gain((uint8_t)index, jauge_index); 

        printf("Jauge #%d - Gain appliqué : 0x%02X\r\n", jauge_index + 1, readSingleRegister(REG_ADDR_PGA));
    }

    static void handle_gf_change(lv_obj_t * dropdown, uint8_t jauge_index)
    {
        char selected_text[16];
        lv_dropdown_get_selected_str(dropdown, selected_text, sizeof(selected_text));
        
        float gf_value = strtof(selected_text, NULL);
        Jauges[jauge_index].G_F = gf_value;
        
        printf("Jauge #%d - Facteur de jauge (GF) mis à jour : %.1f\r\n", jauge_index + 1, Jauges[jauge_index].G_F);
    }

    static void handle_young_change(lv_obj_t * dropdown, uint8_t jauge_index)
    {
        char selected_text[16];
        lv_dropdown_get_selected_str(dropdown, selected_text, sizeof(selected_text));
        
        int young_value = (int)strtol(selected_text, NULL, 10);
        Jauges[jauge_index].ModuleYoung = young_value;
        
        printf("Jauge #%d - Module de Young mis à jour : %d GPa\r\n", jauge_index + 1, Jauges[jauge_index].ModuleYoung);
    }


    // =========================================================================
    // 📥 ÉVÉNEMENTS SQUARELINE STUDIO (REDIRECTIONS CONCISES)
    // =========================================================================

    // --- JAUGE #1 (Index 0) ---
    void Calibrate1(lv_event_t * e)   { handle_calibrate(0); }
    void PGA1(lv_event_t * e)         { handle_pga_change((lv_obj_t *)lv_event_get_target(e), 0); }
    void GF1(lv_event_t * e)          { handle_gf_change((lv_obj_t *)lv_event_get_target(e), 0); }
    void ModuleYoung1(lv_event_t * e) { handle_young_change((lv_obj_t *)lv_event_get_target(e), 0); }

    // --- JAUGE #2 (Index 1) ---
    void Calibrate2(lv_event_t * e)   { handle_calibrate(1); }
    void PGA2(lv_event_t * e)         { handle_pga_change((lv_obj_t *)lv_event_get_target(e), 1); }
    void GF2(lv_event_t * e)          { handle_gf_change((lv_obj_t *)lv_event_get_target(e), 1); }
    void ModuleYoung2(lv_event_t * e) { handle_young_change((lv_obj_t *)lv_event_get_target(e), 1); }

    // --- JAUGE #3 (Index 2) ---
    void Calibrate3(lv_event_t * e)   { handle_calibrate(2); }
    void PGA3(lv_event_t * e)         { handle_pga_change((lv_obj_t *)lv_event_get_target(e), 2); }
    void GF3(lv_event_t * e)          { handle_gf_change((lv_obj_t *)lv_event_get_target(e), 2); }
    void ModuleYoung3(lv_event_t * e) { handle_young_change((lv_obj_t *)lv_event_get_target(e), 2); }

    // --- JAUGE #4 (Index 3) ---
    void Calibrate4(lv_event_t * e)   { handle_calibrate(3); }
    void PGA4(lv_event_t * e)         { handle_pga_change((lv_obj_t *)lv_event_get_target(e), 3); }
    void GF4(lv_event_t * e)          { handle_gf_change((lv_obj_t *)lv_event_get_target(e), 3); }
    void ModuleYoung4(lv_event_t * e) { handle_young_change((lv_obj_t *)lv_event_get_target(e), 3); }

    // --- JAUGE #5 (Index 4) ---
    void Calibrate5(lv_event_t * e)   { handle_calibrate(4); }
    void PGA5(lv_event_t * e)         { handle_pga_change((lv_obj_t *)lv_event_get_target(e), 4); }
    void GF5(lv_event_t * e)          { handle_gf_change((lv_obj_t *)lv_event_get_target(e), 4); }
    void ModuleYoung5(lv_event_t * e) { handle_young_change((lv_obj_t *)lv_event_get_target(e), 4); }

}
