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


/* Minimal GLES2-class 2D renderer for the suxty overlay.
 *
 * All raw GL calls live in gl.c.  Function pointers are fetched through
 * SDL_GL_GetProcAddress, so suxty links against SDL2 only (no libGLESv2 /
 * libEGL at link time) and works with both GLES2/3 and desktop compat
 * contexts.
 */

#ifndef UGL_H
#define UGL_H

#include <stdint.h>

#define UGL_RGBA(r, g, b, a) ((((uint32_t) (r)) << 24) | (((uint32_t) (g)) << 16) | (((uint32_t) (b)) << 8) | ((uint32_t) (a)))

/* Phase 1: resolve GL entry points.  Any current context will do. */
int  ugl_load_functions(void);
/* Phase 2: build shaders / textures.  Must run with the *overlay* context current. */
int  ugl_init_resources(void);
void ugl_shutdown_resources(void);

/* Game-context side: copy the currently displayed (default framebuffer) frame into
 * an internal CPU buffer, leaving the caller's GL state untouched.  Returns 0 on success. */
int  ugl_capture_frame(int w, int h);
const uint8_t *ugl_capture_data(int *w, int *h);   /* RGBA, bottom-up, alpha forced to 255 */

/* Overlay-context side */
void ugl_background_upload(void);                  /* upload last capture as the backdrop texture */
void ugl_begin(int w, int h);                      /* viewport + state + clear */
void ugl_begin_overlay(int w, int h);              /* same, but draws over whatever is in the back buffer (no clear) */
void ugl_end(void);                                /* flush batches */
void ugl_background_draw(int w, int h, float brightness);   /* full-screen frozen frame */
void ugl_rect(int x, int y, int w, int h, uint32_t rgba);
/* Two faces: UGL_FONT_ITEM is 8x16 (items, titles), UGL_FONT_HINT is 12x24 (hint / footer text).  Both are drawn at an
 * integer scale.  The control characters \x01 .. \x04 are a down, up, left and right arrow. */
typedef enum { UGL_FONT_ITEM = 0, UGL_FONT_HINT = 1 } ugl_font;
void ugl_text(int x, int y, int scale, uint32_t rgba, const char *str);              /* UGL_FONT_ITEM */
int  ugl_text_width(int scale, const char *str);
void ugl_text_f(ugl_font font, int x, int y, int scale, uint32_t rgba, const char *str);
int  ugl_text_width_f(ugl_font font, int scale, const char *str);
int  ugl_glyph_h(ugl_font font);                                                       /* unscaled cell height */
/* read the default framebuffer back (overlay context, after drawing, before swap).
 * Returned buffer is RGBA bottom-up, owned by gl.c. */
const uint8_t *ugl_readback(int w, int h);

#endif /* UGL_H */
