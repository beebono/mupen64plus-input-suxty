/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - gl.h
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


/* entry points come from SDL_GL_GetProcAddress, so suxty links SDL2 only */

#ifndef UGL_H
#define UGL_H

#include <stdint.h>

#define UGL_RGBA(r, g, b, a) ((((uint32_t) (r)) << 24) | (((uint32_t) (g)) << 16) | (((uint32_t) (b)) << 8) | ((uint32_t) (a)))

int  ugl_load_functions(void);
/* must run with the overlay context current */
int  ugl_init_resources(void);
void ugl_shutdown_resources(void);

int  ugl_capture_frame(int w, int h);
const uint8_t *ugl_capture_data(int *w, int *h);   /* RGBA, bottom-up, alpha forced to 255 */

void ugl_background_upload(void);
void ugl_begin(int w, int h);
void ugl_begin_overlay(int w, int h);              /* no clear */
void ugl_end(void);
void ugl_background_draw(int w, int h, float brightness);
void ugl_rect(int x, int y, int w, int h, uint32_t rgba);
typedef enum { UGL_FONT_ITEM = 0, UGL_FONT_HINT = 1 } ugl_font;
void ugl_text(int x, int y, int scale, uint32_t rgba, const char *str);
int  ugl_text_width(int scale, const char *str);
void ugl_text_f(ugl_font font, int x, int y, int scale, uint32_t rgba, const char *str);
int  ugl_text_width_f(ugl_font font, int scale, const char *str);
int  ugl_glyph_h(ugl_font font);
/* overlay context, after drawing, before swap; buffer owned by gl.c */
const uint8_t *ugl_readback(int w, int h);

#endif /* UGL_H */
