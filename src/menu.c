/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - menu.c
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


#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <SDL.h>

#include "audio.h"
#include "cfg.h"
#include "foir_if.h"
#include "menu.h"
#include "menu_int.h"
#include "pad.h"
#include "remap.h"
#include "states.h"
#include "toast.h"
#include "ui.h"

#define MAX_DEPTH 10
static menu_page l_stack[MAX_DEPTH];
static int       l_focus_stack[MAX_DEPTH];     /* the focus of each page below the top one */
static int       l_scroll_stack[MAX_DEPTH];    /* ... and its scroll position */
static int       l_depth;
static char      l_game[32];
static int       l_quit_saves;

/* the slot the Save / Load / In Slot controls act on, and the cards of the Save States carousel */
static int        l_sel;                       /* 0-9 numbered slots, 10 = auto */
static int        l_action_slot;
static ui_card    l_cards[STATES_NUM_ENTRIES];
static state_info l_info[STATES_NUM_ENTRIES];
static char       l_msg[64];                   /* the main page's and the Save States page's message line */
static int        l_msg_kind;
static int        l_msg_focus;                 /* the focus it was set at: it goes when the focus moves */
static char       l_set_msg[96];               /* the Settings page's status line */
static int        l_set_msg_kind;
static int        l_set_msg_focus;

#define MAIN_FOCUS_RESUME 3                    /* the main page opens on Resume */

void menu_set_game_name(const char *name)
{
    size_t i, n = 0;
    /* ROM header names are space-padded, sometimes with Shift-JIS bytes: keep printable ASCII only */
    for (i = 0; name != NULL && name[i] && n < sizeof(l_game) - 1; i++)
        if ((unsigned char) name[i] >= 32 && (unsigned char) name[i] < 127)
            l_game[n++] = name[i];
    while (n > 0 && l_game[n - 1] == ' ')
        n--;
    l_game[n] = 0;
}

void menu_set_quit_saves(int saves) { l_quit_saves = saves; }
int  menu_action_slot(void)         { return l_action_slot; }
int  menu_selected_slot(void)       { return l_sel >= STATES_NUM_SLOTS ? -1 : l_sel; }

/* a new page starts with the first row focused (confirm pages list "No" first, so it is the default); going
 * back returns to the row you came from */
void menu_push(menu_page p)
{
    if (l_depth < MAX_DEPTH)
    {
        l_focus_stack[l_depth - 1 < 0 ? 0 : l_depth - 1] = ui_focus_get();
        l_scroll_stack[l_depth - 1 < 0 ? 0 : l_depth - 1] = ui_scroll_get();
        l_stack[l_depth++] = p;
    }
    ui_focus_set(0);
}

void menu_pop(void)
{
    if (l_depth > 1)
    {
        l_depth--;
        ui_focus_restore(l_focus_stack[l_depth - 1], l_scroll_stack[l_depth - 1]);
    }
    else
        ui_focus_set(0);
}

static void push(menu_page p) { menu_push(p); }
static void pop(void)         { menu_pop(); }

static void set_msg(const char *msg, int kind)
{
    snprintf(l_msg, sizeof(l_msg), "%s", msg ? msg : "");
    l_msg_kind = kind;
    l_msg_focus = ui_focus_get();
}

static void set_settings_msg(const char *msg, int kind)
{
    snprintf(l_set_msg, sizeof(l_set_msg), "%s", msg ? msg : "");
    l_set_msg_kind = kind;
    l_set_msg_focus = ui_focus_get();
}

/* A confirm dialog's "No" and "Yes", side by side: Yes returns 1, No goes back. */
int menu_yes_no(void)
{
    int no, yes;
    ui_row_begin(2);
    no = ui_button("No");
    yes = ui_button("Yes");
    if (no)
        pop();
    return yes;
}

