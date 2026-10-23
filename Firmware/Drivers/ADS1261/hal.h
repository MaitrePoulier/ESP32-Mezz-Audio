#ifndef HAL_H_
#define HAL_H_

#include <stdbool.h>
#include <stdint.h> // Ajouté pour s'assurer que uint32_t/uint8_t soient reconnus

// Si le fichier est lu par un compilateur C++, on ouvre un bloc extern "C"
#ifdef __cplusplus
extern "C" {
#endif

/* Function prototypes */
void _ads126x_set_active_device(uint8_t);
void delay_ms(uint32_t time_ms);
bool pollForDRDY(uint32_t timeout);
void setCS(bool state);
void setPWDN(bool state);
void setRESET(void);
void setSTART(bool state);
void enableExternalClock(bool state);
void toggleRESET(void);
void toggleSTART(void);
void setFault(bool state);
void SPI_SendReceive(uint8_t *ulDataTx, uint8_t *ulDataRx, uint8_t byteLength);

// MODIFICATION ICI : Ajout de 'const' devant char *command
void handleSPIerror(uint8_t *DataTx, uint8_t *DataRx, uint8_t byteLength, const char *command);

// Si le fichier est lu par un compilateur C++, on ferme le bloc extern "C"
#ifdef __cplusplus
}
#endif

#endif /* HAL_H_ */
