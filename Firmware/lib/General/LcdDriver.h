#ifndef LCD_DRIVER_H
    #define LCD_DRIVER_H

    #include "Config.h"
    #include "driver/spi_master.h"
    #include "esp_err.h"

    esp_err_t LcdDriver_init(void);
    esp_err_t LcdDriver_set_window(int x0, int y0, int x1, int y1);
    uint16_t  LcdDriver_get_random_bright_color(void);


    void my_flush_cb(void * disp, const void * area, uint8_t * px_map); 
    void LcdDriver_touch_read(void * indev, void * data);
    void LcdDriver_touch_calibrate(void);


#endif // LCD_DRIVER_H