static void refresh_cards(void)
{
    int i;
    for (i = 0; i < STATES_NUM_ENTRIES; i++)
    {
        ui_card *c = &l_cards[i];
        states_stat(i, &l_info[i]);
        memset(c, 0, sizeof(*c));
        if (i == STATES_AUTO_SLOT)
            snprintf(c->title, sizeof(c->title), "Auto");
        else
            snprintf(c->title, sizeof(c->title), "Slot %d", i);
        if (l_info[i].exists)
        {
            states_format_time(l_info[i].mtime, c->l1, sizeof(c->l1), c->l2, sizeof(c->l2));
            if (l_info[i].pj64)
                snprintf(c->tag, sizeof(c->tag), "PJ64");
        }
        else
        {
            snprintf(c->l1, sizeof(c->l1), "Empty");
            c->empty = 1;
        }
    }
}

void menu_open(int current_slot)
{
    l_depth = 0;
    ctl_reset();
    av_reset();
    l_sel = current_slot >= 0 && current_slot < STATES_NUM_SLOTS ? current_slot : 0;
    set_msg("", 0);
    set_settings_msg("", 0);
    refresh_cards();
    push(PAGE_MAIN);
    ui_focus_set(MAIN_FOCUS_RESUME);
}

/* ---- the scope: titles, markers, what the status line says ----------------------------------------- */

const char *menu_scoped_title(char *buf, size_t size, const char *base)
{
    snprintf(buf, size, "%s%s", base, g_cfg.scope == CFG_SCOPE_GAME ? " (Game)" : "");
    return buf;
}

int menu_mark_kind(int overridden)
{
    return overridden ? (g_cfg.scope == CFG_SCOPE_GAME ? 1 : 2) : 0;
}

/* the one place the markers are explained: when a marked row has the focus */
const char *menu_scope_status(void)
{
    switch (ui_focus_mark())
    {
        case 1:  return "Green bar: saved for this game";
        case 2:  return "Olive bar: this game has its own";
        default: return g_cfg.scope == CFG_SCOPE_GAME ? "Saved for this game only" : "Saved for all games";
    }
}

/* ---- main ---------------------------------------------------------------------------------------------- */

/* "Empty", or "10-09 18:42" (the year is in the carousel) */
static void slot_info(int slot, char *out, size_t size)
{
    const ui_card *c = &l_cards[slot];
    if (c->empty)
        snprintf(out, size, "Empty");
    else
        snprintf(out, size, "%.10s %.5s", strlen(c->l1) == 10 ? c->l1 + 5 : c->l1, c->l2);
}

static menu_result page_main(void)
{
    menu_result r = MENU_NONE;
    int is_auto = (l_sel == STATES_AUTO_SLOT), exists = l_info[l_sel].exists;
    int act, dir = 0;
    char label[24], info[24];

    if (l_msg[0] && ui_focus_get() != l_msg_focus)
        set_msg("", 0);
    ui_panel_begin_ex(l_game[0] ? l_game : "Menu", 8, UI_W_WIDE);
    ui_grid(2);
    ui_row_begin(2);
    act = ui_button_dis("Save", is_auto);
    if (act < 0)
        set_msg("The auto slot is written by Quit", 0);
    else if (act > 0)
    {
        l_action_slot = l_sel;
        if (exists)
            push(PAGE_CONFIRM_OVERWRITE);
        else
            r = MENU_SAVE_STATE;
    }
    act = ui_button_dis("Load", is_auto || !exists);
    if (act < 0)
        set_msg(is_auto ? "The launcher loads the auto-save" : "Nothing saved here yet", 0);
    else if (act > 0)
    {
        l_action_slot = l_sel;
        r = MENU_LOAD_STATE;
    }
    if (is_auto)
        snprintf(label, sizeof(label), "In Slot Auto");
    else
        snprintf(label, sizeof(label), "In Slot %d", l_sel);
    slot_info(l_sel, info, sizeof(info));
    if (ui_stepper(label, info, &dir))
    {
        set_msg("", 0);
        push(PAGE_STATES);
        ui_focus_set(1);                            /* the carousel is the first row: start on Save */
    }
    else if (dir != 0)
    {
        l_sel = (l_sel + dir + STATES_NUM_ENTRIES) % STATES_NUM_ENTRIES;
        set_msg("", 0);
    }
    ui_row_begin(2);
    if (ui_button("Resume"))   r = MENU_RESUME;
    if (ui_button("Reset"))    push(PAGE_CONFIRM_RESET);
    ui_row_begin(2);
    if (ui_button("Settings")) push(PAGE_SETTINGS);
    if (ui_button("Quit"))     push(PAGE_CONFIRM_QUIT);
    ui_panel_end_k(l_msg[0] ? l_msg : "A Select   B Resume", l_msg_kind);
    return r;
}

