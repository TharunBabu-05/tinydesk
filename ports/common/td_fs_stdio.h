/*
 * td_fs_stdio.h - Files-app backend using the standard C library.
 */
#ifndef TD_FS_STDIO_H
#define TD_FS_STDIO_H

#include "tinydesk/td_sysinfo.h"

/* Filesystem operations rooted at `root` (the string is copied). */
const td_fs_ops_t *td_fs_stdio(const char *root);

#endif
