idf_component_register(
    SRCS
        "../Drivers/ADS1261/ads1261.c"
        "../Drivers/ADS1261/hal.c"
        "../Drivers/ADS1261/Jauges.cpp"
        "LcdDriver.cpp"
        "main.cpp"    
        "Console.cpp"
        "MyWifi.cpp"
        "ESP32.cpp"
        "GuiEngine.cpp"
        # AJOUT DES FICHIERS DE L'INTERFACE SQUARELINE :
        "../lib/UI/ui.c"
        "../lib/UI/ui_helpers.c"
        "../lib/UI/ui_events.cpp"
        "../lib/UI/screens/ui_Screen1.c" # Ajoutez ici chaque écran créé (.c) s'il y en a d'autres
        "../lib/UI/fonts/ui_font_Special.c"

    INCLUDE_DIRS
        "."
        "../lib/Config"
        "../lib/General"
        "../lib/UI"
        "../Drivers/ADS1261"
    REQUIRES
        driver
        esp_driver_i2s
        esp_driver_gpio
        esp_timer
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        console
        esp_stdio
        vfs
        esp_driver_spi  #Grace à lui, LVGL arrête de faire des erreurs sur les fichiers SPI
        lvgl
        bt 
        nvs_flash

)
