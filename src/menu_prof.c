/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-ui-suxty - menu_prof.c
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


/* The per-game option pages of the pause menu: video plugin options, audio options (the volume and the audio plugin's
 * own), a few emulation options and the game profile.  Core/plugin options are listed from the plugin's own config
 * section (profile.c), so any plugin's parameters can be edited; the edit scope (g_cfg.scope) says whether a change is
 * saved globally or for this game only.  Drawn by menu.c's page stack; see menu_int.h and profile.h. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <SDL.h>

#include "cfg.h"
#include "menu.h"
#include "menu_int.h"
#include "pad.h"
#include "profile.h"
#include "remap.h"
#include "ui.h"

static char l_msg[128];
static int  l_msg_kind;

static void set_msg(const char *text, int kind)
{
    snprintf(l_msg, sizeof(l_msg), "%s", text ? text : "");
    l_msg_kind = kind;
}

static int scope_game(void) { return g_cfg.scope == CFG_SCOPE_GAME; }

/* ---- the parameter list of a section -------------------------------------------------------------------- */

static struct {
    char          section[48];
    int           n;
    profile_param list[PROFILE_MAX_PARAMS];
    int           valid;
} l_ps;

static const char *const l_emu_keys[] = { "R4300Emulator", "DisableExtraMem", "CountPerOp", NULL };

/* only = NULL: every parameter; else just these names, in this order */
static void load_list(const char *section, const char *const *only)
{
    if (l_ps.valid && strcmp(l_ps.section, section) == 0)
        return;
    snprintf(l_ps.section, sizeof(l_ps.section), "%s", section);
    l_ps.valid = 1;
    if (only == NULL)
        l_ps.n = profile_list_params(section, l_ps.list, PROFILE_MAX_PARAMS);
    else
    {
        profile_param all[PROFILE_MAX_PARAMS];
        int n = profile_list_params(section, all, PROFILE_MAX_PARAMS), i, k;
        l_ps.n = 0;
        for (k = 0; only[k] != NULL; k++)
            for (i = 0; i < n; i++)
                if (strcasecmp(all[i].name, only[k]) == 0)
                    l_ps.list[l_ps.n++] = all[i];
    }
}

/* ---- how a parameter is edited --------------------------------------------------------------------------- */

typedef struct {
    const char *section, *name;
    int         n;
    const char *names[10];
    const char *vals[10];
} choice_def;

/* Parameters whose valid values are known; everything else is a toggle (bool) or a number stepper (int / float).  A
 * string parameter with no table here is not shown (a path or a free text cannot be edited with a gamepad). */
static const choice_def l_choices[] = {
    { "Video-Foir", "FrameSkip", 5, { "Off", "Every 2nd", "2 of 3", "3 of 4", "Auto" }, { "0", "1", "2", "3", "auto" } },
    { "Video-Foir", "Threads", 9, { "None", "Auto", "1", "2", "3", "4", "5", "6", "7" }, { "-1", "0", "1", "2", "3", "4", "5", "6", "7" } },
    { "Core", "R4300Emulator", 3, { "Pure interpreter", "Cached interpreter", "Dynarec" }, { "0", "1", "2" } },
    { "Audio-SDL", "RESAMPLE", 7, { "trivial", "speex 4", "speex 10", "linear", "sinc fastest", "sinc medium", "sinc best" },
      { "trivial", "speex-fixed-4", "speex-fixed-10", "src-linear", "src-sinc-fastest", "src-sinc-medium-quality", "src-sinc-best-quality" } },
};

static const choice_def *find_choice(const char *sec, const char *name)
{
    size_t i;
    for (i = 0; i < sizeof(l_choices) / sizeof(l_choices[0]); i++)
        if (strcasecmp(l_choices[i].section, sec) == 0 && strcasecmp(l_choices[i].name, name) == 0)
            return &l_choices[i];
    return NULL;
}

