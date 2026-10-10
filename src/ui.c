/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - ui.c
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

#include "font.h"
#include "gl.h"
#include "pad.h"
#include "toast.h"
#include "ui.h"
#include "uinav.h"
#include "uitext.h"

#define COL_PANEL      UGL_RGBA(14, 18, 28, 236)
#define COL_BORDER     UGL_RGBA(70, 130, 230, 255)
#define COL_TITLE      UGL_RGBA(120, 180, 255, 255)
#define COL_TEXT       UGL_RGBA(205, 210, 220, 255)
#define COL_TEXT_FOCUS UGL_RGBA(255, 255, 255, 255)
#define COL_FOCUS_BG   UGL_RGBA(42, 100, 210, 255)
#define COL_CELL_BG    UGL_RGBA(30, 38, 56, 255)
#define COL_HINT       UGL_RGBA(140, 150, 168, 255)
#define COL_VALUE      UGL_RGBA(255, 214, 102, 255)
#define COL_DIS_TEXT   UGL_RGBA(140, 148, 166, 255)
#define COL_DIS_FOCUS  UGL_RGBA(48, 60, 88, 255)
#define COL_DIS_FOCUS_TEXT UGL_RGBA(170, 178, 194, 255)
#define COL_GHOST      UGL_RGBA(74, 84, 106, 255)

#define UI_MAX_IDS 64

static void fit_text(char *buf, size_t size, const char *src, ugl_font font, int scale, int avail);

static ui_metrics l_m;
static int l_focus;
static int l_want_col;
static int l_want_sync;        /* the focus was set by the page logic: the remembered column is that cell's column */
static int l_table_n;          /* focusables of the last finished frame; 0 = unknown (a page was just chosen) */
static int l_nfocus;
static int l_activate;
static int l_adjust;
static int l_yoff;
static uint32_t l_pressed;

static int l_bx0, l_bx1, l_tx0, l_tx1;
static int l_gridcols = 1;
static int l_rowcols, l_rowcell, l_cur_y, l_cur_row;

static int l_scroll_on;
static int l_row;
static int l_last_row;
static int l_vis;
static int l_top;
static int l_rows_total;
static ui_navcell l_cells[UI_MAX_IDS];
static int l_status_y = -1;
static int l_focus_reset;      /* the focus was set during this frame (page change): ui_end() must not trust this page's count */
static int l_restored;         /* focus and scroll position were just restored (going back): trust them for one frame */
static int l_focus_mark;
static char l_notice[96];
static int  l_notice_on, l_notice_kind;
static int  l_has_status;

static void metrics_set(ui_metrics *m, int body, int item)
{
    m->body = body;
    m->item = item;
    m->pad = 4 * body;
    m->row_h = FONT_GLYPH_H * item + 2 * m->pad;
    /* hints are 1.5x the old 8x16 size where that is a whole scale of the crisper 12x24 face; above that the 8x16
     * face at `body`
     * is already about that size on the high-density screens (1024x768 on 4"), and 12x24 at 2x would be as large as
     * the items */
    if (body == 1)
    {
        m->hint_font = UGL_FONT_HINT;
        m->hint_scale = 1;
    }
    else
    {
        m->hint_font = UGL_FONT_ITEM;
        m->hint_scale = body;
    }
    m->hint_h = ugl_glyph_h(m->hint_font) * m->hint_scale;
}

void ui_metrics_for(ui_metrics *m, int w, int h)
{
    int short_side = h < w ? h : w;
    m->w = w;
    m->h = h;
    metrics_set(m, short_side < 600 ? 1 : short_side < 1000 ? 2 : 3, 0);
    metrics_set(m, m->body, m->body + 1);
}

/* the focus changed page: the table of the page about to be drawn is not known yet */
void ui_focus_set(int index)           { l_focus = index; l_want_col = 0; l_want_sync = 1; l_top = 0; l_table_n = 0; l_restored = 0; l_focus_reset = 1; }
int  ui_scroll_get(void)               { return l_top; }
void ui_focus_restore(int index, int top) { l_focus = index; l_want_sync = 1; l_top = top; l_table_n = 0; l_restored = 1; l_focus_reset = 1; }
int  ui_focus_get(void)                { return l_focus; }

