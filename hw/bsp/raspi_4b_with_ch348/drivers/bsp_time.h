#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "bsp.h"

uint32_t bsp_time_get_ms();

uint32_t bsp_time_get_us();

float bsp_time_get();

void bsp_time_init();

#ifdef __cplusplus
}
#endif
