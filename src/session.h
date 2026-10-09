/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-ui-suxty - session.h
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


/* The in-game session: polls the pad for the menu combo and hotkeys and runs the menu.
 *
 * THREADING MODEL (read this before changing anything)
 * ----------------------------------------------------
 * M64CMD_EXECUTE runs the emulator on the calling thread.  The video plugin's frame presentation calls
 * VidExt_GL_SwapBuffers -> video.c -> session_before_swap() on that same thread, once per presented frame, with the
 * game GL context current.  GetKeys() (plugin_api.c) runs on that thread too.  So:
 *
 *  - Detecting the menu combo is a cheap poll of controller *state* in session_before_swap().
 *  - When the combo fires, the menu runs as a nested loop right there, inside the swap call, on the emulation thread.
 *    The core is simply "stopped in the middle of presenting a frame", which is the pause: no thread hand-over, no
 *    locking.  We still send M64CMD_PAUSE / M64CMD_RESUME so the core's state (and any plugin that watches
 *    M64CORE_EMU_STATE) agrees, and so a plugin that renders on its own thread stops getting work.  A swap arriving
 *    on a foreign thread while the menu is open is ignored (session_menu_blocks_this_thread()).
 *  - The video plugin's own state may be changed from the menu for the same reason: foir's live options are set from
 *    inside its buffer swap, between frames, with its workers drained (foir_if.h).
 *  - Core commands that the core only executes from its interrupt handler (M64CMD_STATE_SAVE / _LOAD, M64CMD_RESET)
 *    are queued by the core and run once the emulator runs again.  The menu therefore *returns an action* and closes
 *    first; a save is then followed by watching the state file (fire and forget, see session.c).
 *  - Hotkeys, toasts, fast-forward: also evaluated in the frame hook.  Toasts are drawn over the finished game frame
 *    in the private overlay context just before the swap.
 *  - Hiding hotkey / menu buttons from the game is done by inputfilter.c, which GetKeys() reads through.
 */

#ifndef SESSION_H
#define SESSION_H

#include "m64p_types.h"

int  session_init(void);
void session_shutdown(void);

/* Called from video.c on the thread that presents frames. Returns 1 if the session consumed the
 * swap (it ran the menu and presented), 0 if the caller should swap normally. */
int  session_before_swap(void);
/* 1 if a menu is open and the calling thread is not the one running it. */
int  session_menu_blocks_this_thread(void);

#endif /* SESSION_H */
