/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - menu_av.c
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

/* The Video and Audio option pages and the game profile page of the pause menu.
 *
 *   Video   foir's options, listed from foir's own interface table (foir_if.h); hidden when foir is not the video plugin.
 *   Audio   the core's volume, live (audio.h); hidden entirely if the core can't report it.
 *   Game profile   "Reset all to global" for this game.
 *
 * The edit scope (g_cfg.scope) says whether a change is stored for all games (suxty.cfg) or for this game only.
 * Drawn by menu.c's page stack; see menu_int.h. */

#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "cfg.h"
#include "foir_if.h"
#include "menu.h"
#include "menu_int.h"
#include "pad.h"
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

int av_video_available(void) { return fif_get() != NULL; }
int av_audio_available(void) { return audio_volume_now() >= 0; }

static int         l_nrow;                     /* focusable rows drawn so far on this page */
static const char *l_focus_help;               /* the option under the cursor */
static char        l_focus_note[96];

/* ---- Video ---------------------------------------------------------------------------------------------------- */

/* One foir option: the widget for its type, then what a change / X does. */
static void option_row(unsigned i)
{
    const struct foir_suxty_option *o = fif_option(i);
    int v = fif_shown(i), nv = v, changed = 0, over = fif_overridden(i), focused;
    char text[48];

    if (o == NULL)
        return;
    switch (o->type)
    {
        case FOIR_OPT_BOOL:
            if (ui_toggle(o->label, &nv))
                changed = 1;
            break;
        case FOIR_OPT_ENUM:
            if (ui_choice(o->label, &nv, o->enum_labels, o->max + 1))
                changed = 1;
            break;
        default:
        {
            int dir;
            snprintf(text, sizeof(text), "%d", v);
            dir = ui_step(o->label, text);
            if (dir != 0)
            {
                nv = v + dir * (o->step > 0 ? o->step : 1);
                nv = nv < o->min ? o->min : nv > o->max ? o->max : nv;
                changed = nv != v;
            }
            break;
        }
    }
    focused = l_nrow == ui_focus_get();
    l_nrow++;
    ui_mark_last(menu_mark_kind(over));
    if (changed)
    {
        int r = fif_set(i, nv);
        if (r < 0)
            set_msg("foir refused the change", -1);
        else if (r == FOIR_SUXTY_NEXT_LAUNCH)
            set_msg("Takes effect the next time the game starts", 1);
        else
            set_msg(scope_game() ? "Applied now; saved for this game" : "Applied now; saved for all games", 1);
    }
    else if (ui_x_last() && scope_game() && over)
    {
        fif_reset(i);
        set_msg("Reset to the global value", 1);
    }
    if (focused)
    {
        l_focus_help = o->help;
        l_focus_note[0] = 0;
        if (!(o->flags & FOIR_OPTF_LIVE))
            snprintf(l_focus_note, sizeof(l_focus_note), fif_pending(i) ? "Changed: applies the next time the game starts"
                                                                        : "Read when the game starts (not live)");
        else if (over && !scope_game())
            snprintf(l_focus_note, sizeof(l_focus_note), "This game uses its own value");
    }
}

