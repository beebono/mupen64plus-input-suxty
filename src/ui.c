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
#include "ui.h"

#define COL_PANEL      UGL_RGBA(14, 18, 28, 236)
#define COL_BORDER     UGL_RGBA(70, 130, 230, 255)
#define COL_TITLE      UGL_RGBA(120, 180, 255, 255)
#define COL_TEXT       UGL_RGBA(205, 210, 220, 255)
#define COL_TEXT_FOCUS UGL_RGBA(255, 255, 255, 255)
#define COL_FOCUS_BG   UGL_RGBA(42, 100, 210, 255)
#define COL_HINT       UGL_RGBA(140, 150, 168, 255)
#define COL_VALUE      UGL_RGBA(255, 214, 102, 255)

#define UI_MAX_IDS 64

static void fit_text(char *buf, size_t size, const char *src, int scale, int avail);

static ui_metrics l_m;
static int l_focus;
static int l_nfocus_prev = 1;
static int l_nfocus;           /* focusables seen so far this frame */
static int l_activate;         /* A pressed this frame */
static int l_adjust;           /* -1/0/+1 from left/right this frame */
static int l_yoff;             /* pixel offset of the next row inside the panel's body */
static int l_hints = 1;
static uint32_t l_pressed;     /* the whole pressed mask of this frame (for the secondary actions) */

/* scrolling lists (ui_panel_begin_scroll) */
static int l_scroll_on;        /* the current panel scrolls */
static int l_row;              /* next row index */
static int l_last_row;         /* row of the widget that was just placed */
static int l_vis;              /* visible rows */
static int l_top;              /* first visible row */
static int l_rows_total;       /* rows seen last frame */
static int l_id_row[UI_MAX_IDS];   /* focusable id -> row, from the last frame */
static int l_status_y = -1;
static int l_focus_reset;      /* the focus was set during this frame (page change): ui_end() must not clamp it
                                  against this page's focusable count */
static int l_restored;         /* focus and scroll position were just restored (going back): trust them for one frame */    /* fixed status line of a scrolling panel, -1 = none */

void ui_metrics_for(ui_metrics *m, int w, int h)
{
    int short_side = h < w ? h : w;
    m->w = w;
    m->h = h;
    /* 480 -> 1/2, 720/768 -> 2/3, 1080 and up -> 3/4 */
    m->body = short_side < 600 ? 1 : short_side < 1000 ? 2 : 3;
    m->item = m->body + 1;
    m->pad = 4 * m->body;
    m->row_h = FONT_GLYPH_H * m->item + 2 * m->pad;
}

void ui_set_hints_visible(int visible) { l_hints = visible; }
/* the focusable count of the page about to be drawn is not known yet: don't clamp against the last page's */
void ui_focus_set(int index)           { l_focus = index; l_top = 0; l_nfocus_prev = UI_MAX_IDS; l_restored = 0; l_focus_reset = 1; }
int  ui_scroll_get(void)               { return l_top; }
void ui_focus_restore(int index, int top) { l_focus = index; l_top = top; l_nfocus_prev = UI_MAX_IDS; l_restored = 1; l_focus_reset = 1; }
int  ui_focus_get(void)                { return l_focus; }

void ui_begin(int w, int h, uint32_t pressed)
{
    ui_metrics_for(&l_m, w, h);
    l_pressed = pressed;
    l_activate = (pressed & PADBIT(PAD_A)) != 0;
    l_adjust = ((pressed & PADBIT(PAD_RIGHT)) ? 1 : 0) - ((pressed & PADBIT(PAD_LEFT)) ? 1 : 0);
    if (l_nfocus_prev < 1)
        l_nfocus_prev = 1;
    if (pressed & PADBIT(PAD_DOWN))
        l_focus = (l_focus + 1) % l_nfocus_prev;
    if (pressed & PADBIT(PAD_UP))
        l_focus = (l_focus + l_nfocus_prev - 1) % l_nfocus_prev;
    if (l_focus >= l_nfocus_prev)
        l_focus = l_nfocus_prev - 1;
    l_nfocus = 0;
    l_yoff = 0;
    l_scroll_on = 0;
    l_status_y = -1;
}

void ui_end(void)
{
    if (l_focus_reset)
        l_focus_reset = 0;                 /* a new page was chosen this frame: its count is not known yet */
    else
        l_nfocus_prev = l_nfocus > 0 ? l_nfocus : 1;
}

