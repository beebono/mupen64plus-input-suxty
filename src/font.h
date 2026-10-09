/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-ui-suxty - font.h
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


/* Embedded 8x16 bitmap font atlas (see tools/gen_font.py). */

#ifndef FONT_H
#define FONT_H

#define FONT_GLYPH_W   8
#define FONT_GLYPH_H   16
#define FONT_ATLAS_COLS 16
#define FONT_ATLAS_W   128
#define FONT_ATLAS_H   128
#define FONT_FIRST_CHAR 32
#define FONT_LAST_CHAR  126
/* the last cell of the atlas is solid white; used for filled rectangles */
#define FONT_SOLID_CELL 127

extern const unsigned char font_atlas_alpha[FONT_ATLAS_W * FONT_ATLAS_H];

#endif /* FONT_H */