void ui_begin(int w, int h, uint32_t pressed)
{
    ui_metrics_for(&l_m, w, h);
    l_pressed = pressed;
    l_activate = (pressed & PADBIT(PAD_A)) != 0;
    l_adjust = ((pressed & PADBIT(PAD_RIGHT)) ? 1 : 0) - ((pressed & PADBIT(PAD_LEFT)) ? 1 : 0);
    if (l_table_n > 0)
    {
        int moved;
        if (l_want_sync && l_focus < l_table_n)
        {
            l_want_col = l_cells[l_focus].span == 1 ? l_cells[l_focus].col : 0;
            l_want_sync = 0;
        }
        if (pressed & PADBIT(PAD_UP))
            l_focus = uinav_move(l_cells, l_table_n, l_focus, UI_NAV_UP, &l_want_col);
        if (pressed & PADBIT(PAD_DOWN))
            l_focus = uinav_move(l_cells, l_table_n, l_focus, UI_NAV_DOWN, &l_want_col);
        /* Left / Right move between the cells of a row; a row of one cell keeps them for its own widget */
        if (l_adjust != 0)
        {
            moved = uinav_move(l_cells, l_table_n, l_focus, l_adjust > 0 ? UI_NAV_RIGHT : UI_NAV_LEFT, &l_want_col);
            if (moved != l_focus)
            {
                l_focus = moved;
                l_adjust = 0;
            }
        }
        if (l_focus >= l_table_n)
            l_focus = l_table_n - 1;
    }
    l_nfocus = 0;
    l_yoff = 0;
    l_row = 0;
    l_scroll_on = 0;
    l_status_y = -1;
    l_gridcols = 1;
    l_rowcols = 0;
    l_focus_mark = 0;
    l_has_status = 0;
    l_notice_on = toast_menu_notice(l_notice, sizeof(l_notice), &l_notice_kind);
}

void ui_end(void)
{
    if (l_focus_reset)
        l_focus_reset = 0;                 /* a new page was chosen this frame: its count is not known yet */
    else
        l_table_n = l_nfocus < UI_MAX_IDS ? l_nfocus : UI_MAX_IDS;
}

/* always both lines (a long message wraps into the second), so the panel never changes size */
static int status_area_h(const ui_metrics *m)
{
    return 2 * m->hint_h + m->pad;
}

static int l_footer = 1;       /* the panel being built has a footer line (ui_next_panel_no_footer() clears it until ui_panel_end()) */

void ui_next_panel_no_footer(void) { l_footer = 0; }

static int panel_height_for(const ui_metrics *m, int rows2, int with_status)
{
    int title_h = FONT_GLYPH_H * m->item + 2 * m->pad;
    int hint_h = l_footer ? m->hint_h + 2 * m->pad : 0;
    return title_h + rows2 * m->row_h / 2 + (with_status ? status_area_h(m) : 0) + hint_h + 2 * m->pad;
}

/* panel height keeps the h/24 vertical margin that toasts and the main menu keep; more rows scroll */
static int panel_max_h(const ui_metrics *m)
{
    return m->h - 2 * (m->h / 24);
}

void ui_panel_begin(const char *title, int rows)
{
    ui_panel_begin_ex(title, rows * 2, UI_W_DIALOG);
}

static void panel_begin(const char *title, int rows2, int with_status, int width_chars);

/* rows2: body height in half rows; width_chars: of item text */
void ui_panel_begin_ex(const char *title, int rows2, int width_chars)
{
    panel_begin(title, rows2, 0, width_chars);
}

static void panel_begin(const char *title, int rows2, int with_status, int width_chars)
{
    ui_metrics *m = &l_m;
    int title_h, line, rows = rows2, bar_gap;
    char buf[96];

    while (m->item > 1 && panel_height_for(m, rows, with_status) > panel_max_h(m))
        metrics_set(m, m->body > m->item - 1 ? m->item - 1 : m->body, m->item - 1);

    title_h = FONT_GLYPH_H * m->item + 2 * m->pad;
    line = m->body;

    /* the gutter for the per-game marker and the right margin come on top of the characters asked for */
    bar_gap = l_scroll_on ? 2 * m->pad : m->pad;
    m->panel_w = width_chars * FONT_GLYPH_W * m->item + 8 * m->pad;
    if (m->panel_w > m->w - 4 * m->pad) m->panel_w = m->w - 4 * m->pad;
    m->panel_h = panel_height_for(m, rows, with_status);
    if (m->panel_h > panel_max_h(m)) m->panel_h = panel_max_h(m);
    m->panel_x = (m->w - m->panel_w) / 2;
    m->panel_y = (m->h - m->panel_h) / 2;

    l_bx0 = m->panel_x + m->pad;
    l_bx1 = m->panel_x + m->panel_w - bar_gap;
    l_tx0 = l_bx0 + 3 * m->pad;
    l_tx1 = l_bx1 - 2 * m->pad;
    l_status_y = -1;
    l_gridcols = 1;
    l_rowcols = 0;
    ugl_rect(m->panel_x - line, m->panel_y - line, m->panel_w + 2 * line, m->panel_h + 2 * line, COL_BORDER);
    ugl_rect(m->panel_x, m->panel_y, m->panel_w, m->panel_h, COL_PANEL);

    fit_text(buf, sizeof(buf), title, UGL_FONT_ITEM, m->item, m->panel_w - 4 * m->pad);
    ugl_text(m->panel_x + (m->panel_w - ugl_text_width(m->item, buf)) / 2, m->panel_y + m->pad + m->pad / 2,
             m->item, COL_TITLE, buf);
    ugl_rect(m->panel_x + m->pad, m->panel_y + title_h + m->pad / 2, m->panel_w - 2 * m->pad, line, COL_BORDER);
    l_yoff = 0;
}