static int panel_height_for(const ui_metrics *m, int rows2)
{
    int title_h = FONT_GLYPH_H * m->item + 2 * m->pad;
    int hint_h = l_hints ? FONT_GLYPH_H * m->body + 2 * m->pad : 0;
    return title_h + rows2 * m->row_h / 2 + hint_h + 2 * m->pad;
}

void ui_panel_begin(const char *title, int rows)
{
    ui_panel_begin_ex(title, rows * 2, 60);
}

/* rows2: body height in half rows; width_percent: of the screen width */
void ui_panel_begin_ex(const char *title, int rows2, int width_percent)
{
    ui_metrics *m = &l_m;
    int title_h, min_w, line, rows = rows2;

    /* tiny screens: step the text size down until the panel fits */
    while (m->item > 1 && panel_height_for(m, rows) > m->h - 2 * m->pad)
    {
        m->item--;
        if (m->body > m->item)
            m->body = m->item;
        m->pad = 4 * m->body;
        m->row_h = FONT_GLYPH_H * m->item + 2 * m->pad;
    }

    title_h = FONT_GLYPH_H * m->item + 2 * m->pad;
    min_w = 22 * FONT_GLYPH_W * m->item;
    line = m->body;                         /* border thickness */

    m->panel_w = m->w * width_percent / 100;
    if (m->panel_w < min_w) m->panel_w = min_w;
    if (m->panel_w > m->w - 4 * m->pad) m->panel_w = m->w - 4 * m->pad;
    m->panel_h = panel_height_for(m, rows);
    if (m->panel_h > m->h - 2 * m->pad) m->panel_h = m->h - 2 * m->pad;
    m->panel_x = (m->w - m->panel_w) / 2;
    m->panel_y = (m->h - m->panel_h) / 2;

    l_scroll_on = 0;
    l_status_y = -1;
    ugl_rect(m->panel_x - line, m->panel_y - line, m->panel_w + 2 * line, m->panel_h + 2 * line, COL_BORDER);
    ugl_rect(m->panel_x, m->panel_y, m->panel_w, m->panel_h, COL_PANEL);

    ugl_text(m->panel_x + (m->panel_w - ugl_text_width(m->item, title)) / 2, m->panel_y + m->pad + m->pad / 2,
             m->item, COL_TITLE, title);
    /* separator under the title */
    ugl_rect(m->panel_x + m->pad, m->panel_y + title_h + m->pad / 2, m->panel_w - 2 * m->pad, line, COL_BORDER);
    l_yoff = 0;
}

void ui_panel_end(const char *hint)
{
    ui_metrics *m = &l_m;
    if (l_scroll_on)
    {
        /* "more above / below" markers in the title bar */
        int ty = m->panel_y + m->pad + m->pad / 2;
        int x = m->panel_x + m->panel_w - 2 * m->pad - FONT_GLYPH_W * m->item;
        l_rows_total = l_row;
        if (l_top > 0)
            ugl_text(x, ty, m->item, COL_VALUE, "^");
        if (l_top + l_vis < l_row)
            ugl_text(x - (FONT_GLYPH_W * m->item) * 3 / 2, ty, m->item, COL_VALUE, "v");
    }
    if (l_hints && hint != NULL)
    {
        int y = m->panel_y + m->panel_h - FONT_GLYPH_H * m->body - m->pad;
        char buf[96];
        fit_text(buf, sizeof(buf), hint, m->body, m->panel_w - 2 * m->pad);     /* small screens: cut rather than overflow */
        ugl_text(m->panel_x + (m->panel_w - ugl_text_width(m->body, buf)) / 2, y, m->body, COL_HINT, buf);
    }
}

static int row_y(void)
{
    ui_metrics *m = &l_m;
    int title_h = FONT_GLYPH_H * m->item + 2 * m->pad;
    int y0 = m->panel_y + title_h + m->pad;
    if (l_scroll_on)
    {
        int r = l_row++;
        l_last_row = r;
        if (r < l_top || r >= l_top + l_vis)
            return -1;                      /* scrolled out of view */
        return y0 + (r - l_top) * m->row_h;
    }
    l_last_row = l_yoff / (m->row_h > 0 ? m->row_h : 1);
    {
        int y = y0 + l_yoff;
        l_yoff += m->row_h;
        return y;
    }
}

static int l_last_y = -1;       /* where the last focusable row went (-1: scrolled out of view) */
static int l_last_focused;

