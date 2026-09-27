/*
 * tdsh_platform_win.c - TinyDesk Shell platform API for Windows: time, sleep,
 * random bytes and a thread-based worker (so scripts run with a dedicated
 * stack and background scripts work), plus core initialisation.
 */
#include "tdsh_platform_win.h"

#include <windows.h>

/* Declared by stdlib.h only with _CRT_RAND_S, which must precede the
 * force-included compat header; declare it directly instead. */
int __cdecl rand_s(unsigned int *value);

static uint64_t win_monotonic_ms(void *ctx)
{
    (void)ctx;
    return GetTickCount64();
}

static void win_sleep_ms(void *ctx, uint32_t ms)
{
    (void)ctx;
    Sleep(ms);
}

static void win_yield(void *ctx)
{
    (void)ctx;
    SwitchToThread();
}

static int win_random_bytes(void *ctx, void *buffer, size_t length)
{
    (void)ctx;
    uint8_t *p = buffer;
    while (length > 0) {
        unsigned int v;
        if (rand_s(&v) != 0) return -EIO;
        for (int i = 0; i < 4 && length > 0; i++, length--) {
            *p++ = (uint8_t)v;
            v >>= 8;
        }
    }
    return 0;
}

typedef struct {
    tdsh_worker_fn_t worker;
    tdsh_worker_cleanup_fn_t cleanup;
    void *arg;
    FILE *std[3];     /* the caller's stdio, so script output lands in the same terminal */
    int result;
    bool background;
} worker_t;

static DWORD WINAPI worker_entry(LPVOID param)
{
    worker_t *w = param;
    ushw_set_std(w->std);
    w->result = w->worker(w->arg);
    if (w->cleanup) w->cleanup(w->arg);
    if (w->background) free(w);
    return 0;
}

static int win_worker_run(void *ctx, const char *name, size_t stack_bytes, int priority,
                          bool background, tdsh_worker_fn_t worker, void *arg,
                          tdsh_worker_cleanup_fn_t cleanup, int *result_out)
{
    (void)ctx;
    (void)name;
    (void)priority;
    if (!worker) return -EINVAL;
    worker_t *w = calloc(1, sizeof(*w));
    if (!w) return -ENOMEM;
    w->worker = worker;
    w->cleanup = cleanup;
    w->arg = arg;
    w->background = background;
    ushw_get_std(w->std);

    HANDLE h = CreateThread(NULL, stack_bytes, worker_entry, w, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (!h) {
        free(w);
        return -ENOMEM;
    }
    if (background) {
        CloseHandle(h);
        return 0;
    }
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
    if (result_out) *result_out = w->result;
    free(w);
    return 0;
}

static const tdsh_platform_api_t s_platform = {
    .name = "windows",
    .monotonic_ms = win_monotonic_ms,
    .sleep_ms = win_sleep_ms,
    .yield = win_yield,
    .random_bytes = win_random_bytes,
    .worker_run = win_worker_run,
};

static int make_dir(const char *path)
{
    if (_mkdir(path) == 0 || errno == EEXIST) return 0;
    return -errno;
}

int tdsh_win_init(const char *fs_root, const char *hostname)
{
    static const char *const dirs[] = { "", "/root", "/home", "/tmp", "/etc" };
    char path[TDSH_MAX_REAL_PATH];
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        snprintf(path, sizeof(path), "%s%s", fs_root, dirs[i]);
        int rc = make_dir(path);
        if (rc) return rc;
    }

    tdsh_core_config_t core = TDSH_CORE_CONFIG_DEFAULT();
    core.hostname = hostname;
    core.default_user = "root";
    core.fs_root = fs_root;
    core.platform = &s_platform;
    int rc = tdsh_core_init(&core);
    if (rc) return rc;
    rc = tdsh_register_core_builtins();
    if (rc) return rc;
    return tdsh_win_register_commands();   /* ifconfig, ping, date, tz, ... */
}
