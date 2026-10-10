/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - debug.h
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


/* Test hooks.  There is no way to press buttons on a headless test rig, so the session can be driven by a script
 * through environment variables (read once, at InitiateControllers):
 *
 *   SUXTY_DEBUG_SCRIPT="wait300,menu,wait30,shot,down,wait10,shot,b,wait20,quit"
 *   SUXTY_DEBUG_SHOT_PREFIX=/path/prefix        (writes prefix-1.png, prefix-2.png ...)
 *   SUXTY_DEBUG_OPEN_AFTER=N                    (shorthand for "wait N,menu" first)
 *
 * Tokens (comma/space separated), executed one per "tick" (a tick is one game
 * frame presented while playing, or one menu frame while the menu is open):
 *   waitN           do nothing for N ticks
 *   menu            toggle the menu (same as the Select+Start combo)
 *   shot            save the composited back buffer (game frame, or the menu overlay) as PNG
 *   quit            quit like the menu does (saves the auto slot first if enabled)
 *   kill            stop emulation immediately
 *   stat            log the frame counter and clock (to measure frame rates between two stats)
 *   up down left right a b x y l r start select
 *                   press that button for one tick (then release for one tick)
 *   select+r        press several buttons together (e.g. a hotkey combo) for one tick
 *   holdN:select+y  hold the buttons for N ticks (holdN@K:b also takes a screenshot on tick K of the hold)
 *   probe:a+b+cup+shift+tilt+sx60+sy-80   make the Test controller page show these N64 buttons / stick values whatever the pad
 *                   does (layout checks); probe:off ends it
 *   pjsave:N pjzip:N  write a Project64 state (uncompressed / zip) to slot N's .pjN file with the core itself (to test
 *                   that slots which only exist as PJ64 states are shown and loaded)
 *   cap:button:3    answer the next control-capture prompt with that physical control (button:N, hat:N:up,
 *   cap:axis:2+     axis:N+ / axis:N-), so the remap UI can be driven without a real pad
 */

#ifndef DEBUG_H
#define DEBUG_H

#include <stdint.h>

#include "pad.h"
#include "padmap.h"

typedef struct {
    uint32_t buttons;     /* virtual buttons held this tick */
    int      toggle_menu;
    int      shot;
    int      quit;
    int      kill;
    int      pj_slot;      /* >= 0: save a PJ64 state to this slot ... */
    int      pj_zip;       /* ... zipped */
    int      stat;
    int      cap;          /* capsrc is valid: feed it to the capture prompt */
    padsrc   capsrc;
    int      probe_set;    /* probe_on / probe: override (or stop overriding) what the Test controller page shows */
    int      probe_on;
    n64_state probe;
} dbg_tick_t;

void dbg_configure(const char *script, const char *shot_prefix, int open_menu_after);
int  dbg_active(void);
void dbg_tick(dbg_tick_t *out);

/* PNG writer (uncompressed deflate, so no zlib dependency).  rgba is bottom-up as read from GL. */
int  dbg_save_png(const char *path, const uint8_t *rgba_bottom_up, int w, int h);
/* next "<prefix>-N.png" name; returns NULL if no prefix was configured */
const char *dbg_next_shot_path(char *buf, unsigned size);

#endif /* DEBUG_H */
