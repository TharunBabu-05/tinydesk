/*
 * timer.c - a small pool of millisecond timers driven by the main loop.
 */
#include <stddef.h>

#include "tinydesk/td_input.h"

typedef struct {
    bool used;
    bool repeat;
    uint32_t interval;
    uint32_t due;
    td_timer_fn fn;
    void *user;
} timer_t_;

static timer_t_ s_timers[TD_MAX_TIMERS];

int td_timer_start(uint32_t interval_ms, bool repeat, td_timer_fn fn,
                   void *user, uint32_t now_ms)
{
    if (!fn) return -1;
    if (interval_ms == 0) interval_ms = 1;
    for (int i = 0; i < TD_MAX_TIMERS; i++) {
        if (s_timers[i].used) continue;
        s_timers[i].used = true;
        s_timers[i].repeat = repeat;
        s_timers[i].interval = interval_ms;
        s_timers[i].due = now_ms + interval_ms;
        s_timers[i].fn = fn;
        s_timers[i].user = user;
        return i;
    }
    return -1;
}

void td_timer_stop(int id)
{
    if (id >= 0 && id < TD_MAX_TIMERS) s_timers[id].used = false;
}

void td_timers_run(uint32_t now_ms)
{
    for (int i = 0; i < TD_MAX_TIMERS; i++) {
        timer_t_ *t = &s_timers[i];
        /* Signed difference handles the 32-bit millisecond wrap. */
        if (!t->used || (int32_t)(now_ms - t->due) < 0) continue;

        td_timer_fn fn = t->fn;
        void *user = t->user;
        if (t->repeat) {
            t->due += t->interval;
            /* If we fell far behind, do not try to catch up. */
            if ((int32_t)(now_ms - t->due) >= 0) t->due = now_ms + t->interval;
        } else {
            t->used = false;
        }
        fn(user);   /* may start or stop timers, including this one */
    }
}

void td_timers_reset(void)
{
    for (int i = 0; i < TD_MAX_TIMERS; i++) s_timers[i].used = false;
}
