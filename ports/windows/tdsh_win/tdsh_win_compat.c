/*
 * tdsh_win_compat.c - virtual stdio streams and per-thread std slots for
 * running TinyDesk Shell on Windows (see tdsh_win_compat.h).
 */
#define USHW_IMPL
#include "tdsh_win_compat.h"

#include <windows.h>

#define VFILE_MAX 32

typedef struct {
    bool used;
    void *cookie;
    ushw_read_fn readfn;
    ushw_write_fn writefn;
    int (*closefn)(void *);
    bool eof;
    bool err;
} vfile_t;

static vfile_t s_vf[VFILE_MAX];
static INIT_ONCE s_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION s_lock;

static BOOL CALLBACK init_lock(PINIT_ONCE once, PVOID param, PVOID *ctx)
{
    (void)once;
    (void)param;
    (void)ctx;
    InitializeCriticalSection(&s_lock);
    return TRUE;
}

static vfile_t *as_virtual(FILE *f)
{
    vfile_t *v = (vfile_t *)(void *)f;
    if (v >= s_vf && v < s_vf + VFILE_MAX && v->used) return v;
    return NULL;
}

FILE *ushw_funopen(const void *cookie, ushw_read_fn readfn, ushw_write_fn writefn,
                   void *seekfn, int (*closefn)(void *))
{
    (void)seekfn;
    InitOnceExecuteOnce(&s_once, init_lock, NULL, NULL);
    EnterCriticalSection(&s_lock);
    vfile_t *v = NULL;
    for (int i = 0; i < VFILE_MAX; i++) {
        if (!s_vf[i].used) { v = &s_vf[i]; break; }
    }
    if (v) {
        memset(v, 0, sizeof(*v));
        v->used = true;
        v->cookie = (void *)cookie;
        v->readfn = readfn;
        v->writefn = writefn;
        v->closefn = closefn;
    }
    LeaveCriticalSection(&s_lock);
    if (!v) errno = EMFILE;
    return (FILE *)(void *)v;
}

/* ------------------------------------------------------ std slots */

static _Thread_local FILE *t_std[3];
static _Thread_local bool t_ready;

FILE **ushw_std(int which)
{
    if (!t_ready) {
        t_std[0] = stdin;
        t_std[1] = stdout;
        t_std[2] = stderr;
        t_ready = true;
    }
    return &t_std[which];
}

void ushw_get_std(FILE *out[3])
{
    for (int i = 0; i < 3; i++) out[i] = *ushw_std(i);
}

void ushw_set_std(FILE *const in[3])
{
    for (int i = 0; i < 3; i++) *ushw_std(i) = in[i];
}

/* ------------------------------------------------------- writing */

static int v_write(vfile_t *v, const char *buf, int len)
{
    if (!v->writefn) {
        v->err = true;
        return -1;
    }
    int done = 0;
    while (done < len) {
        int n = v->writefn(v->cookie, buf + done, len - done);
        if (n <= 0) {
            v->err = true;
            return -1;
        }
        done += n;
    }
    return done;
}

int ushw_vfprintf(FILE *f, const char *fmt, va_list ap)
{
    vfile_t *v = as_virtual(f);
    if (!v) return vfprintf(f, fmt, ap);

    char small[512];
    va_list copy;
    va_copy(copy, ap);
    int n = vsnprintf(small, sizeof(small), fmt, copy);
    va_end(copy);
    if (n < 0) return -1;
    if (n < (int)sizeof(small)) return v_write(v, small, n) < 0 ? -1 : n;

    char *big = malloc((size_t)n + 1);
    if (!big) return -1;
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    int rc = v_write(v, big, n) < 0 ? -1 : n;
    free(big);
    return rc;
}

int ushw_fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = ushw_vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int ushw_vprintf(const char *fmt, va_list ap) { return ushw_vfprintf(*ushw_std(1), fmt, ap); }

int ushw_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = ushw_vfprintf(*ushw_std(1), fmt, ap);
    va_end(ap);
    return n;
}

int ushw_fputs(const char *s, FILE *f)
{
    vfile_t *v = as_virtual(f);
    if (!v) return fputs(s, f);
    return v_write(v, s, (int)strlen(s)) < 0 ? EOF : 0;
}

