/*
 * tdsh_bridge_esp.h - TinyDesk Shell in the tinydesk Terminal window (ESP-IDF).
 */
#ifndef TDSH_BRIDGE_ESP_H
#define TDSH_BRIDGE_ESP_H

#include "td_apps.h"

/* Backend for td_terminal_set_backend(). tdsh_espidf_init() must have
 * succeeded before the Terminal window is first opened. */
const td_term_backend_t *tdsh_bridge_esp_backend(void);

/* For long-running commands: true once if Ctrl+C was typed in the
 * Terminal window since the last call. Other keys typed meanwhile stay
 * queued for the shell. Only answers for commands on the desktop's own
 * console (false in SSH sessions). */
bool tdsh_bridge_break_requested(void);

#endif
