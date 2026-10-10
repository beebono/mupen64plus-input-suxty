/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - toast.h
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

/* Brief on-screen notices ("Saved slot 3", "Fast-forward on") drawn by suxty's overlay.
 *
 * toast_show() may be called from the emulation thread only.  The session draws toasts after the
 * game has rendered its frame and before the buffer swap, in suxty's private GL context (see
 * session_before_swap()), so the video plugin's GL state is never touched.
 */

#ifndef TOAST_H
#define TOAST_H

#include <stddef.h>

typedef enum { TOAST_INFO, TOAST_OK, TOAST_ERROR } toast_kind;

void toast_set_enabled(int enabled);
void toast_set_duration_ms(int ms);
void toast_show(toast_kind kind, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* persistent top-right badge (fast-forward); NULL or "" removes it */
void toast_set_badge(const char *text);

/* While the menu is open (session.c sets it), a toast does not float over the panel: it becomes a notice that the menu shows in
 * its status line (ui.h) for as long as it lives, and then the page's own status text is back.  Errors always show; the other
 * kinds only with Notices on, as before. */
void toast_set_menu_open(int open);
int  toast_menu_notice(char *out, size_t size, int *kind);    /* 1 = there is one: its text, and its kind as ui_status() takes it */

int  toast_visible(void);                        /* anything to draw right now? */
void toast_draw(int w, int h);                   /* call between ugl_begin_overlay() and ugl_end() */
void toast_clear(void);

#endif /* TOAST_H */
