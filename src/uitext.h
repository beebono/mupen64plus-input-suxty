/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - uitext.h
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

#ifndef UITEXT_H
#define UITEXT_H

#include <stddef.h>
#include <stdint.h>

typedef int (*uitext_measure)(void *ctx, const char *s);

/* at least one character stays */
void uitext_fit(char *buf, size_t size, const char *src, int avail, uitext_measure m, void *ctx);

/* a '\n' ends the first line; else breaks at the last space within `avail` (at the last fitting character when none);
 * line 2 is cut with ".." */
int  uitext_wrap2(const char *src, int avail, uitext_measure m, void *ctx, char *line1, char *line2, size_t size);

/* unpressed arrows are blanks, so every arrow keeps its place */
#define UITEXT_ARROWS_LEN 7
#define UITEXT_ARROWS_ALL "\x02 \x01 \x03 \x04"
void uitext_arrows(int up, int down, int left, int right, char out[UITEXT_ARROWS_LEN + 1]);

/* `ghost` gets all the names (dimmed); `on` has the same width with only the pressed ones, blanks elsewhere, so
 * nothing shifts */
typedef struct {
    uint32_t    bit;
    const char *name;
} uitext_btn;
int  uitext_btn_cells(const uitext_btn *tab, int n, uint32_t mask, char *ghost, char *on, size_t size);

void uitext_remap_msg(char *out, size_t size, const char *control, const char *target, const char *was);

#endif /* UITEXT_H */
