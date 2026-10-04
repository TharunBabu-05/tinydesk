/*
 * counter_app.c - an app registered in the start menu: a label, two buttons
 * and a timer. This is the example used in the README.
 */
#include "td_host_hal.h"
#include "tinydesk/td.h"

static td_widget_t *s_label;
static int s_count;

static void show(void)
{
    td_widget_printf(s_label, "Count: %d", s_count);
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

/* Called every 1000 ms while the window is open. */
static void on_tick(td_window_t *win)
{
    (void)win;
    s_count++;
    show();
}

static void launch(void)
{
    td_window_desc_t desc = {
        .title = "Counter",
        .rect = td_rect(-1, -1, 28, 7),
        .flags = TD_WIN_MOVABLE | TD_WIN_CLOSABLE,
        .on_tick = on_tick,
        .tick_ms = 1000,
    };
    td_window_t *win = td_win_create(&desc);
    if (!win)
        return;
    s_label = td_label(win, 2, 1, 0, "");
    td_button(win, 2, 3, "+1", on_plus, NULL);
    td_button(win, 10, 3, "Reset", on_reset, NULL);
    show();
}

static const td_app_t counter_app = {"Counter", launch, "+1"};

int main(void)
{
    const td_hal_t *hal = td_host_hal_open();
    if (!hal)
        return 1;
    td_init(hal);
    td_app_register(&counter_app);   /* appears in the start menu (F10) */
    launch();
    td_run();                        /* start menu > Exit to quit */
    td_shutdown();
    td_host_hal_close();
    return 0;
}
