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


/* focus is an index into the focusable widgets in call order; page code calls ui_focus_set() when it switches pages */

#ifndef UI_H
#define UI_H

#include <stdint.h>

#include "gl.h"

#define UI_W_DIALOG 24
#define UI_W_LIST   26
#define UI_W_WIDE   28
#define UI_W_FULL   36

typedef struct {
    int w, h;
    int body;
    int item;
    int pad;
    int row_h;
    ugl_font hint_font;  /* hint / footer text: the crisp 12x24 face at the smallest size, else the 8x16 one at `body` */
    int hint_scale;
    int hint_h;
    int panel_x, panel_y, panel_w, panel_h;
} ui_metrics;

void ui_metrics_for(ui_metrics *m, int w, int h);

void ui_begin(int w, int h, uint32_t pressed);
void ui_end(void);
void ui_focus_set(int index);
int  ui_focus_get(void);
int  ui_scroll_get(void);
void ui_focus_restore(int index, int top);

void ui_panel_begin(const char *title, int rows);
void ui_panel_begin_ex(const char *title, int rows2, int width_chars);   /* body height in half rows */
void ui_panel_end(const char *hint);
void ui_next_panel_no_footer(void);
void ui_panel_end_k(const char *hint, int kind);          /* the footer as a message: kind > 0 good, < 0 error */

/* ui_grid(n), after the panel begins: the page's column count (full-width rows span all of them, for navigation).
 * ui_row_begin(n): the next n focusable widgets are cells of one row */
void ui_grid(int cols);
void ui_row_begin(int n);

void ui_label(const char *text);
int  ui_button(const char *label);
int  ui_button_dis(const char *label, int disabled);   /* 1 activated, -1 activated while disabled */
int  ui_button_center(const char *label);
int  ui_toggle(const char *label, int *value);
void ui_toggle_locked(const char *label, int value);
void ui_slider_locked(const char *label, int value, const char *suffix);

typedef struct {
    char title[24];
    char l1[16];
    char l2[16];
    char tag[8];
    int  empty;
} ui_card;
/* Left / Right on it return -1 / +1; the page changes the slot */
int  ui_carousel(const ui_card *cards, int count, int sel);
/* kind > 0 good, < 0 error, 0 neutral */
void ui_status(const char *text, int kind);

int  ui_slider(const char *label, int *value, int min, int max, int step, const char *suffix);

void ui_panel_begin_scroll(const char *title, int rows, int with_status);
void ui_status_bar(const char *text, int kind);
void ui_next_panel_width(int chars);
/* returns 1 when A is pressed on it, 2 when X is */
int  ui_item(const char *label, const char *value);
int  ui_choice(const char *label, int *idx, const char *const *names, int n);
/* returns -1 / 0 / +1 */
int  ui_step(const char *label, const char *text);
/* A is not a step: returns 1 on A, Left / Right come back in *dir */
int  ui_stepper(const char *label, const char *text, int *dir);
/* kind 1 = this game has its own value (green bar), 2 = but you are looking at the global value (olive bar) */
void ui_mark_last(int kind);
int  ui_x_last(void);
int  ui_focus_mark(void);
void ui_progress(int permille);
void ui_label_hi(const char *text);
typedef struct {
    const char *label;
    const char *ghost;
    const char *on;
} ui_cell;
void ui_cells(const ui_cell *cells, int n);
void ui_hold_bar(const char *text, int permille);

#endif /* UI_H */
