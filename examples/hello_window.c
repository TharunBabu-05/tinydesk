/*
 * hello_window.c - the smallest tinydesk program: one window with a label
 * and a button that quits.
 *
 * Build with the host CMake project and run ./build/hello_window.
 */
#include "td_host_hal.h"
#include "tinydesk/td.h"

static void on_quit(td_widget_t *w, void *user)
{
    (void)w;
    (void)user;
    td_quit();
}

int main(void)
{
    const td_hal_t *hal = td_host_hal_open();
    if (!hal)
        return 1;
    td_init(hal);

    td_window_desc_t desc = {
        .title = "Hello",
        .rect = td_rect(-1, -1, 32, 7),     /* centred */
        .flags = TD_WIN_MOVABLE,
    };
    td_window_t *win = td_win_create(&desc);
    td_label(win, 2, 1, 0, "Hello from TinyDesk!");
    td_button(win, 10, 3, "Quit", on_quit, NULL);

    td_run();
    td_shutdown();
    td_host_hal_close();
    return 0;
}
