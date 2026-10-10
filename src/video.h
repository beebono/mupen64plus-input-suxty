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


/* the game context is the plugin's; the overlay context is private to suxty, so the menu never disturbs the plugin's
 * GL state */

#ifndef VIDEO_H
#define VIDEO_H

#include "m64p_types.h"

m64p_error video_install(void);
void video_uninstall(void);

int  video_ui_available(void);
void video_drawable_size(int *w, int *h);
int  video_enter_ui(void);
void video_leave_ui(void);
void video_present(void);
void video_set_unthrottled(int unthrottled);    /* swap interval 0 while fast-forwarding */

#endif /* VIDEO_H */
