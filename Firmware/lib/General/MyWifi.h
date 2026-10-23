#ifndef _WIFI_H_
#define _WIFI_H_
    
    #include "esp_err.h"

    void MyWifiInit(void);
    void MyWifiScan(void);
    void MyWifiScanTask(void *pvParameters);

#endif