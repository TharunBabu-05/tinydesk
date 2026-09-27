/*
 * net_esp.h - network control for the Network app (ESP32-C6).
 */
#ifndef NET_ESP_H
#define NET_ESP_H

#include "tinydesk/td_sysinfo.h"

const td_net_ops_t *net_esp_ops(void);

#endif
