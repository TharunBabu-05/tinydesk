/*
 * update.c - the Software Update app: shows the installed firmware and
 * installs a new one from a URL or a file on the device, with progress.
 * Everything goes through td_sysinfo()->ota (the ESP32 port provides it).
 *
 * Only root may install or roll back; everyone can look.
 */
#include <stdio.h>
#include <string.h>

#include "td_apps.h"

static td_window_t *s_win;
static td_widget_t *s_source, *s_check, *s_install, *s_cancel, *s_progress, *s_restart, *s_rollback;
static char s_note[96];

static const td_ota_ops_t *ota(void) { return td_sysinfo()->ota; }

static void note(const char *msg)
{
    snprintf(s_note, sizeof(s_note), "%s", msg);
    td_wm_invalidate();
}

static bool allowed(void)
{
    if (td_session_is_root()) return true;
    note("Only root can install updates.");
    return false;
}

static bool is_url(const char *s) { return !strncmp(s, "http://", 7) || !strncmp(s, "https://", 8); }

static void start(bool check_only)
{
    if (!ota() || !allowed()) return;
    const char *text = s_source ? s_source->text : "";
    char real[200];
    const char *src = text;
    if (!text[0]) {
        note("Type a URL (http:// or https://) or a .bin file on this device.");
        return;
    }
    if (!is_url(text)) {
        if (!td_session_real_path(text, real, sizeof(real))) {
            note("That file is outside your home folder.");
            return;
        }
        src = real;
    }
    if (!ota()->start(src, check_only)) note("An update is already running.");
    else s_note[0] = '\0';
}

static void on_check(td_widget_t *w, void *user) { (void)w; (void)user; start(true); }
static void on_install(td_widget_t *w, void *user) { (void)w; (void)user; start(false); }
static void on_enter(td_widget_t *w, void *user) { (void)w; (void)user; start(true); }

static void on_cancel(td_widget_t *w, void *user)
{
    (void)w;
    (void)user;
    if (ota() && allowed()) ota()->cancel();
}

static void restart_answer(int button, void *user)
{
    (void)user;
    if (button == 0 && ota()) ota()->restart();
}

static void on_restart(td_widget_t *w, void *user)
{
    (void)w;
    (void)user;
    if (!ota() || !allowed()) return;
    td_msgbox("Software update", "Restart now? Unsaved work is lost.", "Restart|Later",
              restart_answer, NULL);
}

static void rollback_answer(int button, void *user)
{
    (void)user;
    if (button == 0 && ota() && !ota()->roll_back()) note("Could not go back to the previous version.");
}

static void on_rollback(td_widget_t *w, void *user)
{
    (void)w;
    (void)user;
    if (!ota() || !allowed()) return;
    td_ota_info_t i;
    ota()->info(&i);
    char text[96];
    snprintf(text, sizeof(text), "Go back to version %s and restart?", i.on_trial ? "(previous)" : i.other_version);
    td_msgbox("Software update", text, "Roll back|Cancel", rollback_answer, NULL);
}

/* "1.02 MB" / "640 KB" */
static void size_text(uint32_t bytes, char *buf, size_t cap)
{
    if (bytes >= 1024u * 1024u) snprintf(buf, cap, "%u.%02u MB", (unsigned)(bytes >> 20), (unsigned)((bytes % (1u << 20)) * 100 >> 20));
    else snprintf(buf, cap, "%u KB", (unsigned)(bytes / 1024));
}

static void on_tick(td_window_t *win)
{
    (void)win;
    if (!ota()) return;
    td_ota_status_t st;
    ota()->status(&st);
    td_ota_info_t i;
    ota()->info(&i);
    bool running = st.state == TD_OTA_CHECKING || st.state == TD_OTA_INSTALLING;
    td_progress_set(s_progress, st.state == TD_OTA_DONE ? 100 : st.percent > 0 ? st.percent : 0);
    td_widget_set_visible(s_cancel, running);
    td_widget_set_visible(s_restart, st.state == TD_OTA_DONE);
    td_widget_set_visible(s_rollback, !running && st.state != TD_OTA_DONE && (i.on_trial || i.can_roll_back));
    td_widget_set_visible(s_check, !running);
    td_widget_set_visible(s_install, !running);
    td_wm_invalidate();
}