void ui_panel_end(const char *hint)
{
    ui_panel_end_k(hint, 0);
}

void ui_panel_end_k(const char *hint, int kind)
{
    ui_metrics *m = &l_m;
    if (l_scroll_on)
    {
        l_rows_total = l_row;
        if (l_row > l_vis)
        {
            int title_h = FONT_GLYPH_H * m->item + 2 * m->pad;
            int x = m->panel_x + m->panel_w - m->pad * 3 / 2, w = m->pad;
            int y0 = m->panel_y + title_h + m->pad, h = l_vis * m->row_h - m->pad / 2;
            int th = h * l_vis / l_row, ty;
            if (th < 2 * m->pad)
                th = 2 * m->pad;
            ty = y0 + (h - th) * l_top / (l_row - l_vis);
            ugl_rect(x, y0, w, h, COL_CELL_BG);
            ugl_rect(x, ty, w, th, COL_HINT);
        }
    }
    if (l_notice_on && l_scroll_on && l_status_y >= 0 && !l_has_status)
        ui_status_bar("", 0);                      /* the page drew no status text: the notice is still shown */
    if (!l_footer)
    {
        l_footer = 1;                              /* a footerless page has no room for the hint or a notice */
        return;
    }
    if (l_notice_on && !l_has_status && l_status_y < 0)
    {
        hint = l_notice;                           /* a page without a status line: the notice takes the footer */
        kind = l_notice_kind;
    }
    if (hint != NULL)
    {
        int y = m->panel_y + m->panel_h - m->hint_h - m->pad;
        char buf[96];
        fit_text(buf, sizeof(buf), hint, m->hint_font, m->hint_scale, m->panel_w - 2 * m->pad);
        ugl_text_f(m->hint_font, m->panel_x + (m->panel_w - ugl_text_width_f(m->hint_font, m->hint_scale, buf)) / 2, y,
                   m->hint_scale, kind > 0 ? UGL_RGBA(110, 225, 150, 255) : kind < 0 ? UGL_RGBA(255, 120, 120, 255) : COL_HINT, buf);
    }
}

void ui_grid(int cols)      { l_gridcols = cols < 1 ? 1 : cols; }
void ui_row_begin(int n)    { l_rowcols = n > 1 ? n : 0; l_rowcell = 0; }

static int row_y(void)
{
    ui_metrics *m = &l_m;
    int title_h = FONT_GLYPH_H * m->item + 2 * m->pad;
    int y0 = m->panel_y + title_h + m->pad;
    int r = l_row++;
    l_last_row = r;
    if (l_scroll_on)
    {
        if (r < l_top || r >= l_top + l_vis)
            return -1;
        return y0 + (r - l_top) * m->row_h;
    }
    {
        int y = y0 + l_yoff;
        l_yoff += m->row_h;
        return y;
    }
}

static int l_last_y = -1;       /* -1: scrolled out of view */
static int l_last_focused;

typedef struct {
    int id, y, x0, x1;
    int focused, cell;
} ui_slot;

static ui_slot place(void)
{
    ui_metrics *m = &l_m;
    ui_slot s;
    ui_navcell nc;

    s.id = l_nfocus++;
    if (l_rowcols > 0)
    {
        int gap = m->pad, w = l_bx1 - l_bx0, cw = (w - (l_rowcols - 1) * gap) / l_rowcols;
        if (l_rowcell == 0)
        {
            l_cur_y = row_y();
            l_cur_row = l_last_row;
        }
        s.y = l_cur_y;
        s.x0 = l_bx0 + l_rowcell * (cw + gap);
        s.x1 = l_rowcell == l_rowcols - 1 ? l_bx1 : s.x0 + cw;
        s.cell = 1;
        nc.row = l_cur_row;
        nc.col = l_rowcell;
        nc.span = 1;
        if (++l_rowcell >= l_rowcols)
            l_rowcols = 0;
    }
    else
    {
        s.y = row_y();
        s.x0 = l_bx0;
        s.x1 = l_bx1;
        s.cell = 0;
        nc.row = l_last_row;
        nc.col = 0;
        nc.span = l_gridcols;
    }
    if (s.id < UI_MAX_IDS)
        l_cells[s.id] = nc;
    s.focused = s.id == l_focus;
    l_last_y = s.y;
    l_last_focused = s.focused;
    return s;
}

