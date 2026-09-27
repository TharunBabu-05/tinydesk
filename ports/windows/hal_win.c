/*
 * hal_win.c - tinydesk HAL for the Windows console (Windows Terminal
 * recommended). Virtual-terminal processing makes the console understand
 * the same escape sequences as a Unix terminal, and virtual-terminal input
 * delivers keys and mouse reports as escape sequences too.
 */
#include <windows.h>

#include "../common/td_host_hal.h"

typedef struct {
    HANDLE in, out;
    DWORD in_mode, out_mode;
    UINT in_cp, out_cp;
    bool active;
    uint8_t queue[256];     /* UTF-8 bytes waiting to be read */
    int q_head, q_count;
    WCHAR high_surrogate;
} win_ctx_t;

static win_ctx_t s_ctx;

static void queue_byte(win_ctx_t *c, uint8_t b)
{
    if (c->q_count == (int)sizeof(c->queue)) return;
    c->queue[(c->q_head + c->q_count) % sizeof(c->queue)] = b;
    c->q_count++;
}

static void queue_char(win_ctx_t *c, WCHAR wc)
{
    uint32_t cp = wc;
    if (wc >= 0xD800 && wc <= 0xDBFF) {
        c->high_surrogate = wc;
        return;
    }
    if (wc >= 0xDC00 && wc <= 0xDFFF) {
        if (!c->high_surrogate) return;
        cp = 0x10000u + (((uint32_t)c->high_surrogate - 0xD800u) << 10) + (wc - 0xDC00u);
        c->high_surrogate = 0;
    }
    uint8_t u[4];
    int n = td_utf8_encode(cp, u);
    for (int i = 0; i < n; i++) queue_byte(c, u[i]);
}

/* Move whatever the console has into our byte queue, without blocking. */
static void poll_console(win_ctx_t *c)
{
    DWORD pending = 0;
    if (!GetNumberOfConsoleInputEvents(c->in, &pending) || pending == 0) return;
    INPUT_RECORD rec[32];
    DWORD got = 0;
    if (!ReadConsoleInputW(c->in, rec, pending < 32 ? pending : 32, &got)) return;
    for (DWORD i = 0; i < got; i++) {
        if (rec[i].EventType != KEY_EVENT) continue;
        const KEY_EVENT_RECORD *k = &rec[i].Event.KeyEvent;
        if (!k->bKeyDown || k->uChar.UnicodeChar == 0) continue;
        for (WORD r = 0; r < (k->wRepeatCount ? k->wRepeatCount : 1); r++) queue_char(c, k->uChar.UnicodeChar);
    }
}

static int win_read_byte(void *ctx)
{
    win_ctx_t *c = ctx;
    if (c->q_count == 0) poll_console(c);
    if (c->q_count == 0) return -1;
    uint8_t b = c->queue[c->q_head];
    c->q_head = (c->q_head + 1) % (int)sizeof(c->queue);
    c->q_count--;
    return b;
}

static int win_write(void *ctx, const uint8_t *buf, int len)
{
    win_ctx_t *c = ctx;
    DWORD written = 0;
    if (!WriteFile(c->out, buf, (DWORD)len, &written, NULL)) return 0;
    return (int)written;
}

static uint32_t win_millis(void *ctx)
{
    (void)ctx;
    return (uint32_t)GetTickCount64();
}

static void win_sleep(void *ctx, uint32_t ms)
{
    (void)ctx;
    Sleep(ms);
}

static const td_hal_t s_hal = { win_read_byte, win_write, win_millis, win_sleep, &s_ctx };

static BOOL WINAPI on_console_event(DWORD type)
{
    (void)type;
    td_host_hal_close();
    return FALSE;   /* let the default handler end the process */
}

const td_hal_t *td_host_hal_open(void)
{
    win_ctx_t *c = &s_ctx;
    c->in = GetStdHandle(STD_INPUT_HANDLE);
    c->out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleMode(c->in, &c->in_mode) || !GetConsoleMode(c->out, &c->out_mode)) return NULL;
    c->in_cp = GetConsoleCP();
    c->out_cp = GetConsoleOutputCP();

    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleMode(c->out, ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING |
                               DISABLE_NEWLINE_AUTO_RETURN);
    /* No line input, no echo, no Ctrl+C processing, no quick-edit: every key
     * (and mouse report) reaches us as VT bytes. */
    SetConsoleMode(c->in, ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_EXTENDED_FLAGS);
    SetConsoleCtrlHandler(on_console_event, TRUE);
    c->active = true;
    return &s_hal;
}

void td_host_hal_close(void)
{
    win_ctx_t *c = &s_ctx;
    if (!c->active) return;
    c->active = false;
    SetConsoleMode(c->in, c->in_mode);
    SetConsoleMode(c->out, c->out_mode);
    SetConsoleCP(c->in_cp);
    SetConsoleOutputCP(c->out_cp);
}
