/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - session.h
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


/* Threading: session_before_swap(), GetKeys() and the menu all run on the M64CMD_EXECUTE thread. The menu is a nested
 * loop
 * inside the swap call: that is the pause (no hand-over, no locks). M64CMD_PAUSE / RESUME are still sent so the core
 * state and
 * threaded plugins agree. STATE_SAVE / LOAD / RESET run from the core's interrupt handler once the emulator runs
 * again, so the
 * menu returns an action and closes first. A swap on a foreign thread while the menu is open is ignored. */

#ifndef SESSION_H
#define SESSION_H

#include "m64p_types.h"

int  session_init(void);
void session_shutdown(void);

/* returns 1 if the session consumed the swap (ran the menu and presented), 0 if the caller should swap normally */
int  session_before_swap(void);
int  session_menu_blocks_this_thread(void);

#endif /* SESSION_H */
