/*
 * hal_posix.c - tinydesk HAL for Linux / macOS terminals: termios raw mode,
 * poll() for non-blocking input, write() for output.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "../common/td_host_hal.h"

static struct termios s_saved;
static bool s_active;

static int posix_read_byte(void *ctx)
{
    (void)ctx;
    struct pollfd p = { .fd = STDIN_FILENO, .events = POLLIN };
    if (poll(&p, 1, 0) <= 0 || !(p.revents & POLLIN)) return -1;
    unsigned char b;
    return read(STDIN_FILENO, &b, 1) == 1 ? b : -1;
}

static int posix_write(void *ctx, const uint8_t *buf, int len)
{
    (void)ctx;
    int done = 0;
    while (done < len) {
        ssize_t n = write(STDOUT_FILENO, buf + done, (size_t)(len - done));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        done += (int)n;
    }
    return done;
}

static uint32_t posix_millis(void *ctx)
{
    (void)ctx;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

static void posix_sleep(void *ctx, uint32_t ms)
{
    (void)ctx;
    struct timespec ts = { (time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L };
    nanosleep(&ts, NULL);
}

static const td_hal_t s_hal = { posix_read_byte, posix_write, posix_millis, posix_sleep, NULL };

/* Leave the terminal usable even when killed. */
static void on_signal(int sig)
{
    static const char restore[] = "\x1b[?1006l\x1b[?1003l\x1b[?1002l\x1b[?1000l\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l";
    (void)!write(STDOUT_FILENO, restore, sizeof(restore) - 1);
    td_host_hal_close();
    signal(sig, SIG_DFL);
    raise(sig);
}

const td_hal_t *td_host_hal_open(void)
{
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return NULL;
    if (tcgetattr(STDIN_FILENO, &s_saved) != 0) return NULL;

    struct termios raw = s_saved;
    raw.c_iflag &= (tcflag_t)~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= (tcflag_t)~OPOST;
    raw.c_cflag |= CS8;
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN | ISIG);   /* Ctrl+C arrives as 0x03 */
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return NULL;
    s_active = true;

    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
    signal(SIGINT, on_signal);
    return &s_hal;
}

void td_host_hal_close(void)
{
    if (!s_active) return;
    s_active = false;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &s_saved);
}
