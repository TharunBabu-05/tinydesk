/*
 * counter.c - the minimal example app: a label, two buttons and a timer.
 */
#include "td_apps.h"

static td_window_t *s_win;
static td_widget_t *s_value, *s_timer, *s_auto;
static int s_count;
static int s_seconds;

static void show(void)
{
    td_widget_printf(s_value, "Count: %d", s_count);
    td_widget_printf(s_timer, "Window open for %d s", s_seconds);
}

static void on_plus(td_widget_t *w, void *user)
{
    (void)w;
    (void)user;
    s_count++;
    show();
}

static void on_reset(td_widget_t *w, void *user)
{
    (void)w;
    (void)user;
    s_count = 0;
    show();
}

/* Runs once a second while the window is open. */
static void on_tick(td_window_t *win)
{
    (void)win;
    s_seconds++;
    if (td_checkbox_get(s_auto)) s_count++;
    show();
}

static void on_close(td_window_t *win)
{
    (void)win;
    s_win = NULL;
}

static void launch(void)
{
    if (td_win_is_open(s_win)) {
        td_win_focus(s_win);
        return;
    }
    td_window_desc_t d = {
        .title = "Counter",
        .rect = td_rect(-1, -1, 30, 9),
        .flags = TD_WIN_MOVABLE | TD_WIN_CLOSABLE,
        .on_close = on_close,
        .on_tick = on_tick,
        .tick_ms = 1000,
    };
    s_win = td_win_create(&d);
    if (!s_win) return;
    s_seconds = 0;

    s_value = td_label(s_win, 2, 1, 0, "");
    td_button(s_win, 2, 3, "+1", on_plus, NULL);
    td_button(s_win, 10, 3, "Reset", on_reset, NULL);
    s_auto = td_checkbox(s_win, 2, 4, "Count every second", false, NULL, NULL);
    s_timer = td_label(s_win, 2, 6, 0, "");
    td_widget_set_color(s_timer, td_theme()->dim, TD_COLOR_DEFAULT);
    show();
}

static const td_app_t s_app = { "Counter", launch, "+1" };

void td_counter_register(void) { td_app_register(&s_app); }