/* the next focusable: its id, and where it goes */
static int next_focusable(int *y)
{
    int id = l_nfocus++;
    *y = row_y();
    l_last_y = *y;
    l_last_focused = (id == l_focus);
    if (id < UI_MAX_IDS)
        l_id_row[id] = l_last_row;
    return id;
}

/* `src` cut short with ".." until it is at most `avail` pixels wide */
static void fit_text(char *buf, size_t size, const char *src, int scale, int avail)
{
    size_t k = strlen(src);
    snprintf(buf, size, "%s", src);
    while (k > 1 && ugl_text_width(scale, buf) > avail)
    {
        k--;
        snprintf(buf, size, "%.*s..", (int) k, src);
    }
}

static void draw_row(int y, int focused, const char *label, const char *value)
{
    ui_metrics *m = &l_m;
    int ty = y + m->pad;
    int x = m->panel_x + 2 * m->pad;
    if (y < 0)
        return;
    if (focused)
        ugl_rect(m->panel_x + m->pad, y, m->panel_w - 2 * m->pad, m->row_h - m->pad / 2, COL_FOCUS_BG);
    ugl_text(x + m->pad, ty, m->item, focused ? COL_TEXT_FOCUS : COL_TEXT, label);
    if (value != NULL)
    {
        char buf[64];
        int avail = m->panel_w - 6 * m->pad - ugl_text_width(m->item, label) - FONT_GLYPH_W * m->item;
        fit_text(buf, sizeof(buf), value, m->item, avail);
        ugl_text(m->panel_x + m->panel_w - 2 * m->pad - m->pad - ugl_text_width(m->item, buf), ty, m->item,
                 focused ? COL_TEXT_FOCUS : COL_VALUE, buf);
    }
}

void ui_label(const char *text)
{
    ui_metrics *m = &l_m;
    int y = row_y();
    if (y < 0)
        return;
    ugl_text(m->panel_x + (m->panel_w - ugl_text_width(m->item, text)) / 2, y + m->pad, m->item, COL_HINT, text);
}

int ui_button(const char *label)
{
    int y, id = next_focusable(&y);
    int focused = (id == l_focus);
    draw_row(y, focused, label, NULL);
    return focused && l_activate;
}

int ui_button_dis(const char *label, int disabled)
{
    ui_metrics *m = &l_m;
    int y, id = next_focusable(&y);
    int focused = (id == l_focus);
    if (!disabled)
    {
        draw_row(y, focused, label, NULL);
        return focused && l_activate;
    }
    /* disabled rows keep their focus slot (so the focus index is stable) but are dimmed and inert */
    if (y < 0)
        return (focused && l_activate) ? -1 : 0;
    if (focused)
        ugl_rect(m->panel_x + m->pad, y, m->panel_w - 2 * m->pad, m->row_h - m->pad / 2, UGL_RGBA(30, 38, 56, 255));
    ugl_text(m->panel_x + 3 * m->pad, y + m->pad, m->item, UGL_RGBA(90, 98, 112, 255), label);
    return (focused && l_activate) ? -1 : 0;
}

int ui_toggle(const char *label, int *value)
{
    int y, id = next_focusable(&y);
    int focused = (id == l_focus);
    int changed = 0;
    draw_row(y, focused, label, *value ? "ON" : "OFF");
    if (focused && (l_activate || l_adjust != 0))
    {
        *value = !*value;
        changed = 1;
    }
    return changed;
}

int ui_slider(const char *label, int *value, int min, int max, int step, const char *suffix)
{
    int y, id = next_focusable(&y);
    int focused = (id == l_focus);
    int changed = 0;
    char buf[32];

    if (focused && l_adjust != 0)
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
    snprintf(buf, sizeof(buf), "%s%d%s%s", focused ? "< " : "", *value, suffix ? suffix : "", focused ? " >" : "");
    draw_row(y, focused, label, buf);
    return changed;
}

/* ---- slot carousel and status line -------------------------------------------------------- */