static void page_video(int back)
{
    char title[48];
    unsigned i, n = fif_count();
    int any_next = 0;

    l_nrow = 0;
    l_focus_help = NULL;
    l_focus_note[0] = 0;
    ui_next_panel_width(90);
    ui_panel_begin_scroll(menu_scoped_title(title, sizeof(title), "Video: foir"), 9, 1);
    for (i = 0; i < n; i++)
        if (fif_option(i)->flags & FOIR_OPTF_LIVE)
            option_row(i);
    for (i = 0; i < n; i++)
        if (!(fif_option(i)->flags & FOIR_OPTF_LIVE))
        {
            if (!any_next)
                ui_label("- Applies at the next launch -");
            any_next = 1;
            option_row(i);
        }
    if (ui_button("Save as foir default"))
    {
        if (fif_save_defaults() == 0)
            set_msg("Written to [Video-Foir] in mupen64plus.cfg", 1);
        else
            set_msg("Couldn't write mupen64plus.cfg", -1);
    }
    l_nrow++;
    if (ui_button("Back"))
        menu_pop();
    l_nrow++;

    if (l_msg[0])
        ui_status_bar(l_msg, l_msg_kind);
    else if (l_focus_note[0])
        ui_status_bar(l_focus_note, 0);
    else if (l_focus_help != NULL)
        ui_status_bar(l_focus_help, 0);
    else
        ui_status_bar(scope_game() ? "Saved for this game only" : "Saved for all games", 0);
    ui_panel_end("A Change   X Reset   B Back");
    if (back)
        menu_pop();
}

/* ---- Audio ------------------------------------------------------------------------------------------------------ */

static void page_audio(int back)
{
    char title[48], text[24];
    int ov = cfg_overridden("audio", "volume");
    int unset = !cfg_get_scoped("audio", "volume", "")[0];
    int v = unset ? audio_volume_now() : cfg_get_scoped_int("audio", "volume", 80);
    int dir;

    ui_next_panel_width(80);
    ui_panel_begin_scroll(menu_scoped_title(title, sizeof(title), "Audio"), 3, 1);
    if (v < 0)
        v = 80;
    snprintf(text, sizeof(text), unset ? "%d%% (plugin)" : "%d%%", v);
    dir = ui_step("Volume", text);
    ui_mark_last(menu_mark_kind(ov));
    if (dir != 0)
    {
        v += dir * 5;
        v = v < 0 ? 0 : v > 100 ? 100 : v;
        cfg_set_int("audio", "volume", v);
        audio_apply_volume();
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
            set_msg("Volume: the audio plugin's own default", 0);
        }
        audio_apply_volume();
    }
    if (ui_button("Back"))
        menu_pop();
    if (l_msg[0])
        ui_status_bar(l_msg, l_msg_kind);
    else if (ov && !scope_game())
        ui_status_bar("This game uses its own volume", 0);
    else
        ui_status_bar(scope_game() ? "Saved for this game only (X: reset)" : "Saved for all games (X: plugin default)", 0);
    ui_panel_end("A Select   Left/Right Change   X Reset   B Back");
    if (back)
        menu_pop();
}

/* ---- the game profile ----------------------------------------------------------------------------------------- */

static void page_profile(int back)
{
    char line[96], title[32];
    int n = cfg_game_override_count();

    ui_panel_begin_scroll(menu_scoped_title(title, sizeof(title), "Game profile"), 6, 1);
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
    }
    if (ui_button("Back"))
        menu_pop();
    if (l_msg[0])
        ui_status_bar(l_msg, l_msg_kind);
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
        remap_reset_game();
        cfg_reset_all_game();
        cfg_save();
        remap_flush();
        fif_reapply_all();                /* foir's live options go back to the global values */
        audio_apply_volume();
        set_msg("This game now uses the global settings", 1);
        menu_pop();
    }
    ui_panel_end("A Select   B Back");
    if (back)
        menu_pop();
}

/* ---- dispatch --------------------------------------------------------------------------------------------------- */

int av_owns_page(menu_page p) { return p >= PAGE_VIDEO; }

void av_frame(menu_page page, int back)
{
    switch (page)
    {
        case PAGE_VIDEO:   page_video(back); break;
        case PAGE_AUDIO:   page_audio(back); break;
        case PAGE_PROFILE: page_profile(back); break;
        case PAGE_CONFIRM_PROFILE_RESET: page_confirm_profile_reset(back); break;
        default: break;
    }
}

void av_leave_page(menu_page p)
{
    if (p >= PAGE_VIDEO)
    {
        cfg_save();
        set_msg("", 0);
    }
}

void av_reset(void)
{
    set_msg("", 0);
}
