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


/* env, read once: SUXTY_DEBUG_SCRIPT (comma-separated tokens, one per tick), SUXTY_DEBUG_SHOT_PREFIX,
 * SUXTY_DEBUG_OPEN_AFTER=N
 * tokens: waitN menu shot quit kill stat <button> a+b holdN[@K]:btns probe:btns|off pjsave:N pjzip:N
 * cap:button:N|hat:N:dir|axis:N+- */

#ifndef DEBUG_H
#define DEBUG_H

#include <stdint.h>

#include "pad.h"
#include "padmap.h"

typedef struct {
    uint32_t buttons;
    int      toggle_menu;
    int      shot;
    int      quit;
    int      kill;
    int      pj_slot;
    int      pj_zip;
    int      stat;
    int      cap;
    padsrc   capsrc;
    int      probe_set;
    int      probe_on;
    n64_state probe;
} dbg_tick_t;

void dbg_configure(const char *script, const char *shot_prefix, int open_menu_after);
int  dbg_active(void);
void dbg_tick(dbg_tick_t *out);

int  dbg_save_png(const char *path, const uint8_t *rgba_bottom_up, int w, int h);
const char *dbg_next_shot_path(char *buf, unsigned size);

#endif /* DEBUG_H */
