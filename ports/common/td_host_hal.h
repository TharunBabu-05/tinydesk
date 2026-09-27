/*
 * td_host_hal.h - the desktop HAL (Windows console or POSIX terminal) and
 * the shared host start-up code.
 */
#ifndef TD_HOST_HAL_H
#define TD_HOST_HAL_H

#include "tinydesk/td.h"

/* Put the terminal into raw mode and return the HAL, or NULL if stdin /
 * stdout is not an interactive terminal. */
const td_hal_t *td_host_hal_open(void);

/* Restore the terminal mode (safe to call more than once). */
void td_host_hal_close(void);

/* Full desktop application used by ports/windows/main.c and
 * ports/posix/main.c: sysinfo, settings file, Files root, TinyDesk Shell terminal,
 * all demo apps. Returns the process exit code. */
int td_host_main(const char *platform_name);

/* Everything td_host_main() does before td_run(), for a caller that drives
 * the loop itself (tools/tdsim.c). */
void td_host_setup(const td_hal_t *hal, const char *platform_name);

/* Give the host build a network backend (tdsim uses a simulated one). */
void td_host_use_net(const td_net_ops_t *net);

/* Give the host build user accounts (tdsim uses test accounts; the desktop
 * build has none and belongs to root). */
void td_host_use_users(bool (*exists)(const char *), bool (*auth)(const char *, const char *));

#endif