typedef struct {
    ugl_font font;
    int      scale;
} font_ctx;

static int measure_cb(void *ctx, const char *s)
{
    const font_ctx *f = (const font_ctx *) ctx;
    return ugl_text_width_f(f->font, f->scale, s);
}

static void fit_text(char *buf, size_t size, const char *src, ugl_font font, int scale, int avail)
{
    font_ctx f = { font, scale };
    uitext_fit(buf, size, src, avail, measure_cb, &f);
}

static void row_bg(const ui_slot *s, uint32_t col)
{
    ui_metrics *m = &l_m;
    ugl_rect(s->x0, s->y, s->x1 - s->x0, m->row_h - m->pad / 2, col);
}

static void row_outline(const ui_slot *s, int t, uint32_t col)
{
    ui_metrics *m = &l_m;
    int w = s->x1 - s->x0, h = m->row_h - m->pad / 2;
    ugl_rect(s->x0, s->y, w, t, col);
    ugl_rect(s->x0, s->y + h - t, w, t, col);
    ugl_rect(s->x0, s->y, t, h, col);
    ugl_rect(s->x0 + w - t, s->y, t, h, col);
}

/* neither may leave the row: the value keeps at least half of it, the label the rest */
static void draw_row_ex(const ui_slot *s, const char *label, const char *value, int disabled)
{
    ui_metrics *m = &l_m;
    int ty = s->y + m->pad, tx0 = l_tx0, avail = l_tx1 - l_tx0;
    uint32_t fg = disabled ? (s->focused ? COL_DIS_FOCUS_TEXT : COL_DIS_TEXT) : s->focused ? COL_TEXT_FOCUS : COL_TEXT;
    uint32_t vfg = disabled ? fg : s->focused ? COL_TEXT_FOCUS : COL_VALUE;
    char lb[96], vb[96];
    int gap = FONT_GLYPH_W * m->item, vw = 0;

    if (s->y < 0)
        return;
    if (s->focused)
        row_bg(s, disabled ? COL_DIS_FOCUS : COL_FOCUS_BG);
    if (disabled && s->focused)
        row_outline(s, m->body > 1 ? m->body : 2, COL_BORDER);
    if (value != NULL)
    {
        int lw = ugl_text_width(m->item, label), vmax = avail - lw - gap;
        if (vmax < avail / 2)
            vmax = avail / 2;
        fit_text(vb, sizeof(vb), value, UGL_FONT_ITEM, m->item, vmax);
        vw = ugl_text_width(m->item, vb);
        ugl_text(l_tx1 - vw, ty, m->item, vfg, vb);
        avail -= vw + gap;
    }
    fit_text(lb, sizeof(lb), label, UGL_FONT_ITEM, m->item, avail);
    ugl_text(tx0, ty, m->item, fg, lb);
}

static void draw_row(const ui_slot *s, const char *label, const char *value)
{
    draw_row_ex(s, label, value, 0);
}

static int value_room(const char *label)
{
    int avail = l_tx1 - l_tx0, v = avail - ugl_text_width(l_m.item, label) - FONT_GLYPH_W * l_m.item;
    return v < avail / 2 ? avail / 2 : v;
}

static void arrowed(char *buf, size_t size, const char *label, const char *text, int focused)
{
    char t[80];
    fit_text(t, sizeof(t), text, UGL_FONT_ITEM, l_m.item, value_room(label) - (focused ? 4 * FONT_GLYPH_W * l_m.item : 0));
    snprintf(buf, size, "%s%s%s", focused ? "< " : "", t, focused ? " >" : "");
}

