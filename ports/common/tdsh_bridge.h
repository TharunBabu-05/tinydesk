/*
 * tdsh_bridge.h - run TinyDesk Shell on its own thread and connect it to the
 * tinydesk Terminal app on desktop hosts (Windows and POSIX).
 */
#ifndef TDSH_BRIDGE_H
#define TDSH_BRIDGE_H

#include "td_apps.h"

/* Backend for td_terminal_set_backend(). The shell starts when the Terminal
 * window first opens; its files live under fs_root (created if missing). */
const td_term_backend_t *td_tdsh_host_backend(const char *fs_root, const char *hostname);

#endif
