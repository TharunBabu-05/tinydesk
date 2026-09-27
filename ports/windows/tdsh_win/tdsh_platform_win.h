/*
 * tdsh_platform_win.h - start the TinyDesk Shell core on Windows.
 */
#ifndef TDSH_PLATFORM_WIN_H
#define TDSH_PLATFORM_WIN_H

#include "tdsh.h"

/* Create the sandbox tree under fs_root (root, home, tmp, etc), initialise
 * the core and register the portable built-in commands. Returns 0. */
int tdsh_win_init(const char *fs_root, const char *hostname);

/* The Windows versions of the POSIX port's extra commands (ifconfig, ping,
 * date, cal, tz, write, hostpath, capabilities); called by tdsh_win_init(). */
int tdsh_win_register_commands(void);

#endif