static void draw_button(const ui_slot *s, const char *label, int centered, int disabled)
{
    ui_metrics *m = &l_m;
    char buf[96];
    int w = s->x1 - s->x0;
    uint32_t fg;

    if (s->y < 0)
        return;
    if (s->cell || centered)
    {
        uint32_t bg = COL_CELL_BG;
        if (s->focused)
            bg = disabled ? COL_DIS_FOCUS : COL_FOCUS_BG;
        row_bg(s, bg);
    }
    else if (s->focused)
        row_bg(s, disabled ? COL_DIS_FOCUS : COL_FOCUS_BG);
    if (disabled && s->focused)
        row_outline(s, m->body > 1 ? m->body : 2, COL_BORDER);
    fg = disabled ? (s->focused ? COL_DIS_FOCUS_TEXT : COL_DIS_TEXT) : s->focused ? COL_TEXT_FOCUS : COL_TEXT;
    if (s->cell || centered)
    {
        fit_text(buf, sizeof(buf), label, UGL_FONT_ITEM, m->item, w - 2 * m->pad);
        ugl_text(s->x0 + (w - ugl_text_width(m->item, buf)) / 2, s->y + m->pad, m->item, fg, buf);
    }
    else
    {
        fit_text(buf, sizeof(buf), label, UGL_FONT_ITEM, m->item, l_tx1 - l_tx0);
        ugl_text(l_tx0, s->y + m->pad, m->item, fg, buf);
    }
}

void ui_label(const char *text)
{
    ui_metrics *m = &l_m;
    int y = row_y();
    char buf[96];
    if (y < 0)
        return;
    fit_text(buf, sizeof(buf), text, UGL_FONT_ITEM, m->item, l_tx1 - l_tx0);
    ugl_text(m->panel_x + (m->panel_w - ugl_text_width(m->item, buf)) / 2, y + m->pad, m->item, COL_HINT, buf);
}

int ui_button(const char *label)
{
    ui_slot s = place();
    draw_button(&s, label, 0, 0);
    return s.focused && l_activate;
}

int ui_button_center(const char *label)
{
    ui_slot s = place();
    draw_button(&s, label, 1, 0);
    return s.focused && l_activate;
}

int ui_button_dis(const char *label, int disabled)
{
    ui_slot s = place();
    draw_button(&s, label, 0, disabled);
    if (!disabled)
        return s.focused && l_activate;
    return (s.focused && l_activate) ? -1 : 0;
}

int ui_toggle(const char *label, int *value)
{
    ui_slot s = place();
    int changed = 0;
    draw_row(&s, label, *value ? "On" : "Off");
    if (s.focused && (l_activate || l_adjust != 0))
    {
        *value = !*value;
        changed = 1;
    }
    return changed;
}

void ui_toggle_locked(const char *label, int value)
{
    ui_slot s = place();
    draw_row_ex(&s, label, value ? "On" : "Off", 1);
}

void ui_slider_locked(const char *label, int value, const char *suffix)
{
    ui_slot s = place();
    char buf[32];
    snprintf(buf, sizeof(buf), "%d%s", value, suffix ? suffix : "");
    draw_row_ex(&s, label, buf, 1);
}

int ui_slider(const char *label, int *value, int min, int max, int step, const char *suffix)
{
    ui_slot s = place();
    int changed = 0;
    char buf[32];

    if (s.focused && l_adjust != 0)
    {
        int nv = *value + l_adjust * step;
        if (nv < min) nv = min;
        if (nv > max) nv = max;
        if (nv != *value)
        {
            *value = nv;
            changed = 1;
        }
    }
    snprintf(buf, sizeof(buf), "%s%d%s%s", s.focused ? "< " : "", *value, suffix ? suffix : "", s.focused ? " >" : "");
    draw_row(&s, label, buf);
    return changed;
}