/* three rows tall; cards are laid out around the centre one, which is highlighted */
void ui_carousel(const ui_card *cards, int count, int sel)
{
    ui_metrics *m = &l_m;
    int y0 = row_y();
    int h_total, small, gap, card_w, visible, i, x0, cy;
    int gutter = FONT_GLYPH_W * m->item + m->pad;            /* room for the scroll arrows */
    int body_w = m->panel_w - 2 * gutter;

    l_yoff += 2 * m->row_h;                                     /* the other two rows */
    h_total = 3 * m->row_h - m->pad;
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
    x0 = m->panel_x + gutter + (body_w - (visible * card_w + (visible - 1) * gap)) / 2;
    cy = y0;

    for (i = 0; i < visible; i++)
    {
        int off = i - visible / 2;
        int idx = ((sel + off) % count + count) % count;
        const ui_card *c = &cards[idx];
        int x = x0 + i * (card_w + gap);
        int focused = (off == 0);
        uint32_t bg = focused ? COL_FOCUS_BG : UGL_RGBA(30, 38, 56, 255);
        uint32_t fg = focused ? COL_TEXT_FOCUS : COL_TEXT;
        uint32_t fg2 = focused ? COL_TEXT_FOCUS : COL_HINT;
        int ty = cy + m->pad;
        int h = h_total;

        if (!focused)
        {
            /* neighbours are a little shorter so the centre card stands out */
            int shrink = m->pad * 2;
            ugl_rect(x, cy + shrink, card_w, h - 2 * shrink, bg);
            ty = cy + shrink + m->pad;
        }
        else
        {
            ugl_rect(x - m->body, cy - m->body, card_w + 2 * m->body, h + 2 * m->body, COL_BORDER);
            ugl_rect(x, cy, card_w, h, bg);
        }
        ugl_text(x + (card_w - ugl_text_width(m->item, c->title)) / 2, ty, m->item, fg, c->title);
        ty += FONT_GLYPH_H * m->item + m->pad / 2;
        ugl_text(x + (card_w - ugl_text_width(small, c->l1)) / 2, ty, small, c->empty ? COL_HINT : fg2, c->l1);
        ty += FONT_GLYPH_H * small;
        if (c->l2[0])
            ugl_text(x + (card_w - ugl_text_width(small, c->l2)) / 2, ty, small, fg2, c->l2);
        if (c->tag[0])
        {
            ty += FONT_GLYPH_H * small;
            ugl_text(x + (card_w - ugl_text_width(small, c->tag)) / 2, ty, small, COL_VALUE, c->tag);
        }
    }
    /* scroll arrows at the panel edges */
    ugl_text(m->panel_x + m->pad / 2, y0 + (h_total - FONT_GLYPH_H * m->item) / 2, m->item, COL_VALUE, "<");
    ugl_text(m->panel_x + m->panel_w - m->pad / 2 - FONT_GLYPH_W * m->item, y0 + (h_total - FONT_GLYPH_H * m->item) / 2,
             m->item, COL_VALUE, ">");
}

/* a half-row status line (progress / result messages) */
void ui_status(const char *text, int kind)
{
    ui_metrics *m = &l_m;
    uint32_t col = kind > 0 ? UGL_RGBA(110, 225, 150, 255) : kind < 0 ? UGL_RGBA(255, 120, 120, 255) : COL_HINT;
    int y = m->panel_y + FONT_GLYPH_H * m->item + 2 * m->pad + m->pad + l_yoff;
    int scale = m->item > 1 ? m->item - 1 : 1;
    l_yoff += m->row_h / 2;
    if (text != NULL && text[0] != 0)
        ugl_text(m->panel_x + (m->panel_w - ugl_text_width(scale, text)) / 2, y + (m->row_h / 2 - FONT_GLYPH_H * scale) / 2, scale, col, text);
}

/* ---- scrolling lists, value rows, choices ------------------------------------------------------ */

/* A panel that shows `want_rows` rows of a longer list (fewer if the screen is too small) and scrolls to keep the
 * focused row in view.  with_status reserves a half row under the list for ui_status_bar(). */
static int l_next_width;

void ui_next_panel_width(int percent) { l_next_width = percent; }

