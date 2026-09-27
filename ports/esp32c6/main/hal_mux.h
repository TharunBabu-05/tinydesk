/*
 * hal_mux.h - tinydesk HAL for the ESP-IDF ports (see hal_mux.c).
 */
#ifndef HAL_MUX_H
#define HAL_MUX_H

#include "tinydesk/td_hal.h"

/* Set up the board's link and return the HAL (NULL on error). */
const td_hal_t *hal_mux_init(void);

#endif