/* a stored value as the menu names it ("1" -> "Cached interpreter") */
static const char *display_value(const choice_def *cd, const char *val)
{
    int i;
    if (cd != NULL)
        for (i = 0; i < cd->n; i++)
            if (strcasecmp(cd->vals[i], val) == 0)
                return cd->names[i];
    return val;
}

static int step_for(int v)
{
    int a = v < 0 ? -v : v, s = 1;
    while (s * 32 < a)
        s *= 2;
    return s;
}

static int l_nrow;                     /* focusable rows drawn so far on this page */
static const char *l_focus_name;       /* the parameter under the cursor (for the status line) */
static char l_focus_help[160];
static char l_focus_note[96];

/* One parameter row.  Returns 1 when the value changed. */
static int param_row(const char *sec, const profile_param *pp)
{
    char val[256], nv[256];
    const choice_def *cd = find_choice(sec, pp->name);
    int over = profile_overridden(sec, pp->name), changed = 0;
    int focused;

    if (!profile_get_scoped(sec, pp->name, val, sizeof(val)))
        return 0;
    if (cd != NULL)
    {
        const char *names[11];
        char custom[64];
        int idx = -1, i, n = cd->n;
        for (i = 0; i < n; i++)
        {
            names[i] = cd->names[i];
            if (strcasecmp(cd->vals[i], val) == 0)
                idx = i;
        }
        if (idx < 0)
        {
            snprintf(custom, sizeof(custom), "%.60s", val);
            names[n] = custom;
            idx = n++;
        }
        if (ui_choice(pp->name, &idx, names, n) && idx < cd->n)
        {
            profile_set(sec, pp->name, cd->vals[idx]);
            changed = 1;
        }
    }
    else if (pp->type == M64TYPE_BOOL)
    {
        int on = strcasecmp(val, "true") == 0;
        if (ui_toggle(pp->name, &on))
        {
            profile_set(sec, pp->name, on ? "True" : "False");
            changed = 1;
        }
    }
    else if (pp->type == M64TYPE_INT)
    {
        int v = atoi(val), dir = ui_step(pp->name, val);
        if (dir != 0)
        {
            snprintf(nv, sizeof(nv), "%d", v + dir * step_for(v));
            profile_set(sec, pp->name, nv);
            changed = 1;
        }
    }
    else if (pp->type == M64TYPE_FLOAT)
    {
        double v = atof(val), a = v < 0 ? -v : v, step = a < 1 ? 0.05 : a < 10 ? 0.25 : 1.0;
        int dir = ui_step(pp->name, val);
        if (dir != 0)
        {
            snprintf(nv, sizeof(nv), "%.4f", v + dir * step);
            profile_set(sec, pp->name, nv);
            changed = 1;
        }
    }
    else
        return 0;                                   /* a free string: not editable here */

    focused = (l_nrow == ui_focus_get());
    l_nrow++;
    ui_mark_last(menu_mark_kind(over));
    if (changed)
        set_msg(over && !scope_game() ? "Saved for all games (this game keeps its own)" : "Applies after a restart", 0);
    if (ui_x_last() && scope_game() && over)
    {
        profile_reset(sec, pp->name);
        cfg_save();
        set_msg("Reset (applies after a restart)", 1);
        changed = 1;
    }
    if (focused)
    {
        const char *h = profile_param_help(sec, pp->name);
        char gv[256];
        l_focus_name = pp->name;
        snprintf(l_focus_help, sizeof(l_focus_help), "%s", h ? h : "");
        l_focus_note[0] = 0;
        if (over && !scope_game() && profile_get_game(sec, pp->name, gv, sizeof(gv)))
            snprintf(l_focus_note, sizeof(l_focus_note), "This game uses %.60s", display_value(cd, gv));
        else if (over)
            snprintf(l_focus_note, sizeof(l_focus_note), "Saved for this game");
    }
    return changed;
}