/* ---- Save States ------------------------------------------------------------------------------------------ */

static menu_result page_states(uint32_t pressed)
{
    menu_result r = MENU_NONE;
    int act, cdir;
    int is_auto, exists;
    int dir = ((pressed & PADBIT(PAD_R)) ? 1 : 0) - ((pressed & PADBIT(PAD_L)) ? 1 : 0);

    if (dir != 0)
    {
        l_sel = (l_sel + dir + STATES_NUM_ENTRIES) % STATES_NUM_ENTRIES;
        set_msg("", 0);
    }
    ui_panel_begin_ex("Save States", 9, UI_W_FULL);
    ui_grid(3);
    cdir = ui_carousel(l_cards, STATES_NUM_ENTRIES, l_sel);
    if (cdir != 0)
    {
        l_sel = (l_sel + cdir + STATES_NUM_ENTRIES) % STATES_NUM_ENTRIES;
        set_msg("", 0);
    }
    is_auto = (l_sel == STATES_AUTO_SLOT);
    exists = l_info[l_sel].exists;
    ui_status(l_msg, l_msg_kind);
    ui_row_begin(3);
    act = ui_button_dis("Save", is_auto);
    if (act < 0)
        set_msg("The auto slot is written by Quit", 0);
    else if (act > 0)
    {
        l_action_slot = l_sel;
        if (exists)
            push(PAGE_CONFIRM_OVERWRITE);
        else
            r = MENU_SAVE_STATE;
    }
    /* the auto-save is loaded by the launcher at the next start (--savestate), not by the menu */
    act = ui_button_dis("Load", is_auto || !exists);
    if (act < 0)
        set_msg(is_auto ? "The auto-save is loaded by your launcher" : "Nothing saved here yet", 0);
    else if (act > 0)
    {
        l_action_slot = l_sel;
        r = MENU_LOAD_STATE;
    }
    act = ui_button_dis("Delete", !exists);
    if (act < 0)
        set_msg("Nothing saved here yet", 0);
    else if (act > 0)
        push(PAGE_CONFIRM_DELETE);
    ui_panel_end("L/R Slot   A Select   B Back");
    return r;
}

/* ---- Settings ---------------------------------------------------------------------------------------------- */

/* options for the "choose one" rows: a value table plus how to show it */
typedef struct {
    const char *const *names;
    const int         *vals;
    int                n;
} choice_set;

static const char *const l_ff_speed_names[] = { "150%", "200%", "300%", "400%", "500%", "Unlimited" };
static const int         l_ff_speed_vals[]  = { 150, 200, 300, 400, 500, 0 };
static const choice_set  l_ff_speed = { l_ff_speed_names, l_ff_speed_vals, 6 };
static const char *const l_ff_mode_names[] = { "Hold", "Toggle" };

/* A row choosing one of `set`'s values for ini [section] key, as the edit scope sees it.  A value that is not in the
 * table (edited by hand in suxty.cfg) is shown as it is, as an extra last option, until the user picks another one.
 * Returns 1 on change. */
static int settings_choice(const char *label, const choice_set *set, const char *suffix, const char *section, const char *key, int def)
{
    const char *names[8];
    char custom[24];
    int cur = cfg_get_scoped_int(section, key, def);
    int idx = -1, i, n = set->n, changed;

    for (i = 0; i < n; i++)
    {
        names[i] = set->names[i];
        if (set->vals[i] == cur)
            idx = i;
    }
    if (idx < 0)
    {
        snprintf(custom, sizeof(custom), "%d%s", cur, suffix);
        names[n] = custom;
        idx = n;
        n++;
    }
    changed = ui_choice(label, &idx, names, n);
    if (changed && idx < set->n)
        cfg_set_int(section, key, set->vals[idx]);
    else if (changed)
        changed = 0;
    return changed;
}

