#ifndef _GENERAL_H_
#define _GENERAL_H_
    // ESP32-WROOM-32E-N8R2
    #include "esp_random.h"

    //For the screen
    //MHS4001 - ST7796S - 16 bits
    #define SPI_FREQUENCY  80*1000000

    #define SCREEN_WIDTH   480
    #define SCREEN_HEIGHT  320

    #define LCD_HOST  SPI3_HOST
    #define TFT_MISO  19
    #define TFT_MOSI  23
    #define TFT_SCLK  18
    #define TFT_CS     5  // Chip select control pin
    #define TFT_DC     4  // Data Command control pin
    #define TFT_RST   33  // Reset pin (could connect to RST pin)

    //XPT2046
    #define TOUCH_CS   2  // Chip select pin (T_CS) of touch screen

    //ADS1261B
    //HSPI
    #define JAUGES_HOST  SPI2_HOST
    #define JAUGES_FREQUENCY 10*1000000
    #define SPI_SCLK  14
    #define SPI_MISO  12
    #define SPI_MOSI  13

    #define SPI_CS1   15
    #define DataReady 34
    #define SPI_CS2   26
    #define SPI_CS3   25
    #define SPI_CS4   27
    #define SPI_CS5   32


    //Define the random number source
    inline int32_t console_random(void){return esp_random();}

    /* Standard 16-bit RGB565 Color Definitions */
    #define RGB565_BLACK   0x0000
    #define RGB565_WHITE   0xFFFF
    #define RGB565_RED     0xF800
    #define RGB565_GREEN   0x07E0
    #define RGB565_BLUE    0x001F
    #define RGB565_YELLOW  0xFFE0
    #define RGB565_CYAN    0x07FF
    #define RGB565_MAGENTA 0xF81F
    #define RGB565_GRAY    0x7BEF
    #define RGB565_ORANGE  0xFD20
    #define RGB565_MAROON  0x7800
    #define RGB565_PURPLE  0x780F
    #define RGB565_OLIVE   0x7BE0
    #define RGB565_NAVY    0x000F
    #define RGB565_TEAL    0x03EF



#endif