int ushw_puts(const char *s)
{
    FILE *out = *ushw_std(1);
    if (ushw_fputs(s, out) == EOF) return EOF;
    return ushw_fputc('\n', out) == EOF ? EOF : 0;
}

int ushw_fputc(int c, FILE *f)
{
    vfile_t *v = as_virtual(f);
    if (!v) return fputc(c, f);
    char ch = (char)c;
    return v_write(v, &ch, 1) < 0 ? EOF : (unsigned char)ch;
}

int ushw_putchar(int c) { return ushw_fputc(c, *ushw_std(1)); }

size_t ushw_fwrite(const void *p, size_t size, size_t n, FILE *f)
{
    vfile_t *v = as_virtual(f);
    if (!v) return fwrite(p, size, n, f);
    if (size == 0 || n == 0) return 0;
    return v_write(v, p, (int)(size * n)) < 0 ? 0 : n;
}

/* ------------------------------------------------------- reading */

int ushw_fgetc(FILE *f)
{
    vfile_t *v = as_virtual(f);
    if (!v) return fgetc(f);
    if (!v->readfn || v->eof) return EOF;
    char c;
    int n = v->readfn(v->cookie, &c, 1);
    if (n == 1) return (unsigned char)c;
    if (n == 0) v->eof = true;
    else v->err = true;
    return EOF;
}

int ushw_getchar(void) { return ushw_fgetc(*ushw_std(0)); }

char *ushw_fgets(char *buf, int n, FILE *f)
{
    vfile_t *v = as_virtual(f);
    if (!v) return fgets(buf, n, f);
    if (n <= 0) return NULL;
    int i = 0;
    while (i < n - 1) {
        int c = ushw_fgetc(f);
        if (c == EOF) break;
        buf[i++] = (char)c;
        if (c == '\n') break;
    }
    buf[i] = '\0';
    return i > 0 ? buf : NULL;
}

size_t ushw_fread(void *p, size_t size, size_t n, FILE *f)
{
    vfile_t *v = as_virtual(f);
    if (!v) return fread(p, size, n, f);
    size_t want = size * n, got = 0;
    if (!v->readfn || want == 0) return 0;
    while (got < want && !v->eof) {
        int r = v->readfn(v->cookie, (char *)p + got, (int)(want - got));
        if (r == 0) v->eof = true;
        if (r < 0) v->err = true;
        if (r <= 0) break;
        got += (size_t)r;
    }
    return size ? got / size : 0;
}

/* ------------------------------------------------------- control */

int ushw_fflush(FILE *f)
{
    if (f && as_virtual(f)) return 0;
    return fflush(f);
}

int ushw_fclose(FILE *f)
{
    vfile_t *v = as_virtual(f);
    if (!v) return fclose(f);
    int rc = v->closefn ? v->closefn(v->cookie) : 0;
    EnterCriticalSection(&s_lock);
    v->used = false;
    LeaveCriticalSection(&s_lock);
    return rc;
}

int ushw_setvbuf(FILE *f, char *buf, int mode, size_t size)
{
    if (as_virtual(f)) return 0;   /* virtual streams are unbuffered */
    return setvbuf(f, buf, mode, size);
}

int ushw_ferror(FILE *f)
{
    vfile_t *v = as_virtual(f);
    return v ? v->err : ferror(f);
}

int ushw_feof(FILE *f)
{
    vfile_t *v = as_virtual(f);
    return v ? v->eof : feof(f);
}

void ushw_clearerr(FILE *f)
{
    vfile_t *v = as_virtual(f);
    if (!v) {
        clearerr(f);
        return;
    }
    v->eof = false;
    v->err = false;
}

void ushw_rewind(FILE *f)
{
    vfile_t *v = as_virtual(f);
    if (!v) {
        rewind(f);
        return;
    }
    v->eof = false;
    v->err = false;
}

void ushw_perror(const char *s)
{
    ushw_fprintf(*ushw_std(2), "%s%s%s\n", s ? s : "", s && *s ? ": " : "", strerror(errno));
}