/* after a settings row: the "this game has its own value" marker, and X = reset to global */
static void settings_row_end(const char *section, const char *key)
{
    int over = cfg_overridden(section, key);
    ui_mark_last(menu_mark_kind(over));
    if (g_cfg.scope == CFG_SCOPE_GAME && over && ui_x_last())
        cfg_reset(section, key);
}

/* the Volume row: shown only when the core can report the volume; X goes back to the audio plugin's own default (all games) or to
 * the global volume (this game) */
static void settings_volume(void)
{
    int ov = cfg_overridden("audio", "volume");
    int unset = !cfg_get_scoped("audio", "volume", "")[0];
    int v = unset ? audio_volume_now() : cfg_get_scoped_int("audio", "volume", 80);
    int game = g_cfg.scope == CFG_SCOPE_GAME, dir;
    char text[24];

    if (v < 0)
        v = 80;
    snprintf(text, sizeof(text), "%d%%", v);
    dir = ui_step("Volume", text);
    ui_mark_last(menu_mark_kind(ov));
    if (dir != 0)
    {
        v += dir * 5;
        v = v < 0 ? 0 : v > 100 ? 100 : v;
        cfg_set_int("audio", "volume", v);
        audio_apply_volume();
    }
    else if (ui_x_last())
    {
        if (game && ov)
        {
            cfg_reset("audio", "volume");
            set_settings_msg("Volume: back to the global one", 1);
        }
        else if (!game)
        {
            ini_remove(g_cfg.ini, "audio", "volume");
            cfg_apply();
            set_settings_msg("Volume: audio plugin default", 0);
        }
        audio_apply_volume();
    }
}

/* "Save Game Profile": every per-game-capable setting in force becomes this game's own */
static void save_game_profile(void)
{
    char t[64];
    cfg_pin_profile();
    remap_pin_game();
    fif_pin_game();
    cfg_save();
    remap_flush();
    snprintf(t, sizeof(t), "%d settings saved for this game", cfg_game_setting_count());
    set_settings_msg(t, 1);
    toast_show(TOAST_OK, "Game profile saved");
}

