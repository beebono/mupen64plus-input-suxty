/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - font.h
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

#ifndef FONT_H
#define FONT_H

#define FONT_GLYPH_W   8
#define FONT_GLYPH_H   16
#define FONT_ATLAS_COLS 16
#define FONT_ATLAS_W   128
#define FONT_ATLAS_H   128
#define FONT_FIRST_CHAR 32
#define FONT_LAST_CHAR  126
/* arrow glyphs sit after '~'; text reaches them as these control characters */
#define FONT_CH_DOWN    "\x01"
#define FONT_CH_UP      "\x02"
#define FONT_CH_LEFT    "\x03"
#define FONT_CH_RIGHT   "\x04"
#define FONT_CELL_DOWN  95
#define FONT_CELL_UP    96
#define FONT_CELL_LEFT  97
#define FONT_CELL_RIGHT 98
/* solid white, for filled rectangles */
#define FONT_SOLID_CELL 127

extern const unsigned char font_atlas_alpha[FONT_ATLAS_W * FONT_ATLAS_H];

#define FONT2_GLYPH_W   12
#define FONT2_GLYPH_H   24
#define FONT2_ATLAS_W   192
#define FONT2_ATLAS_H   192
extern const unsigned char font_atlas_alpha_12x24[FONT2_ATLAS_W * FONT2_ATLAS_H];

#endif /* FONT_H */