int ui_carousel(const ui_card *cards, int count, int sel)
{
    ui_metrics *m = &l_m;
    ui_slot s = place();
    int y0 = s.y;
    int h_total, small, gap, card_w, visible, i, x0, cy;
    int gw = FONT_GLYPH_W * m->item;
    int gutter = gw + 2 * m->pad;
    int body_w = (l_bx1 - l_bx0) - 2 * gutter;
    int dir = 0;

    l_yoff += 2 * m->row_h;
    if (s.focused && l_adjust != 0)
        dir = l_adjust;
    h_total = 3 * m->row_h - 3 * m->pad;                        /* leaves a gap above the status line */
    small = m->item > 1 ? m->item - 1 : 1;
    gap = m->pad;
    {
        /* widest line is the date; show as many cards as fit (odd number, at most 5) */
        int need = ugl_text_width(small, "0000-00-00") + m->pad;
        visible = 5;
        while (visible > 1 && (body_w - (visible - 1) * gap) / visible < need)
            visible -= 2;
    }
    if (visible > count)
        visible = count % 2 ? count : count - 1;
    card_w = (body_w - (visible - 1) * gap) / visible;
    x0 = l_bx0 + gutter + (body_w - (visible * card_w + (visible - 1) * gap)) / 2;
    cy = y0;

    if (s.focused)
    {
        ugl_rect(l_bx0, cy, gutter, h_total, COL_FOCUS_BG);
        ugl_rect(l_bx1 - gutter, cy, gutter, h_total, COL_FOCUS_BG);
    }
    for (i = 0; i < visible; i++)
    {
        int off = i - visible / 2;
        int idx = ((sel + off) % count + count) % count;
        const ui_card *c = &cards[idx];
        int x = x0 + i * (card_w + gap);
        int focused = (off == 0);
        uint32_t bg = focused ? COL_FOCUS_BG : COL_CELL_BG;
        uint32_t fg = focused ? COL_TEXT_FOCUS : COL_TEXT;
        uint32_t fg2 = focused ? COL_TEXT_FOCUS : COL_HINT;
        int ty = cy + m->pad;
        int h = h_total;

        if (!focused)
        {
            int shrink = m->pad * 2;
            ugl_rect(x, cy + shrink, card_w, h - 2 * shrink, bg);
            ty = cy + shrink + m->pad;
        }
        else
        {
            ugl_rect(x - m->body, cy - m->body, card_w + 2 * m->body, h + 2 * m->body, s.focused ? COL_TEXT_FOCUS : COL_BORDER);
            ugl_rect(x, cy, card_w, h, bg);
        }
        ugl_text(x + (card_w - ugl_text_width(m->item, c->title)) / 2, ty, m->item, fg, c->title);
        ty += FONT_GLYPH_H * m->item + m->pad / 2;
        ugl_text(x + (card_w - ugl_text_width(small, c->l1)) / 2, ty, small, c->empty && !focused ? COL_HINT : fg2, c->l1);
        ty += FONT_GLYPH_H * small;
        if (c->l2[0])
            ugl_text(x + (card_w - ugl_text_width(small, c->l2)) / 2, ty, small, fg2, c->l2);
        if (c->tag[0])
        {
            ty += FONT_GLYPH_H * small;
            ugl_text(x + (card_w - ugl_text_width(small, c->tag)) / 2, ty, small, focused ? COL_TEXT_FOCUS : COL_VALUE, c->tag);
        }
    }
    {
        int ay = y0 + (h_total - FONT_GLYPH_H * m->item) / 2;
        uint32_t ac = s.focused ? COL_TEXT_FOCUS : COL_VALUE;
        ugl_text(l_bx0 + (gutter - gw) / 2, ay, m->item, ac, "<");
        ugl_text(l_bx1 - gutter + (gutter - gw) / 2, ay, m->item, ac, ">");
    }
    return dir;
}

/* the 12x24 cell is a little taller than a half row at the smallest size; its glyphs are not, the cell margin is
 * empty */
static uint32_t status_color(int kind)
{
    return kind > 0 ? UGL_RGBA(110, 225, 150, 255) : kind < 0 ? UGL_RGBA(255, 120, 120, 255) : COL_HINT;
}

static void status_text(int y, const char *text, int kind)
{
    ui_metrics *m = &l_m;
    char buf[96];
    fit_text(buf, sizeof(buf), text, m->hint_font, m->hint_scale, m->panel_w - 4 * m->pad);
    ugl_text_f(m->hint_font, m->panel_x + (m->panel_w - ugl_text_width_f(m->hint_font, m->hint_scale, buf)) / 2,
               y + (m->row_h / 2 - m->hint_h) / 2, m->hint_scale, status_color(kind), buf);
}

static void status_text2(int y, const char *text, int kind)
{
    ui_metrics *m = &l_m;
    char l1[160], l2[160];
    font_ctx f = { m->hint_font, m->hint_scale };
    int n = uitext_wrap2(text, m->panel_w - 4 * m->pad, measure_cb, &f, l1, l2, sizeof(l1));
    uint32_t col = status_color(kind);
    ugl_text_f(m->hint_font, m->panel_x + (m->panel_w - ugl_text_width_f(m->hint_font, m->hint_scale, l1)) / 2, y, m->hint_scale, col, l1);
    if (n > 1)
        ugl_text_f(m->hint_font, m->panel_x + (m->panel_w - ugl_text_width_f(m->hint_font, m->hint_scale, l2)) / 2, y + m->hint_h,
                   m->hint_scale, col, l2);
}

