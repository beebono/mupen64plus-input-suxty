/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-ui-suxty - menu.c
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

#include <SDL.h>

#include "cfg.h"
#include "menu.h"
#include "menu_int.h"
#include "pad.h"
#include "audio.h"
#include "remap.h"
#include "states.h"
#include "ui.h"

#define MAX_DEPTH 10
static menu_page l_stack[MAX_DEPTH];
static int       l_focus_stack[MAX_DEPTH];     /* the focus of each page below the top one */
static int       l_scroll_stack[MAX_DEPTH];    /* ... and its scroll position */
static int       l_depth;
static char      l_game[32];
static int       l_quit_saves;

/* save-state carousel */
static int        l_sel;                       /* 0-9 numbered slots, 10 = auto */
static int        l_action_slot;
static ui_card    l_cards[STATES_NUM_ENTRIES];
static state_info l_info[STATES_NUM_ENTRIES];
static char       l_msg[64];
static int        l_msg_kind;

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
    push(PAGE_MAIN);
    ui_set_hints_visible(g_cfg.show_hints);
}

static menu_result page_main(void)
{
    menu_result r = MENU_NONE;
    static const char *const scopes[2] = { "All games", "This game" };
    int sc = g_cfg.scope == CFG_SCOPE_GAME;

    ui_panel_begin_scroll("Paused", 10, 0);
    ui_label(l_game[0] ? l_game : " ");
    if (ui_button("Resume"))      r = MENU_RESUME;
    if (ui_button("Save States")) { refresh_cards(); set_msg("", 0); push(PAGE_STATES); }
    if (ui_button("Reset"))       push(PAGE_CONFIRM_RESET);
    if (ui_button("Controls"))    push(PAGE_CONTROLS);
    if (av_video_available() && ui_button("Video"))   push(PAGE_VIDEO);      /* foir only */
    if (av_audio_available() && ui_button("Audio"))   push(PAGE_AUDIO);
    if (ui_button("Settings"))    push(PAGE_SETTINGS);
    /* where the changes made in the Controls / Video / Audio / Settings pages are saved */
    if (ui_choice("Apply to", &sc, scopes, cfg_game_available() ? 2 : 1))
        cfg_set_scope(sc ? CFG_SCOPE_GAME : CFG_SCOPE_GLOBAL);
    if (ui_button("Quit"))        push(PAGE_CONFIRM_QUIT);
    ui_panel_end("A Select   B Resume");
    return r;
}

static menu_result page_states(uint32_t pressed)
{
    menu_result r = MENU_NONE;
    int act;
    int is_auto = (l_sel == STATES_AUTO_SLOT);
    int exists = l_info[l_sel].exists;
    int dir = ((pressed & (PADBIT(PAD_RIGHT) | PADBIT(PAD_R))) ? 1 : 0) - ((pressed & (PADBIT(PAD_LEFT) | PADBIT(PAD_L))) ? 1 : 0);

    if (dir != 0)
    {
        l_sel = (l_sel + dir + STATES_NUM_ENTRIES) % STATES_NUM_ENTRIES;
        is_auto = (l_sel == STATES_AUTO_SLOT);
        exists = l_info[l_sel].exists;
        set_msg("", 0);
    }

    ui_panel_begin_ex("Save States", 13, 90);
    ui_carousel(l_cards, STATES_NUM_ENTRIES, l_sel);
    ui_status(l_msg, l_msg_kind);
    act = ui_button_dis(is_auto ? "Save (auto slot)" : "Save", is_auto);
    if (act < 0)
        set_msg("The auto slot is written by Quit (Settings: Auto-save)", 0);
    else if (act > 0)
    {
        l_action_slot = l_sel;
        if (exists)
            push(PAGE_CONFIRM_OVERWRITE);
        else
            r = MENU_SAVE_STATE;
    }
    /* the auto-save is loaded by the launcher at the next start (--savestate), not by the menu */
    act = ui_button_dis(is_auto ? "Load (by launcher)" : "Load", is_auto || !exists);
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
    ui_panel_end("Left/Right Slot   A Select   B Back");
    return r;
}

/* options for the "choose one" rows: a value table plus how to show it */
typedef struct {
    const char *const *names;
    const int         *vals;
    int                n;
} choice_set;

static const char *const l_ff_speed_names[] = { "150%", "200%", "300%", "400%", "500%", "Unlimited" };
static const int         l_ff_speed_vals[]  = { 150, 200, 300, 400, 500, 0 };
static const choice_set  l_ff_speed = { l_ff_speed_names, l_ff_speed_vals, 6 };
static const char *const l_quit_hold_names[] = { "Instant", "0.5 s", "1 s", "2 s", "3 s" };
static const int         l_quit_hold_vals[]  = { 0, 500, 1000, 2000, 3000 };
static const choice_set  l_quit_hold = { l_quit_hold_names, l_quit_hold_vals, 5 };
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

