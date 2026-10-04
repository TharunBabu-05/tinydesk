/*
 * session.c - whose desktop this is.
 *
 * Mirrors TinyDesk Shell's user model: root sees the whole filesystem and lives in
 * /root; every other user lives in /home/<user> and is kept inside it. The
 * current user decides the Desktop folder, where the Files app may go and
 * which user the Terminal's shell runs as.
 *
 * The user changes when someone logs in to the desktop (Telnet, or
 * Start > Switch user...), and follows `login` / `logout` typed in the
 * Terminal. A change closes the previous user's windows and clears the
 * Terminal so nothing of theirs stays on screen.
 */
#include <stdio.h>
#include <string.h>

#include "td_apps.h"

#define NAME_LEN  32
#define PATH_LEN  TD_PATH_MAX
#define FOLLOW_MS 1000
#define EXPECT_MS 10000

static char s_user[NAME_LEN] = "root";
static char s_home[PATH_LEN];
static char s_jail[PATH_LEN];
static char s_expect[NAME_LEN];      /* user we asked the shell to become */
static uint32_t s_expect_until;
static char s_login_name[NAME_LEN];  /* Switch user dialog */
static bool s_started;

const char *td_session_user(void)
{
    return s_user;
}
bool td_session_is_root(void)
{
    return strcmp(s_user, "root") == 0;
}
const char *td_session_home(void)
{
    return s_home;
}
const char *td_session_jail(void)
{
    return s_jail;
}

static void compute_paths(void)
{
    const td_fs_ops_t *fs = td_sysinfo()->fs;
    const char *root = fs ? fs->root : "";
    if (td_session_is_root())
    {
        snprintf(s_home, sizeof(s_home), "%s/root", root);
        snprintf(s_jail, sizeof(s_jail), "%s", root);
    }
    else
    {
        snprintf(s_home, sizeof(s_home), "%s/home/%.*s", root, NAME_LEN - 1, s_user);
        snprintf(s_jail, sizeof(s_jail), "%s", s_home);
    }
}

bool td_session_real_path(const char *path, char *real, size_t cap)
{
    const td_fs_ops_t *fs = td_sysinfo()->fs;
    if (!fs || !path || !path[0] || strstr(path, ".."))
        return false;
    char home[PATH_LEN], logical[PATH_LEN + 96];
    snprintf(home, sizeof(home), "%s", td_shell_path(s_home));
    if (path[0] == '~')
        snprintf(logical, sizeof(logical), "%s%s", home, path + 1);
    else if (path[0] == '/')
        snprintf(logical, sizeof(logical), "%s", path);
    else
        snprintf(logical, sizeof(logical), "%s/%s", home, path);
    if (!td_session_is_root())
    {
        size_t n = strlen(home);
        if (strncmp(logical, home, n) != 0 || (logical[n] != '/' && logical[n] != '\0'))
            return false;
    }
    return snprintf(real, cap, "%s%s", fs->root, logical) < (int)cap;
}

static const char *shell_user(void)
{
    const td_term_backend_t *b = td_terminal_backend();
    const char *u = (b && b->user) ? b->user(b->ctx) : NULL;
    return (u && u[0]) ? u : NULL;
}

/* Ask the shell to redraw its prompt (TinyDesk Shell's line editor: Ctrl+L). */
static void shell_redraw(void)
{
    const td_term_backend_t *b = td_terminal_backend();
    if (b && b->write)
        b->write(b->ctx, (const uint8_t *)"\x0c", 1);
}

void td_session_switch(const char *user, bool switch_shell)
{
    if (!user || !user[0])
        return;
    if (strcmp(user, s_user) == 0 && !switch_shell)
        return;

    /* Nothing of the previous user may stay on screen. */
    td_menu_close();
    td_wm_close_all();
    td_terminal_reset();

    snprintf(s_user, sizeof(s_user), "%s", user);
    compute_paths();
    td_files_reset();
    td_settings_apply_saved();       /* their theme, pattern, clock format */
    td_clipboard_clear();            /* nothing copied by the previous user */

    const td_term_backend_t *b = td_terminal_backend();
    if (switch_shell && b && b->set_user)
    {
        snprintf(s_expect, sizeof(s_expect), "%s", user);
        s_expect_until = td_millis() + EXPECT_MS;
        b->set_user(b->ctx, user);
    }
    else
    {
        shell_redraw();
    }

    td_desktop_folder_init();
    td_wm_set_user_label(s_user);
    td_logf('I', "desktop session: %s", s_user);
    td_wm_invalidate();
}

/* `login` / `logout` in the Terminal change the shell's user: follow it. */
static void follow_shell(void *user)
{
    (void)user;
    const char *su = shell_user();
    if (!su)
        return;
    if (s_expect[0])
    {
        /* We asked for a switch; wait until the shell has made it. */
        if (strcmp(su, s_expect) != 0 && (int32_t)(s_expect_until - td_millis()) > 0)
            return;
        s_expect[0] = '\0';
    }
    if (strcmp(su, s_user) != 0)
        td_session_switch(su, false);
}

/* ------------------------------------------------ Start > Switch user */

static void password_answer(const char *password, void *user)
{
    (void)user;
    const td_sysinfo_t *si = td_sysinfo();
    bool ok = si->authenticate && si->authenticate(s_login_name, password);
    if (!ok)
    {
        td_msgbox("Switch user", "Wrong user name or password.", "OK", NULL, NULL);
        return;
    }
    td_session_switch(s_login_name, true);
}

static void name_answer(const char *name, void *user)
{
    (void)user;
    const td_sysinfo_t *si = td_sysinfo();
    if (!si->user_exists || !si->user_exists(name))
    {
        td_msgbox("Switch user", "There is no user with that name.", "OK", NULL, NULL);
        return;
    }
    snprintf(s_login_name, sizeof(s_login_name), "%s", name);
    char prompt[64];
    snprintf(prompt, sizeof(prompt), "Password for %.32s:", name);
    td_passwordbox("Switch user", prompt, password_answer, NULL);
}

static void switch_user_menu(void)
{
    td_inputbox("Switch user", "User name:", "", name_answer, NULL);
}

/* ------------------------------------------------------------- init */

void td_session_init(void)
{
    const char *su = shell_user();
    snprintf(s_user, sizeof(s_user), "%s", su ? su : "root");
    compute_paths();
    td_files_reset();
    td_settings_apply_saved();
    td_desktop_folder_init();
    td_wm_set_user_label(s_user);
    if (s_started)
        return;
    s_started = true;
    td_timer_start(FOLLOW_MS, true, follow_shell, NULL, td_millis());
    if (td_sysinfo()->authenticate)
        td_wm_add_start_item("Switch user...", switch_user_menu);
}
