#ifndef __JAUGES_H__
    #define __JAUGES_H__

    #include "esp_err.h"
    #include "ads1261.h"

    #define V_EXC  5.0f        // Votre tension d'excitation (SEN+ / SEN-)
    #define MAX_ADC 8388607    // 2^23 - 1
    #define nbJauges  5        //nombre de Jauges qui sont présentement ouvertes

    typedef enum{
        GAIN_1	    = ((uint8_t) 0x00),
	    GAIN_2      = ((uint8_t) 0x01),
	    GAIN_4      = ((uint8_t) 0x02),
	    GAIN_8      = ((uint8_t) 0x03),
	    GAIN_16     = ((uint8_t) 0x04),
	    GAIN_32     = ((uint8_t) 0x05),
	    GAIN_64     = ((uint8_t) 0x06),
	    GAIN_128    = ((uint8_t) 0x07)
    } PGA_t;


    typedef struct{
        PGA_t   Gain = GAIN_32;
        float   Gain_real = 32;
        float   G_F = 2.0f;          // Facteur de jauge typique (à vérifier sur la datasheet de votre jauge)
        int32_t adc_reading;       //valeurs
        float   uStrain;             //val_microstrain
        float   uStrain_avg;         //moyenne
        float   uStrain_StdDev;      //variance
        int     Contrainte = 0;
        int     ModuleYoung = 70;
        double  Somme = 0;
        double  Somme_sq = 0;
    }Jauges_t;

    const float pga_valeurs_reelles[] = {1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f, 128.0f};
    
    #ifdef __cplusplus
    extern "C" {
    #endif

        esp_err_t Jauges_init(void);
        int32_t   Jauges_read(int);
        esp_err_t Jauges_newValue(Jauges_t &,int32_t);
        esp_err_t Jauges_calcStats(Jauges_t &);

    #ifdef __cplusplus
    }
    #endif

#endif 