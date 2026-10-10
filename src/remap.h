/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - remap.h
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

/* GetKeys() asks remap_player_state() for the N64 state the player's mapping makes of the hotkey-filtered pad, so any
 * change is live on the next poll */

#ifndef REMAP_H
#define REMAP_H

#include <stddef.h>

#include <SDL.h>

#include "padmap.h"

#define REMAP_PLAYERS 4

int  remap_init(void);
void remap_shutdown(void);
int  remap_enabled(void);

void remap_prepare_launch(void);
void remap_finish(void);

void remap_update(void);
int  remap_player_state(int player, n64_state *out);

const padmap *remap_get(int player);
padmap       *remap_edit(int player);               /* then remap_commit() */
void          remap_commit(int player);
int           remap_player_want_overridden(int player);
int           remap_pad_overridden(int player);
int           remap_reset_game(void);
void          remap_flush(void);
int           remap_player_pad(int player);         /* pad.c index, or -1 */
int           remap_player_live(int player);
const char   *remap_player_want(int player);
/* returns 1 = effective now, 0 = from the next launch */
int           remap_set_pad(int player, const char *want);
/* deadzone / range are kept; 0 ok, -1 with msg */
int           remap_default_mapping(int player, char *msg, size_t msgsize);
int           remap_pin_game(void);
/* of the real, unfiltered pad (the "test" page) */
void          remap_probe(int player, n64_state *out);
void          remap_set_probe_override(int on, const n64_state *st);

#endif /* REMAP_H */
