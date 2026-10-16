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

#include <stdio.h>
#include <string.h>

#include "cfg.h"
#include "font.h"
#include "menu.h"
#include "menu_int.h"
#include "optif.h"
#include "pad.h"
#include "ui.h"

static char l_msg[128];
static int  l_msg_kind;

static void set_msg(const char *text, int kind)
{
    snprintf(l_msg, sizeof(l_msg), "%s", text ? text : "");
    l_msg_kind = kind;
}

static int scope_game(void) { return g_cfg.scope == CFG_SCOPE_GAME; }

static int         l_nrow;
static const char *l_focus_help;
static char        l_focus_note[96];

static void option_row(optif_t *op, unsigned i)
{
    const struct optif_option *o = optif_option(op, i);
    int v = optif_shown(op, i), nv = v, changed = 0, over = optif_overridden(op, i), focused;
    char text[48];

    if (o == NULL)
        return;
    switch (o->type)
    {
        case OPT_BOOL:
            if (ui_toggle(o->label, &nv))
                changed = 1;
            break;
        case OPT_ENUM:
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
        int r = optif_set(op, i, nv);
        if (r < 0)
        {
            snprintf(text, sizeof(text), "%s refused the change", op->name);
            set_msg(text, -1);
        }
        else if (r == OPT_NEXT_LAUNCH)
            set_msg("Takes effect the next time the game starts", 1);
        else
            set_msg(scope_game() ? "Applied now; saved for this game" : "Applied now; saved for all games", 1);
    }
    else if (ui_x_last() && scope_game() && over)
    {
        optif_reset(op, i);
        set_msg("Reset to the global value", 1);
    }
    if (focused)
    {
        l_focus_help = o->help;
        l_focus_note[0] = 0;
        if (!(o->flags & OPTF_LIVE))
            snprintf(l_focus_note, sizeof(l_focus_note), optif_pending(op, i) ? "Changed: applies the next time the game starts"
                                                                        : "Read when the game starts (not live)");
        else if (over && !scope_game())
            snprintf(l_focus_note, sizeof(l_focus_note), "This game uses its own value");
    }
}

static void page_options(optif_t *op, const char *name, const char *default_btn, menu_page confirm, int back)
{
    char title[48];
    unsigned i, n = optif_count(op);
    int any_next = 0, rows = 2;

    for (i = 0; i < n; i++)
        rows++;
    for (i = 0; i < n; i++)
        if (!(optif_option(op, i)->flags & OPTF_LIVE) && !any_next)
        {
            any_next = 1;
            rows++;                             /* the "applies at the next launch" heading */
        }
    any_next = 0;
    l_nrow = 0;
    l_focus_help = NULL;
    l_focus_note[0] = 0;
    ui_next_panel_width(UI_W_FULL);
    ui_panel_begin_scroll(menu_scoped_title(title, sizeof(title), name), rows, 1);
    for (i = 0; i < n; i++)
        if (optif_option(op, i)->flags & OPTF_LIVE)
            option_row(op, i);
    for (i = 0; i < n; i++)
        if (!(optif_option(op, i)->flags & OPTF_LIVE))
        {
            if (!any_next)
                ui_label(FONT_CH_DOWN " Applies at the Next Launch " FONT_CH_DOWN);
            any_next = 1;
            option_row(op, i);
        }
    if (ui_button(default_btn))
        menu_push(confirm);
    l_nrow++;
    if (ui_button("Back"))
        menu_pop();
    l_nrow++;

    if (l_msg[0])
        ui_status_bar(l_msg, l_msg_kind);
    else if (ui_focus_mark() == 2)
        ui_status_bar(menu_scope_status(), 0);
    else if (l_focus_note[0])
        ui_status_bar(l_focus_note, 0);
    else if (l_focus_help != NULL)
        ui_status_bar(l_focus_help, 0);
    else
        ui_status_bar(menu_scope_status(), 0);
    ui_panel_end(scope_game() ? "A Change   X Reset   B Back" : "A Change   B Back");
    if (back)
        menu_pop();
}

static void page_confirm_default(optif_t *op, const char *section, int back)
{
    ui_panel_begin_ex("Confirm", 6, UI_W_DIALOG);
    ui_grid(2);
    ui_label("Write these settings to");
    ui_label("mupen64plus.cfg?");
    if (menu_yes_no())
    {
        if (optif_save_defaults(op) == 0)
        {
            char t[64];
            snprintf(t, sizeof(t), "Written to [%s] in mupen64plus.cfg", section);
            set_msg(t, 1);
        }
        else
            set_msg("Couldn't write mupen64plus.cfg", -1);
        menu_pop();
    }
    ui_panel_end("A Select   B Back");
    if (back)
        menu_pop();
}

int av_owns_page(menu_page p) { return p >= PAGE_VIDEO; }

void av_frame(menu_page page, int back)
{
    switch (page)
    {
        case PAGE_VIDEO:                page_options(&g_foir, "Video Options", "Save as Video Default", PAGE_CONFIRM_VIDEO_DEFAULT, back); break;
        case PAGE_CONFIRM_VIDEO_DEFAULT: page_confirm_default(&g_foir, "Video-Foir", back); break;
        case PAGE_AUDIO:                page_options(&g_altru, "Audio Options", "Save as Audio Default", PAGE_CONFIRM_AUDIO_DEFAULT, back); break;
        case PAGE_CONFIRM_AUDIO_DEFAULT: page_confirm_default(&g_altru, "Audio-Altru", back); break;
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
