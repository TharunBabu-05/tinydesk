/*
 * ota_esp.h - firmware updates on the ESP-IDF boards (see ota_esp.c).
 */
#ifndef OTA_ESP_H
#define OTA_ESP_H

#include "tinydesk/td_sysinfo.h"

const td_ota_ops_t *ota_esp_ops(void);

/* Confirm the running firmware (call once it has run healthily for a
 * while); until then a crash makes the bootloader go back. */
void ota_esp_boot_ok(void);

/* The root-only `ota` shell command. */
int ota_esp_register_command(void);

#endif
