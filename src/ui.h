/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-ui-suxty - ui.h
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


/* Own-drawn immediate-mode widgets (no GUI toolkit).
 *
 * Usage per menu frame (all inside ugl_begin()/ugl_end()):
 *
 *     ui_begin(w, h, pad_pressed_mask);
 *     ui_panel_begin("Title", number_of_rows);
 *     if (ui_button("Resume")) ...;
 *     ui_toggle("Hints", &value);
 *     ui_slider("Dim", &value, 0, 90, 5, "%");
 *     ui_panel_end("A Select   B Back");
 *     ui_end();
 *
 * Focus is an index into the focusable widgets in call order.  D-pad up/down
 * moves it (wrapping), A activates, left/right adjusts toggles and sliders.
 * Page code calls ui_focus_set() when it switches pages.
 */

#ifndef UI_H
#define UI_H

#include <stdint.h>

typedef struct {
    int w, h;            /* drawable size in pixels */
    int body;            /* font scale for small text   */
    int item;            /* font scale for menu items   */
    int pad;             /* base padding unit in pixels */
    int row_h;
    int panel_x, panel_y, panel_w, panel_h;
} ui_metrics;

void ui_metrics_for(ui_metrics *m, int w, int h);   /* exposed so tests can check the scaling table */

void ui_begin(int w, int h, uint32_t pressed);
void ui_end(void);
void ui_focus_set(int index);
int  ui_focus_get(void);
int  ui_scroll_get(void);                              /* first visible row of the current scrolling list */
void ui_focus_restore(int index, int top);             /* back to a page: its focus and scroll position */
void ui_set_hints_visible(int visible);

void ui_panel_begin(const char *title, int rows);
void ui_panel_begin_ex(const char *title, int rows2, int width_percent);   /* body height in half rows */
void ui_panel_end(const char *hint);

void ui_label(const char *text);                       /* non-focusable row */
int  ui_button(const char *label);                     /* 1 when activated this frame */
int  ui_button_dis(const char *label, int disabled);   /* dimmed and inert when disabled; 1 activated, -1 activated while disabled */
int  ui_toggle(const char *label, int *value);         /* 1 when changed */

typedef struct {
    char title[24];      /* "Slot 3" */
    char l1[16];         /* date, or "Empty" */
    char l2[16];         /* time */
    char tag[8];         /* "PJ64": a small highlighted third line */
    int  empty;
} ui_card;
/* horizontal slot carousel (3 rows tall, not focusable: the page handles left/right itself) */
void ui_carousel(const ui_card *cards, int count, int sel);
/* half-row status line; kind > 0 good, < 0 error, 0 neutral */
void ui_status(const char *text, int kind);

int  ui_slider(const char *label, int *value, int min, int max, int step, const char *suffix);

/* Scrolling list: shows want_rows rows (fewer on a small screen) of however many the page draws, and keeps the
 * focus in view.  with_status reserves a line for ui_status_bar() under the list. */
void ui_panel_begin_scroll(const char *title, int want_rows, int with_status);
void ui_status_bar(const char *text, int kind);
/* the next ui_panel_begin_scroll() panel is this wide (percent of the screen) instead of the default */
void ui_next_panel_width(int percent);
/* Row with a value on the right.  Returns 1 when A is pressed on it, 2 when X is. */
int  ui_item(const char *label, const char *value);
/* One of several options, Left/Right changes it (A steps forward).  Returns 1 when it changed. */
int  ui_choice(const char *label, int *idx, const char *const *names, int n);
/* A row for a value that is stepped with Left/Right (A steps forward) and shown as text; returns -1 / 0 / +1. */
int  ui_step(const char *label, const char *text);
/* Marks the row placed last: kind 1 = this game has its own value (green bar), 2 = ... but you are looking at the
 * global value (dim bar).  X on the last row ("reset to global"): */
void ui_mark_last(int kind);
int  ui_x_last(void);
void ui_progress(int permille);                          /* non-focusable bar row */
void ui_label_hi(const char *text);                      /* highlighted centered label */

#endif /* UI_H */
