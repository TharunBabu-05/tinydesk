/*
 * theme.c - the built-in colour themes and the desktop pattern.
 *
 * Colours are xterm 256-colour palette indexes; 0..15 are the classic
 * ANSI colours (0 black, 1 red, 2 green, 3 yellow/brown, 4 blue,
 * 5 magenta, 6 cyan, 7 light grey, 8..15 the bright versions).
 */
#include "tinydesk/td_wm.h"

static const td_theme_t s_themes[] = {
    {
        .name = "Classic",
        .desktop_fg = 3, .desktop_bg = 4,
        .win_fg = 0, .win_bg = 7,
        .frame_fg = 8, .frame_active_fg = 15,
        .title_fg = 15, .title_bg = 6,
        .title_inactive_fg = 7, .title_inactive_bg = 8,
        .button_fg = 0, .button_bg = 2,
        .focus_fg = 15, .focus_bg = 10,
        .input_fg = 15, .input_bg = 1,
        .select_fg = 0, .select_bg = 6,
        .shadow_fg = 8, .shadow_bg = 0,
        .taskbar_fg = 0, .taskbar_bg = 7,
        .taskbar_active_fg = 15, .taskbar_active_bg = 6,
        .menu_fg = 0, .menu_bg = 7,
        .menu_select_fg = 15, .menu_select_bg = 6,
        .accent = 1,
        .dim = 8,
        .icon_fg = 15,
        .close_hover_fg = 15, .close_hover_bg = 1,
    },
    {
        .name = "Dark",
        .desktop_fg = 24, .desktop_bg = 234,   /* a clearly visible teal pattern */
        .win_fg = 252, .win_bg = 236,
        .frame_fg = 240, .frame_active_fg = 75,
        .title_fg = 231, .title_bg = 25,
        .title_inactive_fg = 246, .title_inactive_bg = 238,
        .button_fg = 252, .button_bg = 239,
        .focus_fg = 231, .focus_bg = 31,
        .input_fg = 252, .input_bg = 233,
        .select_fg = 231, .select_bg = 24,
        .shadow_fg = 236, .shadow_bg = 232,
        .taskbar_fg = 250, .taskbar_bg = 233,
        .taskbar_active_fg = 231, .taskbar_active_bg = 25,
        .menu_fg = 252, .menu_bg = 238,
        .menu_select_fg = 231, .menu_select_bg = 25,
        .accent = 75,
        .dim = 244,
        .icon_fg = 252,
        .close_hover_fg = 231, .close_hover_bg = 160,
    },
};

#define THEME_COUNT ((int)(sizeof(s_themes) / sizeof(s_themes[0])))

static int s_current = TD_THEME_DEFAULT;
static uint32_t s_pattern = 0x2591;   /* light shade */
static bool s_icons = true;

const td_theme_t *td_theme(void) { return &s_themes[s_current]; }
int td_theme_count(void) { return THEME_COUNT; }
int td_theme_index(void) { return s_current; }

const td_theme_t *td_theme_get(int index)
{
    if (index < 0 || index >= THEME_COUNT) return NULL;
    return &s_themes[index];
}

void td_theme_set(int index)
{
    if (index < 0 || index >= THEME_COUNT) return;
    s_current = index;
    td_wm_invalidate();
}

void td_desktop_set_pattern(uint32_t ch)
{
    s_pattern = ch ? ch : ' ';
    td_wm_invalidate();
}

uint32_t td_desktop_pattern(void) { return s_pattern; }

void td_desktop_set_icons(bool on)
{
    s_icons = on;
    td_wm_invalidate();
}

bool td_desktop_icons(void) { return s_icons; }
