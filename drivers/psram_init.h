#ifndef PSRAM_INIT_H
#define PSRAM_INIT_H

#include "pico/stdlib.h"

void psram_init(uint cs_pin);
extern int psram_max_freq_mhz;  // MHz, before psram_init
void psram_exit_qpi(void);       // before any reset

#endif
