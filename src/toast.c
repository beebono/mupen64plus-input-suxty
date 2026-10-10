/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - toast.c
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

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <SDL.h>

#include "font.h"
#include "gl.h"
#include "toast.h"
#include "ui.h"

#define COL_BG      UGL_RGBA(10, 14, 22, 225)
#define FADE_MS     300
#define NOTICE_MIN_MS 2500          /* in the menu a notice is the only feedback of what was just done: it stays at least this long */

static int      l_enabled = 1;
static int      l_duration = 1800;
static char     l_text[96];
static toast_kind l_kind;
static uint32_t l_until;           /* SDL_GetTicks() deadline; 0 = none */
static char     l_badge[24];
static int      l_menu_open;
static char     l_note[96];
static toast_kind l_note_kind;
static uint32_t l_note_until;      /* the menu's notice (see toast_set_menu_open()) */

void toast_set_enabled(int enabled)     { l_enabled = enabled; if (!enabled) l_until = 0; }
void toast_set_duration_ms(int ms)      { l_duration = ms < 300 ? 300 : ms > 10000 ? 10000 : ms; }
void toast_clear(void)                  { l_until = 0; l_note_until = 0; }

void toast_set_menu_open(int open)
{
    l_menu_open = open;
    l_note_until = 0;
}

int toast_menu_notice(char *out, size_t size, int *kind)
{
    if (l_note_until == 0 || (int32_t) (l_note_until - SDL_GetTicks()) <= 0)
    {
        l_note_until = 0;
        return 0;
    }
    snprintf(out, size, "%s", l_note);
    *kind = l_note_kind == TOAST_OK ? 1 : l_note_kind == TOAST_ERROR ? -1 : 0;
    return 1;
}

void toast_show(toast_kind kind, const char *fmt, ...)
{
    va_list ap;
    if (!l_enabled && kind != TOAST_ERROR)
        return;
    if (l_menu_open)
    {
        uint32_t ms = (uint32_t) (l_duration < NOTICE_MIN_MS ? NOTICE_MIN_MS : l_duration) + (kind == TOAST_ERROR ? 1200u : 0u);
        va_start(ap, fmt);
        vsnprintf(l_note, sizeof(l_note), fmt, ap);
        va_end(ap);
        l_note_kind = kind;
        l_note_until = SDL_GetTicks() + ms;
        if (l_note_until == 0)
            l_note_until = 1;
        return;
    }
    va_start(ap, fmt);
    vsnprintf(l_text, sizeof(l_text), fmt, ap);
    va_end(ap);
    l_kind = kind;
    l_until = SDL_GetTicks() + (uint32_t) l_duration + (kind == TOAST_ERROR ? 1200u : 0u);
    if (l_until == 0)
        l_until = 1;
}

void toast_set_badge(const char *text)
{
    snprintf(l_badge, sizeof(l_badge), "%s", text ? text : "");
}

static int toast_remaining(void)
{
    if (l_until == 0)
        return 0;
    return (int32_t) (l_until - SDL_GetTicks());
}

int toast_visible(void)
{
    if (l_until != 0 && toast_remaining() <= 0)
        l_until = 0;
    return l_until != 0 || (l_enabled && l_badge[0] != 0);
}

static void box(int x, int y, int w, int h, int border, uint32_t edge, uint32_t bg)
{
    ugl_rect(x - border, y - border, w + 2 * border, h + 2 * border, edge);
    ugl_rect(x, y, w, h, bg);
}

void toast_draw(int w, int h)
{
    ui_metrics m;
    int scale, pad, margin, border;

    ui_metrics_for(&m, w, h);
    scale = m.body > 1 ? m.body : 1;                   /* toasts are a notch smaller than menu items */
    if (m.body == 1 && m.item == 2)
        scale = 2;                                     /* 480p: the 8x16 font at 1x is too small to read from a couch */
    pad = 3 * m.body;
    margin = h / 24 + pad;
    border = m.body;

    if (l_until != 0)
    {
        int rem = toast_remaining();
        if (rem <= 0)
        {
            l_until = 0;
        }
        else
        {
            /* '\n' starts another line (long notices); the lines are left-aligned in one box */
            char lines[3][96];
            int nl = 0, tw = 0, i, lh = FONT_GLYPH_H * scale;
            const char *p = l_text;
            while (nl < 3)
            {
                const char *e = strchr(p, '\n');
                size_t n = e != NULL ? (size_t) (e - p) : strlen(p);
                if (n >= sizeof(lines[0]))
                    n = sizeof(lines[0]) - 1;
                memcpy(lines[nl], p, n);
                lines[nl][n] = 0;
                if (ugl_text_width(scale, lines[nl]) > tw)
                    tw = ugl_text_width(scale, lines[nl]);
                nl++;
                if (e == NULL)
                    break;
                p = e + 1;
            }
            int bw = tw + 4 * pad, bh = nl * lh + (nl - 1) * pad + 2 * pad;
            int x = (w - bw) / 2, y = margin;
            uint32_t edge, fg;
            int alpha = rem < FADE_MS ? rem * 255 / FADE_MS : 255;
            switch (l_kind)
            {
                case TOAST_OK:    edge = UGL_RGBA(70, 200, 120, alpha); fg = UGL_RGBA(200, 255, 220, alpha); break;
                case TOAST_ERROR: edge = UGL_RGBA(230, 80, 80, alpha);  fg = UGL_RGBA(255, 215, 215, alpha); break;
                default:          edge = UGL_RGBA(70, 130, 230, alpha); fg = UGL_RGBA(240, 244, 255, alpha); break;
            }
            box(x, y, bw, bh, border, edge, (COL_BG & ~0xffu) | (uint32_t) (225 * alpha / 255));
            for (i = 0; i < nl; i++)
                ugl_text(x + 2 * pad, y + pad + i * (lh + pad), scale, fg, lines[i]);
        }
    }
    if (l_enabled && l_badge[0] != 0)
    {
        int tw = ugl_text_width(scale, l_badge);
        int bw = tw + 4 * pad, bh = FONT_GLYPH_H * scale + 2 * pad;
        int x = w - bw - margin, y = margin;
        box(x, y, bw, bh, border, UGL_RGBA(255, 190, 60, 255), COL_BG);
        ugl_text(x + 2 * pad, y + pad, scale, UGL_RGBA(255, 214, 102, 255), l_badge);
    }
}