void ui_panel_begin_scroll(const char *title, int want_rows, int with_status)
{
    ui_metrics *m = &l_m;
    int vis = want_rows, extra = with_status ? 1 : 0, fr, total = l_rows_total;
    int width = l_next_width > 0 ? l_next_width : 64;

    l_next_width = 0;
    while (vis > 4 && panel_height_for(m, vis * 2 + extra) > m->h - 2 * m->pad)
        vis--;
    ui_panel_begin_ex(title, vis * 2 + extra, width);
    l_scroll_on = 1;
    l_row = 0;
    l_vis = vis;
    if (with_status)
    {
        int title_h = FONT_GLYPH_H * m->item + 2 * m->pad;
        l_status_y = m->panel_y + title_h + m->pad + vis * m->row_h;
    }
    fr = (l_focus >= 0 && l_focus < UI_MAX_IDS) ? l_id_row[l_focus] : 0;
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

/* a row with a value on the right; returns 1 when A is pressed on it, 2 for X (the "clear" action) */
int ui_item(const char *label, const char *value)
{
    int y, id = next_focusable(&y);
    int focused = (id == l_focus);
    draw_row(y, focused, label, value);
    if (!focused)
        return 0;
    if (l_activate)
        return 1;
    if (l_pressed & PADBIT(PAD_X))
        return 2;
    return 0;
}

/* choose one of several options with Left/Right (A steps forward); returns 1 when the choice changed */
int ui_choice(const char *label, int *idx, const char *const *names, int n)
{
    int y, id = next_focusable(&y);
    int focused = (id == l_focus);
    int changed = 0;
    char buf[64];

    if (n <= 0)
        return 0;
    if (*idx < 0 || *idx >= n)
        *idx = 0;
    if (focused && (l_activate || l_adjust != 0))
    {
        *idx = (*idx + (l_adjust != 0 ? l_adjust : 1) + n) % n;
        changed = 1;
    }
    snprintf(buf, sizeof(buf), "%s%s%s", focused ? "< " : "", names[*idx], focused ? " >" : "");
    draw_row(y, focused, label, buf);
    return changed;
}

int ui_step(const char *label, const char *text)
{
    int y, id = next_focusable(&y);
    int focused = (id == l_focus);
    int dir = 0;
    char buf[64];

    if (focused && (l_activate || l_adjust != 0))
        dir = l_adjust != 0 ? l_adjust : 1;
    snprintf(buf, sizeof(buf), "%s%s%s", focused ? "< " : "", text, focused ? " >" : "");
    draw_row(y, focused, label, buf);
    return dir;
}

void ui_mark_last(int kind)
{
    ui_metrics *m = &l_m;
    int w = m->body * 2 + 2;
    if (l_last_y < 0 || kind <= 0)
        return;
    ugl_rect(m->panel_x + m->pad + 1, l_last_y + 1, w, m->row_h - m->pad / 2 - 2,
             kind == 1 ? UGL_RGBA(110, 225, 150, 255) : UGL_RGBA(120, 130, 90, 255));
}

int ui_x_last(void)
{
    return l_last_focused && (l_pressed & PADBIT(PAD_X));
}

/* status line at the bottom of a scrolling panel (needs with_status) */
void ui_status_bar(const char *text, int kind)
{
    ui_metrics *m = &l_m;
    uint32_t col = kind > 0 ? UGL_RGBA(110, 225, 150, 255) : kind < 0 ? UGL_RGBA(255, 120, 120, 255) : COL_HINT;
    int scale = m->item > 1 ? m->item - 1 : 1;
    char buf[96];
    if (l_status_y < 0 || text == NULL || text[0] == 0)
        return;
    fit_text(buf, sizeof(buf), text, scale, m->panel_w - 4 * m->pad);
    ugl_text(m->panel_x + (m->panel_w - ugl_text_width(scale, buf)) / 2,
             l_status_y + (m->row_h / 2 - FONT_GLYPH_H * scale) / 2, scale, col, buf);
}

/* non-focusable row with a horizontal bar, permille 0..1000 */
void ui_progress(int permille)
{
    ui_metrics *m = &l_m;
    int y = row_y(), x, w, h;
    if (y < 0)
        return;
    if (permille < 0) permille = 0;
    if (permille > 1000) permille = 1000;
    x = m->panel_x + 3 * m->pad;
    w = m->panel_w - 6 * m->pad;
    h = m->row_h / 3;
    y += (m->row_h - h) / 2;
    ugl_rect(x, y, w, h, UGL_RGBA(30, 38, 56, 255));
    ugl_rect(x, y, w * permille / 1000, h, COL_FOCUS_BG);
}

/* centered text in the value colour (the thing being asked for) */
void ui_label_hi(const char *text)
{
    ui_metrics *m = &l_m;
    int y = row_y();
    char buf[96];
    if (y < 0)
        return;
    fit_text(buf, sizeof(buf), text, m->item, m->panel_w - 4 * m->pad);
    ugl_text(m->panel_x + (m->panel_w - ugl_text_width(m->item, buf)) / 2, y + m->pad, m->item, COL_VALUE, buf);
}