void ui_status(const char *text, int kind)
{
    ui_metrics *m = &l_m;
    int y = m->panel_y + FONT_GLYPH_H * m->item + 2 * m->pad + m->pad + l_yoff;
    l_yoff += m->row_h / 2;
    l_has_status = 1;
    if (l_notice_on)
    {
        text = l_notice;
        kind = l_notice_kind;
    }
    if (text != NULL && text[0] != 0)
        status_text(y, text, kind);
}

static int l_next_width;

void ui_next_panel_width(int chars) { l_next_width = chars; }

void ui_panel_begin_scroll(const char *title, int rows, int with_status)
{
    ui_metrics *m = &l_m;
    int vis = rows, fr, total = l_rows_total;
    int width = l_next_width > 0 ? l_next_width : UI_W_LIST;

    l_next_width = 0;
    while (vis > 4 && panel_height_for(m, vis * 2, with_status) > panel_max_h(m))
        vis--;
    l_scroll_on = 1;                     /* the rows leave room for the scroll bar */
    panel_begin(title, vis * 2, with_status, width);
    l_row = 0;
    l_vis = vis;
    if (with_status)
    {
        int title_h = FONT_GLYPH_H * m->item + 2 * m->pad;
        l_status_y = m->panel_y + title_h + m->pad + vis * m->row_h;
    }
    fr = (l_table_n > 0 && l_focus >= 0 && l_focus < l_table_n) ? l_cells[l_focus].row : 0;
    if (l_restored)
    {
        l_restored = 0;                    /* the row table is from the page we came back from: keep the saved position */
        fr = l_top;
    }
    else if (l_focus == 0)
        l_top = 0;
    if (fr < l_top)
        l_top = fr;
    if (fr >= l_top + vis)
        l_top = fr - vis + 1;
    if (total > 0 && l_top > total - vis)
        l_top = total - vis;
    if (l_top < 0)
        l_top = 0;
}

/* returns 1 when A is pressed on it, 2 for X (the "clear" action) */
int ui_item(const char *label, const char *value)
{
    ui_slot s = place();
    draw_row(&s, label, value);
    if (!s.focused)
        return 0;
    if (l_activate)
        return 1;
    if (l_pressed & PADBIT(PAD_X))
        return 2;
    return 0;
}

int ui_choice(const char *label, int *idx, const char *const *names, int n)
{
    ui_slot s = place();
    int changed = 0;
    char buf[96];

    if (n <= 0)
        return 0;
    if (*idx < 0 || *idx >= n)
        *idx = 0;
    if (s.focused && (l_activate || l_adjust != 0))
    {
        *idx = (*idx + (l_adjust != 0 ? l_adjust : 1) + n) % n;
        changed = 1;
    }
    arrowed(buf, sizeof(buf), label, names[*idx], s.focused);
    draw_row(&s, label, buf);
    return changed;
}

int ui_step(const char *label, const char *text)
{
    ui_slot s = place();
    int dir = 0;
    char buf[96];

    if (s.focused && (l_activate || l_adjust != 0))
        dir = l_adjust != 0 ? l_adjust : 1;
    arrowed(buf, sizeof(buf), label, text, s.focused);
    draw_row(&s, label, buf);
    return dir;
}

/* A is its own action (returns 1) and Left / Right set *dir */
int ui_stepper(const char *label, const char *text, int *dir)
{
    ui_slot s = place();
    char buf[96];

    *dir = s.focused ? l_adjust : 0;
    arrowed(buf, sizeof(buf), label, text, s.focused);
    draw_row(&s, label, buf);
    return s.focused && l_activate;
}

void ui_mark_last(int kind)
{
    ui_metrics *m = &l_m;
    int w = m->body * 2 + 2;
    if (l_last_focused)
        l_focus_mark = kind > 0 ? kind : 0;
    if (l_last_y < 0 || kind <= 0)
        return;
    ugl_rect(l_bx0 + m->body, l_last_y + 2, w, m->row_h - m->pad / 2 - 4,
             kind == 1 ? UGL_RGBA(110, 225, 150, 255) : UGL_RGBA(150, 160, 80, 255));
}

int ui_focus_mark(void)
{
    return l_focus_mark;
}

int ui_x_last(void)
{
    return l_last_focused && (l_pressed & PADBIT(PAD_X));
}

/* a toast made while the menu is open is shown here instead of the page's text, until it expires */
void ui_status_bar(const char *text, int kind)
{
    if (l_status_y < 0)
        return;
    l_has_status = 1;
    if (l_notice_on)
    {
        text = l_notice;
        kind = l_notice_kind;
    }
    if (text == NULL || text[0] == 0)
        return;
    status_text2(l_status_y, text, kind);
}

