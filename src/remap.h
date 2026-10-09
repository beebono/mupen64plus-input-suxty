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

/* The live controller remap layer: which physical pad drives which N64 player, with which padmap.
 *
 * suxty IS the input plugin, so there is nothing to configure in another plugin and no SDL joystick call is
 * intercepted: GetKeys() asks remap_player_state() for the N64 state that the player's mapping makes of the (hotkey
 * filtered, see inputfilter.h) pad.  A change of a mapping, a preset, the deadzone or the player's pad is therefore
 * live: the next poll sees it.  Two layers: the global mapping (suxty.cfg) and the game's own (games/<md5>.cfg).
 */

#ifndef REMAP_H
#define REMAP_H

#include <stddef.h>

#include <SDL.h>

#include "padmap.h"

#define REMAP_PLAYERS 4

int  remap_init(void);                    /* after pad_init() and cfg_init() */
void remap_shutdown(void);
int  remap_enabled(void);                 /* at least one player has a pad */

void remap_prepare_launch(void);                    /* InitiateControllers: who plays */
void remap_finish(void);                            /* RomClosed / shutdown: write pending changes */

/* GetKeys, on the emulation thread. */
void remap_update(void);                            /* after every SDL_JoystickUpdate: evaluate all players */
int  remap_player_state(int player, n64_state *out);   /* 1 = the player has a pad */

/* Menu API.  Two layers (M4): the global mapping/choices and the game's own.  remap_get() shows the layer of the
 * current scope (g_cfg.scope); remap_edit() + remap_commit() change and persist it (suxty.cfg, or the game file in
 * GAME scope, where the game starts from a copy of the global mapping).  What the game plays with is the game's layer
 * if it has one. */
const padmap *remap_get(int player);
padmap       *remap_edit(int player);               /* then remap_commit() */
void          remap_commit(int player);             /* persist */
int           remap_player_want_overridden(int player);   /* the game file chooses this player's pad */
int           remap_pad_overridden(int player);     /* the game file has its own mapping for this player's pad */
int           remap_reset_game(void);               /* drop all of the game's controller choices/mappings; returns how many */
void          remap_flush(void);                    /* write pending changes */
int           remap_player_pad(int player);         /* pad.c index, or -1 */
int           remap_player_live(int player);        /* the player was present at launch (the core was told) */
const char   *remap_player_want(int player);        /* "" auto, "none", or a pad name */
/* Chooses the pad of a player ("" auto, "none", pad name).  Returns 1 = effective now, 0 = from the next launch. */
int           remap_set_pad(int player, const char *want);
int           remap_apply_preset(int player, int preset, char *msg, size_t msgsize);
/* the N64 state this player's mapping makes of the real, unfiltered pad (the "test" page) */
void          remap_probe(int player, n64_state *out);

#endif /* REMAP_H */
