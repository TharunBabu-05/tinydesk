/*
 * tdsh_win_compat.h - lets the unmodified TinyDesk Shell core build and run on
 * Windows (MinGW-w64 / UCRT).
 *
 * This header is force-included (-include) into the TinyDesk Shell sources and the
 * tinydesk shell bridge only. It provides what the core expects from newlib
 * or glibc but the Windows C runtime lacks:
 *
 *   - assignable, per-thread stdin/stdout/stderr (the core redirects them
 *     for pipes, command substitution and the terminal session);
 *   - funopen(): FILE streams backed by read/write callbacks;
 *   - mkdir(path, mode).
 *
 * Every stdio call the core makes is routed through ushw_* functions that
 * handle these virtual streams and forward real FILEs to the C runtime.
 */
#ifndef TDSH_WIN_COMPAT_H
#define TDSH_WIN_COMPAT_H

/* Pull in every system header the core uses before any macro below is
 * defined, so their declarations are not rewritten. */
#include <ctype.h>
#include <direct.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <io.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*ushw_read_fn)(void *cookie, char *buf, int len);
typedef int (*ushw_write_fn)(void *cookie, const char *buf, int len);

/* BSD-style funopen (seek is not supported and must be NULL). */
FILE *ushw_funopen(const void *cookie, ushw_read_fn readfn, ushw_write_fn writefn,
                   void *seekfn, int (*closefn)(void *));

/* This thread's stdin (0), stdout (1) or stderr (2) slot. */
FILE **ushw_std(int which);

/* Copy the calling thread's slots so a new thread can inherit them. */
void ushw_get_std(FILE *out[3]);
void ushw_set_std(FILE *const in[3]);

int ushw_printf(const char *fmt, ...);
int ushw_fprintf(FILE *f, const char *fmt, ...);
int ushw_vprintf(const char *fmt, va_list ap);
int ushw_vfprintf(FILE *f, const char *fmt, va_list ap);
int ushw_puts(const char *s);
int ushw_fputs(const char *s, FILE *f);
int ushw_putchar(int c);
int ushw_fputc(int c, FILE *f);
int ushw_getchar(void);
int ushw_fgetc(FILE *f);
char *ushw_fgets(char *buf, int n, FILE *f);
size_t ushw_fwrite(const void *p, size_t size, size_t n, FILE *f);
size_t ushw_fread(void *p, size_t size, size_t n, FILE *f);
int ushw_fflush(FILE *f);
int ushw_fclose(FILE *f);
int ushw_setvbuf(FILE *f, char *buf, int mode, size_t size);
int ushw_ferror(FILE *f);
int ushw_feof(FILE *f);
void ushw_clearerr(FILE *f);
void ushw_rewind(FILE *f);
void ushw_perror(const char *s);

#ifdef __cplusplus
}
#endif

#ifndef USHW_IMPL
#undef stdin
#undef stdout
#undef stderr
#define stdin  (*ushw_std(0))
#define stdout (*ushw_std(1))
#define stderr (*ushw_std(2))

#undef printf
#undef fprintf
#undef vprintf
#undef vfprintf
#undef puts
#undef fputs
#undef putchar
#undef putc
#undef fputc
#undef getchar
#undef getc
#undef fgetc
#undef fgets
#undef fwrite
#undef fread
#undef fflush
#undef fclose
#undef setvbuf
#undef ferror
#undef feof
#undef clearerr
#undef rewind
#undef perror
#define printf   ushw_printf
#define fprintf  ushw_fprintf
#define vprintf  ushw_vprintf
#define vfprintf ushw_vfprintf
#define puts     ushw_puts
#define fputs    ushw_fputs
#define putchar  ushw_putchar
#define putc     ushw_fputc
#define fputc    ushw_fputc
#define getchar  ushw_getchar
#define getc     ushw_fgetc
#define fgetc    ushw_fgetc
#define fgets    ushw_fgets
#define fwrite   ushw_fwrite
#define fread    ushw_fread
#define fflush   ushw_fflush
#define fclose   ushw_fclose
#define setvbuf  ushw_setvbuf
#define ferror   ushw_ferror
#define feof     ushw_feof
#define clearerr ushw_clearerr
#define rewind   ushw_rewind
#define perror   ushw_perror

#define funopen(cookie, r, w, s, c) ushw_funopen((cookie), (r), (w), (s), (c))
#undef mkdir
#define mkdir(path, mode) _mkdir(path)
#endif /* USHW_IMPL */

#endif /* TDSH_WIN_COMPAT_H */
