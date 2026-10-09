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


/* The in-game session: polls the pad for the menu combo and runs the menu.
 *
 * THREADING MODEL (read this before changing anything)
 * ----------------------------------------------------
 * M64CMD_EXECUTE runs the emulator on the calling thread (our main thread).  The
 * video plugin's frame presentation calls VidExt_GL_SwapBuffers -> video.c ->
 * session_before_swap() on that same thread, once per presented frame, with the
 * game GL context current.  So:
 *
 *  - Detecting the menu combo is a cheap poll of controller *state* in
 *    session_before_swap().  Nothing is consumed from the SDL event queue.
 *  - When the combo fires, the menu runs as a nested loop right there, inside the
 *    swap call, on the emulation thread.  The core is simply "stopped in the middle of
 *    presenting a frame", which is the pause: no thread hand-over, no locking.
 *    We still send M64CMD_PAUSE / M64CMD_RESUME so the core's state (and any plugin
 *    that watches M64CORE_EMU_STATE) agrees, and so a plugin that renders on its own
 *    thread stops getting work.
 *  - The core's own pause_loop() would call VidExt_GL_SwapBuffers once and then spin
 *    without swapping.  We never reach it, because the menu loop returns (and resumes)
 *    before the core's VI handler gets to pause_loop().  If a video plugin presents from
 *    a different thread than the emulation thread, any swap arriving on a foreign thread
 *    while the menu is open is ignored (session_menu_blocks_this_thread()).
 *  - Core commands that the core only executes from its interrupt handler (save/load
 *    state jobs, M64CMD_RESET) are queued by the core and run once the emulator runs
 *    again.  The menu therefore *returns an action* and closes first.  A save/load is then
 *    a "job" (session.c) that outlives the menu: the state callback
 *    (M64CORE_STATE_SAVECOMPLETE / LOADCOMPLETE, a save reports from the core's worker
 *    thread) only stores the result, and the next frame hook(s) on the emulation thread
 *    complete the job: a toast for hotkey saves/loads, and for a save started from the menu
 *    the menu re-opens on the carousel ("Saving slot N..." until the result is in).
 *  - Hotkeys, toasts, fast-forward: also evaluated in the frame hook.  Toasts are drawn over
 *    the finished game frame in the private overlay context just before the swap.
 *  - Hiding hotkey / menu buttons from the game is done by inputfilter.c (see there).
 *  - The N64 controller remap (M3) lives in remap.c / padmap.c; its SDL joystick overrides are in inputfilter.c, and its
 *    menu pages (menu_ctl.c) run inside this same nested menu loop, so mapping changes need no locking either.
 *
 * Input: suxty polls SDL_GameController state and never reads the event queue for pad
 * input; the input plugin keeps polling raw SDL_Joystick state.  Opening the same device
 * twice is reference-counted by SDL, so the two do not fight.
 */

#ifndef SESSION_H
#define SESSION_H

#include "m64p_types.h"

int  session_init(void);
void session_shutdown(void);
/* --resume: queue a load of the auto-save slot before the emulator starts.  1 = queued, 0 = no auto-save
 * exists, -1 = couldn't queue. */
int  session_resume(void);

/* Called from video.c on the thread that presents frames. Returns 1 if the session consumed the
 * swap (it ran the menu and presented), 0 if the caller should swap normally. */
int  session_before_swap(void);
/* 1 if a menu is open and the calling thread is not the one running it. */
int  session_menu_blocks_this_thread(void);

/* StateCallback / DebugCallback plumbing (any thread). */
void session_core_state(m64p_core_param param, int value);
void session_core_message(int level, const char *message);
/* 1 if the game was stopped to be started again (menu "Restart game": launch-time options changed).  main() then
 * execs itself with --resume once everything is shut down; the auto-save written on the way out brings you back. */
int  session_restart_requested(void);
/* Async-signal-safe: ask for a clean stop (SIGINT/SIGTERM). */
void session_request_stop(void);

#endif /* SESSION_H */
