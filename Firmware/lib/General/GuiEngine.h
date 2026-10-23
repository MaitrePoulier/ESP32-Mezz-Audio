#ifndef _GUI_ENGINE_H_
    #define _GUI_ENGINE_H_

    #ifdef __cplusplus
        extern "C" {
    #endif

    /**
     * @brief Initialise LVGL, alloue les buffers DMA internes,
     *        configure le Watchdog et lance la tâche de rendu sur le Core 1.
     */
    void GuiEngine_init(void);

    #ifdef __cplusplus
    }
    #endif

#endif // _GUI_ENGINE_H_