static void page_settings(void)
{
    int dim = g_cfg.dim_percent;
    int toasts = g_cfg.show_toasts;
    int autosave = cfg_get_scoped_bool("states", "autosave_on_quit", 0);
    int confirm = cfg_get_scoped_bool("hotkeys", "confirm_quit", 1);
    int ff_mode = strcasecmp(cfg_get_scoped("hotkeys", "fast_forward_mode", "hold"), "toggle") == 0;
    int game = g_cfg.scope == CFG_SCOPE_GAME;
    int foir = av_foir_available(), vol = audio_volume_now() >= 0;
    int nset = cfg_game_setting_count(), have_game = cfg_game_available();
    int rows, id = 0, dim_id, apply_id;
    char title[32];
    static const char *const scopes[2] = { "All Games", "This Game" };
    int sc = game;

    if (l_set_msg[0] && ui_focus_get() != l_set_msg_focus)
        set_settings_msg("", 0);                  /* a message belongs to the row it was made on */
    /* the same rows, in the same order, in both scopes (the cursor never moves under you when "Apply To" changes) */
    rows = 1 + foir + 1 + vol + 4 + 2 + 1 + 1 + (nset > 0 ? 1 : 0) + 1;
    ui_panel_begin_scroll(menu_scoped_title(title, sizeof(title), "Settings"), rows, 1);
    if (ui_item("Controls", ">") == 1)
        push(PAGE_CONTROLS);
    id++;
    if (foir)
    {
        if (ui_item("FOIR Options", ">") == 1)
            push(PAGE_FOIR);
        id++;
    }
    if (ui_item("Hotkeys", ">") == 1)
        push(PAGE_HOTKEYS);
    id++;
    if (vol)
    {
        settings_volume();
        id++;
    }
    if (ui_toggle("Auto-Save", &autosave))
    {
        cfg_set_bool("states", "autosave_on_quit", autosave);
        menu_set_quit_saves(g_cfg.autosave_on_quit);
    }
    settings_row_end("states", "autosave_on_quit");
    if (ui_toggle("Confirm Quit", &confirm))
        cfg_set_bool("hotkeys", "confirm_quit", confirm);
    settings_row_end("hotkeys", "confirm_quit");
    if (ui_choice("Fast-Forward", &ff_mode, l_ff_mode_names, 2))
        cfg_set("hotkeys", "fast_forward_mode", ff_mode ? "toggle" : "hold");
    settings_row_end("hotkeys", "fast_forward_mode");
    settings_choice("FF Speed", &l_ff_speed, "%", "hotkeys", "fast_forward_speed", 300);
    settings_row_end("hotkeys", "fast_forward_speed");
    id += 4;
    dim_id = id;
    if (game)
    {
        /* the look of the menu itself is not a per-game matter: the global values, in place, dimmed */
        ui_slider_locked("Dim", dim, "%");
        ui_toggle_locked("Notices", toasts);
    }
    else
    {
        if (ui_slider("Dim", &dim, 0, 90, 5, "%"))
            cfg_set_int("menu", "dim_percent", dim);
        if (ui_toggle("Notices", &toasts))
            cfg_set_bool("menu", "show_toasts", toasts);
    }
    id += 2;
    apply_id = id++;
    /* where the changes made in the Controls / FOIR Options / Settings pages are saved */
    if (ui_choice("Apply To", &sc, scopes, have_game ? 2 : 1))
    {
        cfg_set_scope(sc ? CFG_SCOPE_GAME : CFG_SCOPE_GLOBAL);
        set_settings_msg("", 0);
    }
    if (ui_button_dis("Save Game Profile", !have_game) > 0)
        save_game_profile();
    id++;
    if (nset > 0 && ui_button("Reset Game Profile"))
        push(PAGE_CONFIRM_PROFILE_RESET);
    if (ui_button("Back"))
        pop();

    if (l_set_msg[0])
        ui_status_bar(l_set_msg, l_set_msg_kind);
    else if (game && (ui_focus_get() == dim_id || ui_focus_get() == dim_id + 1))
        ui_status_bar("Global Only", 0);
    else if (ui_focus_get() == apply_id)
        ui_status_bar("Where Controls/FOIR/Settings save", 0);
    else if (ui_focus_get() == apply_id + 1)
    {
        char t[64];
        if (!have_game)
            snprintf(t, sizeof(t), "This ROM has no ID: no game settings");
        else if (nset > 0)
            snprintf(t, sizeof(t), "%d setting%s saved for this game", nset, nset == 1 ? "" : "s");
        else
            snprintf(t, sizeof(t), "Keeps these settings for this game");
        ui_status_bar(t, 0);
    }
    else
        ui_status_bar(menu_scope_status(), 0);
    ui_panel_end(game ? "A Select  <> Change  X Reset  B Back" : "A Select  <> Change  B Back");
}

static void page_confirm_profile_reset(void)
{
    ui_panel_begin_ex("Confirm", 6, UI_W_DIALOG);
    ui_label("Forget this game's");
    ui_label("own settings?");
    if (menu_yes_no())
    {
        remap_reset_game();
        cfg_reset_all_game();
        cfg_save();
        remap_flush();
        fif_reapply_all();                /* foir's live options go back to the global values */
        audio_apply_volume();
        pop();
        set_settings_msg("This game uses the global settings", 1);      /* after the pop: the message belongs to the row we land on */
    }
    ui_panel_end("A Select   B Back");
}

/* ---- Reset, Quit and the Save States dialogs --------------------------------------------------------------- */

static menu_result page_reset(void)
{
    menu_result r = MENU_NONE;
    ui_panel_begin_ex("Reset", 6, UI_W_DIALOG);
    ui_grid(2);
    ui_label("Reset the game?");
    ui_row_begin(2);
    if (ui_button("No"))                       pop();
    if (ui_button("Yes"))                      r = MENU_RESET_SOFT;
    if (ui_button_center("Hard (Power Cycle)")) r = MENU_RESET;
    ui_panel_end("A Select   B Back");
    return r;
}