static void page_settings(void)
{
    int dim = g_cfg.dim_percent;
    int hints = g_cfg.show_hints;
    int toasts = g_cfg.show_toasts;
    int autosave = cfg_get_scoped_bool("states", "autosave_on_quit", 0);
    int swap = g_cfg.swap_ab;
    int ff_mode = strcasecmp(cfg_get_scoped("hotkeys", "fast_forward_mode", "hold"), "toggle") == 0;
    int game = g_cfg.scope == CFG_SCOPE_GAME;
    char title[32];

    ui_panel_begin_scroll(menu_scoped_title(title, sizeof(title), "Settings"), 9, 1);
    if (!game)
    {
        /* the look of the menu itself is not a per-game matter */
        if (ui_slider("Dim", &dim, 0, 90, 5, "%"))
            cfg_set_int("menu", "dim_percent", dim);
        if (ui_toggle("Hints", &hints))
        {
            cfg_set_bool("menu", "show_hints", hints);
            ui_set_hints_visible(g_cfg.show_hints);
        }
        if (ui_toggle("Notices", &toasts))
            cfg_set_bool("menu", "show_toasts", toasts);
        if (ui_toggle("Swap A/B", &swap))
            cfg_set_bool("input", "swap_ab", swap);
    }
    if (ui_toggle("Auto-save on quit", &autosave))
    {
        cfg_set_bool("states", "autosave_on_quit", autosave);
        menu_set_quit_saves(g_cfg.autosave_on_quit);
    }
    settings_row_end("states", "autosave_on_quit");
    if (ui_choice("Fast-forward", &ff_mode, l_ff_mode_names, 2))
        cfg_set("hotkeys", "fast_forward_mode", ff_mode ? "toggle" : "hold");
    settings_row_end("hotkeys", "fast_forward_mode");
    settings_choice("FF speed", &l_ff_speed, "%", "hotkeys", "fast_forward_speed", 300);
    settings_row_end("hotkeys", "fast_forward_speed");
    settings_choice("Quit hold", &l_quit_hold, " ms", "hotkeys", "quit_hold_ms", 1000);
    settings_row_end("hotkeys", "quit_hold_ms");
    if (ui_item("Hotkeys", ">") == 1)
        push(PAGE_HOTKEYS);
    if (ui_item("Game profile", ">") == 1)
        push(PAGE_PROFILE);
    if (ui_button("Back"))
        pop();
    ui_status_bar(game ? "Green bar: saved for this game   X Reset" : "Saved for all games", 0);
    ui_panel_end(game ? "A Select   Left/Right Change   X Reset   B Back" : "A Select   Left/Right Change   B Back");
}

static menu_result page_reset(void)
{
    menu_result r = MENU_NONE;
    ui_panel_begin("Reset", 4);
    ui_label("Reset the game?");
    if (ui_button("No"))                       pop();
    if (ui_button("Soft reset"))               r = MENU_RESET_SOFT;
    if (ui_button("Hard reset (power cycle)")) r = MENU_RESET;
    ui_panel_end("A Select   B Back");
    return r;
}

static menu_result page_confirm(const char *title, const char *question, const char *question2, menu_result yes)
{
    menu_result r = MENU_NONE;
    ui_panel_begin(title, question2 ? 4 : 3);
    ui_label(question);
    if (question2)
        ui_label(question2);
    if (ui_button("No"))  pop();
    if (ui_button("Yes")) r = yes;
    ui_panel_end("A Select   B Back");
    return r;
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
        case PAGE_MAIN:          r = page_main(); if (back && r == MENU_NONE) r = MENU_RESUME; break;
        case PAGE_STATES:
            r = page_states(pressed);
            if (back && r == MENU_NONE && l_stack[l_depth - 1] == PAGE_STATES) pop();
            break;
        case PAGE_CONFIRM_OVERWRITE:
        {
            char q[40];
            snprintf(q, sizeof(q), "Overwrite slot %d?", l_action_slot);
            /* a slot that only has a Project64 state is saved as a Mupen64Plus .st; the .pj file is left alone */
            r = page_confirm("Confirm", q, l_info[l_action_slot].pj64 ? "(Project64 state; saved as .st)" : NULL, MENU_SAVE_STATE);
            if (back) pop();
            break;
        }
        case PAGE_CONFIRM_DELETE:
        {
            char q[40];
            int yes;
            if (l_action_slot != l_sel)
                l_action_slot = l_sel;
            snprintf(q, sizeof(q), l_sel == STATES_AUTO_SLOT ? "Delete the auto-save?" : "Delete slot %d?", l_sel);
            yes = (page_confirm("Confirm", q, l_sel == STATES_AUTO_SLOT ? "(and its backups)" : l_info[l_sel].pj64 ? "(the Project64 state)" : NULL,
                            MENU_RESET) == MENU_RESET);
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
        case PAGE_CONFIRM_RESET: r = page_reset(); if (back) pop(); break;
        case PAGE_CONTROLS: case PAGE_PRESETS: case PAGE_CONFIRM_PRESET: case PAGE_BUTTONS: case PAGE_MODLAYER:
        case PAGE_CAPTURE: case PAGE_TEST: case PAGE_HOTKEYS: case PAGE_COMBO_CAPTURE: case PAGE_COMBO_CONFIRM:
        case PAGE_CONFIRM_CTL_RESET:
            ctl_frame(page, pressed, held, back);
            break;
        case PAGE_VIDEO: case PAGE_AUDIO: case PAGE_PROFILE: case PAGE_CONFIRM_PROFILE_RESET:
            av_frame(page, back);
            break;
        case PAGE_CONFIRM_QUIT:
            r = l_quit_saves ? page_confirm("Confirm", "Quit the game?", "(saves the auto slot first)", MENU_QUIT)
                             : page_confirm("Confirm", "Quit the game?", NULL, MENU_QUIT);
            if (back) pop();
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
