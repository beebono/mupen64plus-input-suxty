/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - video.h
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


/* Video extension override: suxty owns the SDL2 window, the GL context(s) and
 * the buffer swap.  Installed with CoreOverrideVidExt().
 *
 * Two GL contexts are created on the one window:
 *   game context     - what the video plugin renders with (profile/version chosen by the plugin)
 *   overlay context  - private to suxty, used only while the menu is open.  Because it is a
 *                      separate context, the menu can never disturb the plugin's GL state
 *                      (bound programs, buffers, FBOs, VAOs, blend state ...), and the plugin
 *                      resumes with its state exactly as it left it.
 */

#ifndef VIDEO_H
#define VIDEO_H

#include "m64p_types.h"

m64p_error video_install(void);                 /* CoreOverrideVidExt with our function table */
void video_uninstall(void);                     /* plugin shutdown: deactivate the override, close a window left open */

/* used by session.c */
int  video_ui_available(void);                  /* overlay context created and usable */
void video_drawable_size(int *w, int *h);
int  video_enter_ui(void);                      /* make the overlay context current on this thread */
void video_leave_ui(void);                      /* make the game context current again */
void video_present(void);                       /* swap the window's buffers */
void video_set_unthrottled(int unthrottled);    /* swap interval 0 while fast-forwarding; the plugin's setting otherwise */

#endif /* VIDEO_H */