/* question, an optional note under it, No / Yes: 1 on Yes */
static int page_confirm(const char *title, const char *question, const char *note)
{
    int yes;
    ui_panel_begin_ex(title, note ? 6 : 4, UI_W_DIALOG);
    ui_grid(2);
    ui_label(question);
    if (note)
        ui_label(note);
    yes = menu_yes_no();
    ui_panel_end("A Select   B Back");
    return yes;
}

menu_result menu_frame(int w, int h, uint32_t pressed, uint32_t held)
{
    menu_result r = MENU_NONE;
    menu_page page = l_stack[l_depth - 1];

    /* B goes back one page; on the top page it resumes the game */
    int back = (pressed & PADBIT(PAD_B)) != 0;

    ui_begin(w, h, pressed);
    switch (page)
    {
        case PAGE_MAIN:          r = page_main(); if (back && r == MENU_NONE && l_stack[l_depth - 1] == PAGE_MAIN) r = MENU_RESUME; break;
        case PAGE_STATES:
            r = page_states(pressed);
            if (back && r == MENU_NONE && l_stack[l_depth - 1] == PAGE_STATES) pop();
            break;
        case PAGE_CONFIRM_OVERWRITE:
        {
            char q[40];
            snprintf(q, sizeof(q), "Overwrite slot %d?", l_action_slot);
            /* a slot that only has a Project64 state is saved as a Mupen64Plus .st; the .pj file is left alone */
            if (page_confirm("Confirm", q, l_info[l_action_slot].pj64 ? "(PJ64 state)" : NULL))
                r = MENU_SAVE_STATE;
            else if (back)
                pop();
            break;
        }
        case PAGE_CONFIRM_DELETE:
        {
            char q[40];
            int yes;
            snprintf(q, sizeof(q), l_sel == STATES_AUTO_SLOT ? "Delete the auto-save?" : "Delete slot %d?", l_sel);
            yes = page_confirm("Confirm", q, l_sel == STATES_AUTO_SLOT ? "(and its backups)" : l_info[l_sel].pj64 ? "(PJ64 state)" : NULL);
            if (yes)
            {
                if (states_delete(l_sel) == 0)
                {
                    char m[40];
                    snprintf(m, sizeof(m), l_sel == STATES_AUTO_SLOT ? "Deleted the auto-save" : "Deleted slot %d", l_sel);
                    set_msg(m, 1);
                }
                else
                    set_msg("Couldn't delete the state file", -1);
                refresh_cards();
                pop();
            }
            else if (back)
                pop();
            break;
        }
        case PAGE_SETTINGS:      page_settings(); if (back) { pop(); cfg_save(); } break;
        case PAGE_CONFIRM_PROFILE_RESET: page_confirm_profile_reset(); if (back) pop(); break;
        case PAGE_CONFIRM_RESET: r = page_reset(); if (back) pop(); break;
        case PAGE_CONTROLS: case PAGE_CONFIRM_MAPRESET: case PAGE_BUTTONS:
        case PAGE_CAPTURE: case PAGE_TEST: case PAGE_HOTKEYS: case PAGE_COMBO_CAPTURE: case PAGE_COMBO_CONFIRM:
        case PAGE_CONFIRM_CTL_RESET:
            ctl_frame(page, pressed, held, back);
            break;
        case PAGE_FOIR: case PAGE_CONFIRM_FOIR_DEFAULT:
            av_frame(page, back);
            break;
        case PAGE_CONFIRM_QUIT:
            if (page_confirm("Quit", "Quit the game?", l_quit_saves ? "(Auto-Save Enabled)" : NULL))
                r = MENU_QUIT;
            else if (back)
                pop();
            break;
    }
    ui_end();
    if (l_stack[l_depth - 1] != page)
    {
        if (page == PAGE_SETTINGS)
            cfg_save();
        ctl_leave_page(page);
        av_leave_page(page);
    }
    return r;
}

int menu_combo_pressed(uint32_t held)  { return ctl_combo_pressed(held); }
void menu_closing(void)                { ctl_menu_closing(); remap_flush(); cfg_save(); }
