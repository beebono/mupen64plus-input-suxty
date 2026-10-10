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

/* Text formatting for the menu, as pure functions (no drawing, no state): the width of a text comes in through a callback, so
 * the rules can be unit-tested (tools/test_uitext.c).
 *   - cutting a text with ".." and wrapping a status message to two lines,
 *   - the Test controller's cells: the four arrows of the D-pad / C group and the N64 buttons, each in a fixed slot,
 *   - the "South -> A (was B)" message after a control was captured.
 * The arrows are the font's control characters (font.h): FONT_CH_UP / DOWN / LEFT / RIGHT. */

#ifndef UITEXT_H
#define UITEXT_H

#include <stddef.h>
#include <stdint.h>

/* the width in pixels of `s` (ctx is whatever the caller needs to measure) */
typedef int (*uitext_measure)(void *ctx, const char *s);

/* `src` in `buf`, cut short with ".." until it is at most `avail` wide (at least one character stays) */
void uitext_fit(char *buf, size_t size, const char *src, int avail, uitext_measure m, void *ctx);

/* A message for a two line status area.  A '\n' in `src` ends the first line (the toasts do the same).  Else a text that is
 * wider than `avail` breaks at the last space that leaves the first line within `avail` (at the last fitting character when it
 * has no space); the second line is cut with ".." if it is still too wide.  Returns the number of lines (1 or 2); `line2` is
 * empty for 1.  `size` is the size of both buffers. */
int  uitext_wrap2(const char *src, int avail, uitext_measure m, void *ctx, char *line1, char *line2, size_t size);

/* The arrows of a D-pad or C group, always UITEXT_ARROWS_LEN characters in the order up, down, left, right with a space between:
 * a pressed direction shows its arrow, the others a blank (so every arrow keeps its place).  UITEXT_ARROWS_ALL is the same with
 * all four, drawn dimmed under it. */
#define UITEXT_ARROWS_LEN 7
#define UITEXT_ARROWS_ALL "\x02 \x01 \x03 \x04"
void uitext_arrows(int up, int down, int left, int right, char out[UITEXT_ARROWS_LEN + 1]);

/* The N64 buttons of the Test controller, always all of them in the order of the table, joined by spaces.  `ghost` gets all the
 * names (drawn dimmed); `on` has the same width and the names of the buttons whose bit is set in `mask` in their places, blanks
 * elsewhere (drawn lit over the ghost), so a button keeps its place and nothing shifts as buttons are pressed or released.
 * Returns how many buttons are pressed.  `size` is the size of both buffers; the names are never cut. */
typedef struct {
    uint32_t    bit;
    const char *name;
} uitext_btn;
int  uitext_btn_cells(const uitext_btn *tab, int n, uint32_t mask, char *ghost, char *on, size_t size);

/* "South -> A (was B)" (the arrow is FONT_CH_RIGHT); `was` NULL or empty: no parenthesis */
void uitext_remap_msg(char *out, size_t size, const char *control, const char *target, const char *was);

#endif /* UITEXT_H */
