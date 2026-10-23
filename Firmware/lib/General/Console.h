#ifndef _CONSOLE_H_
    #define _CONSOLE_H_

    #ifdef __cplusplus
        extern "C" {
    #endif

    #include <stdint.h>
    #include <stdio.h>
    #include "ainsi-color-codes.h"
    #include "Jauges.h"

    void console_init(void);
    void console_displayIntro(void);
    void console_test(void);
    void register_custom_commands(void);
    void console_printBin(int32_t);
    void console_printStats(const Jauges_t &, int);

    #ifdef __cplusplus
    }
    #endif

#endif