/* the common end of a parameters page: restart button, back button, status line */
static void params_footer(const char *hint)
{
    int pend = profile_pending();
    if (pend > 0)
    {
        if (ui_button("Restart game to apply"))
            menu_push(PAGE_CONFIRM_RESTART);
        l_nrow++;
    }
    if (ui_button("Back"))
        menu_pop();
    l_nrow++;

    if (l_msg[0])
        ui_status_bar(l_msg, l_msg_kind);
    else if (l_focus_name != NULL && l_focus_note[0])
        ui_status_bar(l_focus_note, 1);
    else if (pend > 0 && ui_focus_get() >= l_nrow - 2)
        ui_status_bar("Plugin options apply after a restart", 0);
    else if (l_focus_name != NULL && l_focus_help[0])
        ui_status_bar(l_focus_help, 0);
    else if (pend > 0)
        ui_status_bar("Changed: apply with a restart", 0);
    else
        ui_status_bar(scope_game() ? "Saved for this game only" : "Saved for all games", 0);
    ui_panel_end(hint);
}

static const char *const l_hint_params = "A Change   X Reset   B Back";

/* ---- Video / Audio / Emulation ------------------------------------------------------------------------------ */

static void page_section(const char *base, m64p_plugin_type type, int audio, int back)
{
    char sec[48], title[112], head[96];
    int i, nshown = 0;

    l_nrow = 0;
    l_focus_name = NULL;
    l_focus_note[0] = 0;
    if (!profile_plugin_section(type, sec, sizeof(sec)))
        sec[0] = 0;
    if (sec[0])
        snprintf(head, sizeof(head), "%s: %s", base, strchr(sec, '-') ? strchr(sec, '-') + 1 : sec);
    else
        snprintf(head, sizeof(head), "%s", base);
    ui_next_panel_width(90);
    ui_panel_begin_scroll(menu_scoped_title(title, sizeof(title), head), 9, 1);

    if (audio)
    {
        /* the core's own volume: live, and the one audio option every plugin has */
        int ov = cfg_overridden("audio", "volume");
        int v = cfg_get_scoped("audio", "volume", "")[0] ? cfg_get_scoped_int("audio", "volume", 80) : profile_volume_now();
        int unset = !cfg_get_scoped("audio", "volume", "")[0];
        char text[24];
        int dir;
        if (v < 0)
            v = 80;
        snprintf(text, sizeof(text), unset ? "%d%% (plugin)" : "%d%%", v);
        dir = ui_step("Volume", text);
        if (l_nrow == ui_focus_get())
        {
            l_focus_name = "Volume";
            l_focus_help[0] = 0;
            if (ov && !scope_game())
                snprintf(l_focus_note, sizeof(l_focus_note), "This game uses %d%%", cfg_get_int("audio", "volume", 0));
            else if (ov)
                snprintf(l_focus_note, sizeof(l_focus_note), "Saved for this game");
            else
                snprintf(l_focus_help, sizeof(l_focus_help), "Applies at once (X: plugin default)");
        }
        l_nrow++;
        ui_mark_last(menu_mark_kind(ov));
        if (dir != 0)
        {
            v += dir * 5;
            v = v < 0 ? 0 : v > 100 ? 100 : v;
            cfg_set_int("audio", "volume", v);
            profile_apply_volume();
            set_msg("Volume applied now", 1);
        }
        else if (ui_x_last())
        {
            if (scope_game() && ov)
            {
                cfg_reset("audio", "volume");
                set_msg("Reset: this game uses the global volume", 1);
            }
            else if (!scope_game())
            {
                ini_remove(g_cfg.ini, "audio", "volume");
                cfg_apply();
                set_msg("Volume: the plugin's own default", 0);
            }
            profile_apply_volume();
        }
    }
    if (sec[0])
    {
        load_list(sec, NULL);
        for (i = 0; i < l_ps.n; i++)
            param_row(sec, &l_ps.list[i]);
        nshown = l_ps.n;
    }
    if (!audio && (!sec[0] || nshown == 0))
        ui_label("No options for this plugin");
    params_footer(l_hint_params);
    if (back)
        menu_pop();
}