static void on_draw(td_window_t *win, int w, int h)
{
    (void)win;
    (void)h;
    const td_theme_t *t = td_theme();
    if (!ota()) {
        td_text(1, 1, "Firmware updates are not available on this platform.", t->win_fg, t->win_bg, 0);
        td_text(1, 2, "(The ESP32 build updates itself over the network or from a file.)", t->dim, t->win_bg, 0);
        return;
    }
    td_ota_info_t i;
    ota()->info(&i);
    td_ota_status_t st;
    ota()->status(&st);
    char line[200];

    snprintf(line, sizeof(line), "Installed   TinyDesk %s  (built %s, ESP-IDF %s)", i.version, i.built, i.sdk);
    td_textn(1, 0, line, w - 1, t->win_fg, t->win_bg, TD_BOLD);
    snprintf(line, sizeof(line), "Running     from %s%s", i.running,
             i.on_trial ? ", on trial: confirms itself 30 s after start-up" : ", confirmed");
    td_textn(1, 1, line, w - 1, i.on_trial ? t->accent : t->win_fg, t->win_bg, 0);
    if (i.other_version[0]) snprintf(line, sizeof(line), "Other slot  %s holds version %s", i.next, i.other_version);
    else snprintf(line, sizeof(line), "Other slot  %s is empty (updates go there)", i.next);
    td_textn(1, 2, line, w - 1, t->dim, t->win_bg, 0);

    td_text(1, 4, "Update from", t->win_fg, t->win_bg, 0);
    td_textn(1, 5, "http:// or https:// URL of a tinydesk .bin, or a file here (e.g. ~/tinydesk.bin)", w - 1,
             t->dim, t->win_bg, 0);

    if (st.new_version[0]) {
        snprintf(line, sizeof(line), "Update      TinyDesk %s  (built %s)", st.new_version, st.new_built);
        td_textn(1, 8, line, w - 1, t->win_fg, t->win_bg, 0);
    }
    if (st.state == TD_OTA_INSTALLING || st.state == TD_OTA_DONE || (st.state == TD_OTA_FAILED && st.done)) {
        char done[16], total[16];
        size_text(st.done, done, sizeof(done));
        size_text(st.total, total, sizeof(total));
        if (st.total && st.bytes_per_s && st.state == TD_OTA_INSTALLING) {
            uint32_t left = (st.total - st.done) / st.bytes_per_s;
            snprintf(line, sizeof(line), "%3d%%   %s of %s   %u KB/s   about %u s left", st.percent, done, total,
                     (unsigned)(st.bytes_per_s / 1024), (unsigned)left);
        } else {
            snprintf(line, sizeof(line), "%3d%%   %s of %s", st.state == TD_OTA_DONE ? 100 : st.percent, done, total);
        }
        td_textn(1, 11, line, w - 1, t->win_fg, t->win_bg, 0);
    }
    const char *msg = s_note[0] ? s_note : st.message;
    uint8_t fg = st.state == TD_OTA_FAILED && !s_note[0] ? t->accent : t->win_fg;
    td_textn(1, 12, msg, w - 1, fg, t->win_bg, st.state == TD_OTA_DONE ? TD_BOLD : 0);
    if (!td_session_is_root())
        td_textn(1, 14, "Only root can install updates or roll back.", w - 1, t->dim, t->win_bg, 0);
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
        .title = "Software Update",
        .rect = td_rect(-1, -1, 76, 19),
        .flags = TD_WIN_MOVABLE | TD_WIN_CLOSABLE,
        .on_draw = on_draw,
        .on_close = on_close,
        .on_tick = on_tick,
        .tick_ms = 250,
    };
    s_win = td_win_create(&d);
    if (!s_win) return;
    s_note[0] = '\0';
    if (!ota()) return;
    s_source = td_textbox(s_win, 13, 4, 60, TD_TEXT_MAX - 1, on_enter, NULL);
    s_check = td_button(s_win, 1, 6, "Check", on_check, NULL);
    s_install = td_button(s_win, 11, 6, "Install", on_install, NULL);
    s_cancel = td_button(s_win, 23, 6, "Cancel", on_cancel, NULL);
    s_progress = td_progress(s_win, 1, 10, 72);
    s_restart = td_button(s_win, 1, -1, "Restart now", on_restart, NULL);
    s_rollback = td_button(s_win, 19, -1, "Roll back", on_rollback, NULL);
    td_widget_set_text(s_source, "https://");
    on_tick(s_win);
    td_widget_focus(s_source);
}

static const td_app_t s_app = { "Software Update", launch, "\xE2\x86\x91 " };   /* ↑ */

void td_update_register(void) { td_app_register(&s_app); }
