/*
 * rs485.h - the board's RS-485 lines (from the board configuration) as
 * Modbus RTU lines; NULL when none is configured (see rs485.c).
 */
#ifndef RS485_H
#define RS485_H

#include "td_modbus.h"

const td_mb_serial_t *rs485_serial(void);

#endif