static void page_emu(int back)
{
    char title[48];
    int i;

    l_nrow = 0;
    l_focus_name = NULL;
    l_focus_note[0] = 0;
    ui_next_panel_width(80);
    ui_panel_begin_scroll(menu_scoped_title(title, sizeof(title), "Emulation"), 6, 1);
    load_list("Core", l_emu_keys);
    for (i = 0; i < l_ps.n; i++)
        param_row("Core", &l_ps.list[i]);
    params_footer(l_hint_params);
    if (back)
        menu_pop();
}

/* ---- the game profile ----------------------------------------------------------------------------------------- */

static void page_profile(int back)
{
    char line[96], title[32];
    int n = cfg_game_override_count();

    ui_panel_begin_scroll(menu_scoped_title(title, sizeof(title), "Game profile"), 7, 1);
    if (!cfg_game_available())
    {
        ui_label("This ROM has no MD5:");
        ui_label("no per-game settings");
    }
    else
    {
        snprintf(line, sizeof(line), "%d saved for this game", n);
        ui_label(line);
        snprintf(line, sizeof(line), "%.8s...cfg", g_cfg.game_md5);
        ui_label(line);
        if (ui_button_dis("Reset all to global", n == 0) > 0)
            menu_push(PAGE_CONFIRM_PROFILE_RESET);
        if (profile_pending() > 0 && ui_button("Restart game to apply"))
            menu_push(PAGE_CONFIRM_RESTART);
    }
    if (ui_button("Back"))
        menu_pop();
    if (l_msg[0])
        ui_status_bar(l_msg, l_msg_kind);
    else if (profile_pending() > 0)
        ui_status_bar("Changed plugin options need a restart", 0);
    ui_panel_end("A Select   B Back");
    if (back)
        menu_pop();
}

static void page_confirm_profile_reset(int back)
{
    ui_panel_begin_ex("Confirm", 8, 72);
    ui_label("Forget every setting saved");
    ui_label("for this game?");
    if (ui_button("No"))
        menu_pop();
    if (ui_button("Yes"))
    {
        int c = 0;
        c += remap_reset_game();
        profile_reset_all();              /* first, so the plugin options count towards "restart to apply" */
        c += cfg_reset_all_game();
        cfg_save();
        remap_flush();
        set_msg("This game now uses the global settings", 1);
        (void) c;
        menu_pop();
    }
    ui_panel_end("A Select   B Back");
    if (back)
        menu_pop();
}

static int page_confirm_restart(int back)
{
    int result = MENU_NONE;
    ui_panel_begin_ex("Restart game", 10, 76);
    ui_label("Save and start again so the");
    ui_label("plugins read the new options?");
    ui_label("(you continue where you were)");
    if (ui_button("No"))
        menu_pop();
    if (ui_button("Yes"))
        result = MENU_RESTART;
    ui_panel_end("A Select   B Back");
    if (back)
        menu_pop();
    return result;
}

/* ---- dispatch --------------------------------------------------------------------------------------------------- */

int prof_owns_page(menu_page p) { return p >= PAGE_VIDEO; }

int prof_frame(menu_page page, uint32_t pressed, int back)
{
    (void) pressed;
    switch (page)
    {
        case PAGE_VIDEO:   page_section("Video", M64PLUGIN_GFX, 0, back); break;
        case PAGE_AUDIO:   page_section("Audio", M64PLUGIN_AUDIO, 1, back); break;
        case PAGE_EMU:     page_emu(back); break;
        case PAGE_PROFILE: page_profile(back); break;
        case PAGE_CONFIRM_PROFILE_RESET: page_confirm_profile_reset(back); break;
        case PAGE_CONFIRM_RESTART:       return page_confirm_restart(back);
        default: break;
    }
    return MENU_NONE;
}

void prof_leave_page(menu_page p)
{
    if (p >= PAGE_VIDEO)
    {
        profile_flush();                 /* the global values edited on the page go to mupen64plus.cfg now */
        cfg_save();
        set_msg("", 0);
    }
}

void prof_reset(void)
{
    l_ps.valid = 0;
    set_msg("", 0);
}
