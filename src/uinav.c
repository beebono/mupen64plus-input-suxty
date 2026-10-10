/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - uinav.c
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

#include <stddef.h>

#include "uinav.h"

/* the cell of row `row` that covers column `want` (else the nearest one) */
static int cell_in_row(const ui_navcell *c, int n, int row, int want)
{
    int i, best = -1, bestd = 1 << 30;

    for (i = 0; i < n; i++)
    {
        int d;
        if (c[i].row != row)
            continue;
        if (want >= c[i].col && want < c[i].col + c[i].span)
            return i;
        d = want < c[i].col ? c[i].col - want : want - (c[i].col + c[i].span - 1);
        if (d < bestd)
        {
            bestd = d;
            best = i;
        }
    }
    return best;
}

int uinav_move(const ui_navcell *c, int n, int cur, int dir, int *want_col)
{
    int i, target = cur;

    if (n <= 0)
        return 0;
    if (cur < 0 || cur >= n)
        return cur < 0 ? 0 : n - 1;

    if (dir == UI_NAV_UP || dir == UI_NAV_DOWN)
    {
        /* the neighbouring row in that direction, wrapping to the other end of the page */
        int row = c[cur].row, next = row, lo = row, hi = row, found = 0;
        for (i = 0; i < n; i++)
        {
            if (c[i].row < lo) lo = c[i].row;
            if (c[i].row > hi) hi = c[i].row;
            if (dir == UI_NAV_DOWN && c[i].row > row && (!found || c[i].row < next)) { next = c[i].row; found = 1; }
            if (dir == UI_NAV_UP && c[i].row < row && (!found || c[i].row > next)) { next = c[i].row; found = 1; }
        }
        if (!found)
            next = dir == UI_NAV_DOWN ? lo : hi;
        if (next != row)
        {
            int want = want_col != NULL ? *want_col : 0;
            target = cell_in_row(c, n, next, want);
            if (target < 0)
                target = cur;
        }
    }
    else
    {
        /* the neighbour in the same row (cells are in call order, so that is the next / previous index of that row) */
        int step = dir == UI_NAV_RIGHT ? 1 : -1, first = -1, last = -1;
        for (i = 0; i < n; i++)
            if (c[i].row == c[cur].row)
            {
                if (first < 0) first = i;
                last = i;
            }
        if (first != last)
        {
            target = cur + step;
            if (target > last) target = first;
            if (target < first) target = last;
        }
    }
    if (want_col != NULL && c[target].span == 1)
        *want_col = c[target].col;
    return target;
}
