/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-ui-suxty - remap.h
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
 * HOW A REMAP TAKES EFFECT (and why)
 * ----------------------------------
 * The stock mupen64plus-input-sdl reads Input-SDL-Control1..4 once (PluginStartup / InitiateControllers) and
 * then only calls SDL_JoystickGetButton / GetAxis / GetHat.  It never re-reads its config while a game runs,
 * and it cannot express a modifier layer.  So suxty does not try to make the plugin reload anything:
 *
 *   - Before the emulator starts, the plugin's in-memory Input-SDL-ControlN sections are set to a fixed
 *     "virtual pad" layout (button(0..13) = the 14 N64 buttons, axis(0-,0+) / axis(1-,1+) = the stick, with
 *     AnalogDeadzone 0 and AnalogPeak 32767).  The plugin reads that once, as usual.
 *   - From then on suxty answers the plugin's SDL_JoystickGet* calls for the player's joystick itself
 *     (inputfilter.c overrides, the same layer that hides hotkeys from the game), with the N64 state computed
 *     from the *current* padmap and the real, filtered pad state.  Changing a mapping, a preset, the deadzone,
 *     or even which pad is player 1, is therefore live: the next poll sees it.  Nothing is reloaded.
 *   - What is saved in mupen64plus.cfg is the standard, human-readable form: the real physical bindings
 *     ("button(1)", "axis(2+)", "hat(0 Up)" ...) of the base layer, so the same file works with a stock
 *     setup (the modifier layer has no stock equivalent and is not exported).  After the plugin has read the
 *     virtual layout, suxty puts those real values back in the in-memory sections.
 *
 * If a game's input plugin does not read the pad through those SDL calls the remap has no effect (a one-time
 * warning is logged); [input] remap_input = false in suxty.cfg turns the whole layer off and leaves the
 * plugin's own configuration alone.
 */

#ifndef REMAP_H
#define REMAP_H

#include <stddef.h>

#include <SDL.h>

#include "padmap.h"

#define REMAP_PLAYERS 4

int  remap_init(void);                    /* after pad_init() and cfg_init() */
void remap_shutdown(void);
int  remap_enabled(void);

/* Plugin configuration (needs the core's config API): see the big comment above. */
void remap_set_save_core_config(int save);          /* honour --nosaveoptions */
void remap_prepare_launch(void);                    /* just before M64CMD_EXECUTE */
void remap_after_plugin_init(void);                 /* first frame: put the real bindings back in memory */
void remap_finish(void);                            /* before the final config save */

/* Called by the SDL overrides (inputfilter.c) on the emulation thread. */
int  remap_player_for(SDL_Joystick *js);            /* player 0-3 whose plugin joystick this is, else -1 */
int  remap_virtual_button(int player, int button);
int  remap_virtual_axis(int player, int axis);
void remap_update(void);                            /* after every SDL_JoystickUpdate: evaluate all players */
unsigned remap_virtual_hits(void);                  /* how often the input plugin read a virtual pad */
int      remap_health_check(void);                  /* 1 = plugin reads it, -1 = never did, 0 = n/a */

/* Menu API.  Two layers (M4): the global mapping/choices and the game's own.  remap_get() shows the layer of the
 * current scope (g_cfg.scope); remap_edit() + remap_commit() change and persist it (suxty.cfg, or the game file in
 * GAME scope, where the game starts from a copy of the global mapping).  What the game plays with is the game's layer
 * if it has one.  mupen64plus.cfg only ever receives the global form. */
const padmap *remap_get(int player);
padmap       *remap_edit(int player);               /* then remap_commit() */
void          remap_commit(int player);             /* persist, schedule the config export */
int           remap_player_want_overridden(int player);   /* the game file chooses this player's pad */
int           remap_pad_overridden(int player);     /* the game file has its own mapping for this player's pad */
int           remap_reset_game(void);               /* drop all of the game's controller choices/mappings; returns how many */
void          remap_flush(void);                    /* write pending changes (cfg + mupen64plus.cfg sections) */
int           remap_player_pad(int player);         /* pad.c index, or -1 */
int           remap_player_live(int player);        /* the plugin has a joystick for this player */
const char   *remap_player_want(int player);        /* "" auto, "none", or a pad name */
/* Chooses the pad of a player ("" auto, "none", pad name).  Returns 1 = effective now, 0 = from the next launch. */
int           remap_set_pad(int player, const char *want);
int           remap_apply_preset(int player, int preset, char *msg, size_t msgsize);
/* the N64 state this player's mapping makes of the real, unfiltered pad (the "test" page) */
void          remap_probe(int player, n64_state *out);

#endif /* REMAP_H */
