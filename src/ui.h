/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - ui.h
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
 *     ui_panel_begin_ex("Title", number_of_half_rows, UI_W_DIALOG);
 *     ui_row_begin(2);                          // two cells side by side
 *     if (ui_button("No")) ...;
 *     if (ui_button("Yes")) ...;
 *     ui_toggle("Notices", &value);
 *     ui_panel_end("A Select   B Back");
 *     ui_end();
 *
 * Focus is an index into the focusable widgets in call order.  D-pad up/down moves it by row (wrapping), left/right moves
 * between the cells of a row (uinav.h); A activates, left/right adjusts toggles, choices and sliders (rows with one cell).
 * Page code calls ui_focus_set() when it switches pages.
 */

#ifndef UI_H
#define UI_H

#include <stdint.h>

#include "gl.h"

/* The standard panel widths, in characters of item text, so that pages of one kind line up. */
#define UI_W_DIALOG 24      /* confirm dialogs, Test controller */
#define UI_W_LIST   26      /* Settings, Hotkeys */
#define UI_W_WIDE   28      /* main menu, Controls, Buttons, the capture prompts */
#define UI_W_FULL   36      /* Save States, FOIR Options */

typedef struct {
    int w, h;            /* drawable size in pixels */
    int body;            /* font scale for small text   */
    int item;            /* font scale for menu items   */
    int pad;             /* base padding unit in pixels */
    int row_h;
    ugl_font hint_font;  /* hint / footer text: the crisp 12x24 face at the smallest size, else the 8x16 one at `body` */
    int hint_scale;
    int hint_h;          /* its height in pixels */
    int panel_x, panel_y, panel_w, panel_h;
} ui_metrics;

void ui_metrics_for(ui_metrics *m, int w, int h);   /* exposed so tests can check the scaling table */

void ui_begin(int w, int h, uint32_t pressed);
void ui_end(void);
void ui_focus_set(int index);
int  ui_focus_get(void);
int  ui_scroll_get(void);                              /* first visible row of the current scrolling list */
void ui_focus_restore(int index, int top);             /* back to a page: its focus and scroll position */

void ui_panel_begin(const char *title, int rows);                         /* UI_W_DIALOG wide, `rows` whole rows */
void ui_panel_begin_ex(const char *title, int rows2, int width_chars);   /* body height in half rows */
void ui_panel_end(const char *hint);
void ui_panel_end_k(const char *hint, int kind);          /* the footer as a message: kind > 0 good, < 0 error */

/* Grid pages: ui_grid(n), called after the panel begins, says how many columns the page has (full-width rows then span all of them, for navigation);
 * ui_row_begin(n) makes the next n focusable widgets (buttons) cells of one row. */
void ui_grid(int cols);
void ui_row_begin(int n);

void ui_label(const char *text);                       /* non-focusable row, centred; cut with ".." rather than overflow */
int  ui_button(const char *label);                     /* 1 when activated this frame; left aligned, centred in a cell */
int  ui_button_dis(const char *label, int disabled);   /* dimmed and inert when disabled; 1 activated, -1 activated while disabled */
int  ui_button_center(const char *label);              /* a full-width row with its label centred */
int  ui_toggle(const char *label, int *value);         /* 1 when changed */

typedef struct {
    char title[24];      /* "Slot 3" */
    char l1[16];         /* date, or "Empty" */
    char l2[16];         /* time */
    char tag[8];         /* "PJ64": a small highlighted third line */
    int  empty;
} ui_card;
/* horizontal slot carousel (3 rows tall, focusable: Left / Right on it return -1 / +1, the page changes the slot) */
int  ui_carousel(const ui_card *cards, int count, int sel);
/* half-row status line; kind > 0 good, < 0 error, 0 neutral */
void ui_status(const char *text, int kind);

int  ui_slider(const char *label, int *value, int min, int max, int step, const char *suffix);

/* Scrolling list: shows `rows` rows (fewer on a small screen, then it scrolls and shows a slim scroll bar at the right edge) of
 * however many the page draws, and keeps the focus in view.  with_status reserves a line for ui_status_bar() under the list. */
void ui_panel_begin_scroll(const char *title, int rows, int with_status);
void ui_status_bar(const char *text, int kind);
/* the next ui_panel_begin_scroll() panel is this wide (characters) instead of UI_W_LIST */
void ui_next_panel_width(int chars);
/* Row with a value on the right.  Returns 1 when A is pressed on it, 2 when X is. */
int  ui_item(const char *label, const char *value);
/* One of several options, Left/Right changes it (A steps forward).  Returns 1 when it changed. */
int  ui_choice(const char *label, int *idx, const char *const *names, int n);
/* A row for a value that is stepped with Left/Right (A steps forward) and shown as text; returns -1 / 0 / +1. */
int  ui_step(const char *label, const char *text);
/* The same, but A is not a step: returns 1 on A (the page opens something), and Left / Right come back in *dir (-1 / 0 / +1). */
int  ui_stepper(const char *label, const char *text, int *dir);
/* Marks the row placed last: kind 1 = this game has its own value (green bar), 2 = ... but you are looking at the
 * global value (olive bar).  X on the last row ("reset to global"): */
void ui_mark_last(int kind);
int  ui_x_last(void);
int  ui_focus_mark(void);                                /* the mark kind (0, 1, 2) of the focused row, as set by ui_mark_last() */
void ui_progress(int permille);                          /* non-focusable bar row */
void ui_label_hi(const char *text);                      /* highlighted centered label */
void ui_text_cells(const char *const *texts, int n);     /* one non-focusable row of small texts side by side */
void ui_hold_bar(const char *text, int permille);        /* non-focusable row: text over a progress bar */

#endif /* UI_H */
