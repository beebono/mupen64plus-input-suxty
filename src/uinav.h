/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - uinav.h
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

/* D-pad navigation over a grid of focusable cells: a pure function (no drawing, no state of its own), so the rules can be
 * unit-tested (tools/test_uinav.c).
 *
 * The page's focusable widgets are numbered in call order.  Each has a row, a first column and a span of columns: the cells
 * of a "Save | Load" row have span 1, a full-width row has span = the grid's column count (a plain list is a one-column grid).
 *   Up / Down    next / previous row, wrapping.  In the new row the cell under the remembered column is chosen, so a trip through
 *                a full-width row comes back to the column it left from.
 *   Left / Right the next cell in the same row, wrapping within the row; a row with a single cell does not move (the widget
 *                itself uses Left / Right then: a slider, a choice, the slot of "In Slot").
 */

#ifndef UINAV_H
#define UINAV_H

typedef struct {
    int row;        /* rows are numbered in call order; cells of one row share the number */
    int col;        /* first column */
    int span;       /* columns covered (>= 1) */
} ui_navcell;

enum { UI_NAV_UP, UI_NAV_DOWN, UI_NAV_LEFT, UI_NAV_RIGHT };

/* Returns the new focus (== cur when nothing moves).  *want_col is the remembered column: read for Up / Down, updated when
 * the new cell is a single column cell. */
int uinav_move(const ui_navcell *cells, int n, int cur, int dir, int *want_col);

#endif /* UINAV_H */