void ui_progress(int permille)
{
    ui_metrics *m = &l_m;
    int y = row_y(), x, w, h;
    if (y < 0)
        return;
    if (permille < 0) permille = 0;
    if (permille > 1000) permille = 1000;
    x = l_bx0 + 2 * m->pad;
    w = (l_bx1 - l_bx0) - 4 * m->pad;
    h = m->row_h / 3;
    y += (m->row_h - h) / 2;
    ugl_rect(x, y, w, h, COL_CELL_BG);
    ugl_rect(x, y, w * permille / 1000, h, COL_FOCUS_BG);
}

void ui_hold_bar(const char *text, int permille)
{
    ui_metrics *m = &l_m;
    int y = row_y(), w = l_bx1 - l_bx0;
    char buf[96];
    if (y < 0)
        return;
    if (permille < 0) permille = 0;
    if (permille > 1000) permille = 1000;
    ugl_rect(l_bx0, y, w, m->row_h - m->pad / 2, COL_CELL_BG);
    ugl_rect(l_bx0, y, w * permille / 1000, m->row_h - m->pad / 2, COL_FOCUS_BG);
    fit_text(buf, sizeof(buf), text, UGL_FONT_ITEM, m->item, w - 2 * m->pad);
    ugl_text(l_bx0 + (w - ugl_text_width(m->item, buf)) / 2, y + m->pad, m->item, COL_TEXT_FOCUS, buf);
}

void ui_label_hi(const char *text)
{
    ui_metrics *m = &l_m;
    int y = row_y();
    char buf[96];
    if (y < 0)
        return;
    fit_text(buf, sizeof(buf), text, UGL_FONT_ITEM, m->item, l_tx1 - l_tx0);
    ugl_text(m->panel_x + (m->panel_w - ugl_text_width(m->item, buf)) / 2, y + m->pad, m->item, COL_VALUE, buf);
}

/* the body of a cell keeps the width of `ghost` (else of `on`), so a cell that fills or empties does not move
 * anything */
void ui_cells(const ui_cell *c, int n)
{
    ui_metrics *m = &l_m;
    int y = row_y(), i, total = 0, avail = l_bx1 - l_bx0, x = l_bx0;
    int want[8], lw[8];
    const int sp = ugl_text_width_f(m->hint_font, m->hint_scale, " ");

    if (y < 0 || n < 1 || n > 8)
        return;
    for (i = 0; i < n; i++)
    {
        const char *body = c[i].ghost != NULL ? c[i].ghost : c[i].on;
        int has_l = c[i].label != NULL && c[i].label[0], has_b = body != NULL && body[0];
        lw[i] = has_l ? ugl_text_width_f(m->hint_font, m->hint_scale, c[i].label) + (has_b ? sp : 0) : 0;
        want[i] = lw[i] + (has_b ? ugl_text_width_f(m->hint_font, m->hint_scale, body) : 0) + m->pad;
        total += want[i];
    }
    for (i = 0; i < n; i++)
    {
        int cw = total > avail ? want[i] * avail / total : want[i] + (avail - total) / n;
        int tx = x + (cw - (want[i] - m->pad)) / 2, ty = y + (m->row_h - m->hint_h) / 2 - m->pad / 2;
        if (total > avail)
        {
            /* a screen too small for the cells: each one is its label and what is on, cut with ".." to its share (no
             * ghosts) */
            char t[96], f[96];
            snprintf(t, sizeof(t), "%s%s%s", c[i].label ? c[i].label : "", c[i].label && c[i].label[0] && c[i].on && c[i].on[0] ? " " : "",
                     c[i].on ? c[i].on : "");
            fit_text(f, sizeof(f), t, m->hint_font, m->hint_scale, cw - m->pad / 2);
            ugl_text_f(m->hint_font, x + (cw - ugl_text_width_f(m->hint_font, m->hint_scale, f)) / 2, ty, m->hint_scale, COL_VALUE, f);
            x += cw;
            continue;
        }
        if (c[i].label != NULL && c[i].label[0])
            ugl_text_f(m->hint_font, tx, ty, m->hint_scale, COL_HINT, c[i].label);
        if (c[i].ghost != NULL && c[i].ghost[0])
            ugl_text_f(m->hint_font, tx + lw[i], ty, m->hint_scale, COL_GHOST, c[i].ghost);
        if (c[i].on != NULL && c[i].on[0])
            ugl_text_f(m->hint_font, tx + lw[i], ty, m->hint_scale, COL_VALUE, c[i].on);
        x += cw;
    }
}
