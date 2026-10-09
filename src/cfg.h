/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-ui-suxty - cfg.h
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


/* suxty's own configuration: <configdir>/suxty/suxty.cfg, and the per-game layer on top of it.
 *
 * Layout (follows the Mupen64Plus config directory, README "Configuration layout"):
 *   <configdir>/mupen64plus.cfg      core + plugin settings (owned by the core and the front-end; suxty never writes it)
 *   <configdir>/suxty/suxty.cfg      menu, hotkeys, auto-save options (the global layer of this file)
 *   <configdir>/suxty/games/<md5>.cfg  per-game overrides (the game layer), keyed by the ROM's MD5
 *
 * LAYERING (M4).  Every suxty setting is looked up in the game layer first, then in the global layer
 * (cfg_get*()).  The game file uses the same sections and keys as suxty.cfg:
 *   [menu] [input] [hotkeys] [states] [audio]   suxty options (a few, listed in cfg_key_overridable(), are global only)
 *   [players], [pad:<name>]                      controller choice and mappings (remap.c, padmap_load()'s overlay)
 *   [foir]                                       foir video options by their [Video-Foir] names (foir_if.h)
 * "Reset to global" is simply deleting the game entry; "Save for this game" is writing it.  The menu has an edit
 * scope (g_cfg.scope): writes go to the game layer when it is GAME, to the global layer otherwise.
 */

#ifndef CFG_H
#define CFG_H

#define CFG_VERSION 2        /* [general] version: 2 = input plugin (autosave_on_quit became opt-in) */

#include <stddef.h>
#include <stdint.h>

#include "inifile.h"
#include "osal_preproc.h"

typedef struct {
    char    config_dir[PATH_MAX];
    char    suxty_dir[PATH_MAX];
    char    games_dir[PATH_MAX];
    char    cfg_path[PATH_MAX];
    ini_t  *ini;                /* the global layer: suxty.cfg */

    /* the game layer: games/<rom md5>.cfg (NULL until cfg_load_game(); an empty ini when the game has no overrides) */
    ini_t  *game;
    char    game_path[PATH_MAX];
    char    game_md5[40];
    int     scope;              /* cfg_scope: where the menu's edits go */

    /* cached, validated copies of the [menu]/[input] settings */
    uint32_t menu_combo;        /* pad_button mask that toggles the menu */
    uint32_t recovery_combo;    /* a second combo that always opens the menu (lock-out protection); 0 = off */
    int      dim_percent;       /* 0..90: how much the frozen game frame is darkened behind the menu */
    int      show_hints;        /* footer button hints */
    int      swap_ab;           /* swap confirm/back */
    int      nav_deadzone;      /* % of stick travel that counts as a D-pad press */

    /* hotkeys: combos are masks of pad_button bits; 0 = disabled */
    uint32_t hotkey_button;     /* the hotkey button ("hotkey" in combos); the game sees a neutral pad while it is held */
    uint32_t hk_quick_save;
    uint32_t hk_quick_load;
    uint32_t hk_next_slot;
    uint32_t hk_prev_slot;
    uint32_t hk_fast_forward;
    uint32_t hk_quit;
    int      ff_toggle;         /* 0 = hold the combo, 1 = press to toggle */
    int      ff_speed;          /* percent (50-1000); 0 = unlimited (speed limiter off) */
    int      quit_hold_ms;      /* the quit combo must be held this long */
    int      block_game_input;  /* hide hotkey/menu input from the game (inputfilter.c) */

    int      autosave_on_quit;
    int      show_toasts;
    int      toast_ms;
} suxty_cfg_t;

extern suxty_cfg_t g_cfg;

typedef enum { CFG_SCOPE_GLOBAL = 0, CFG_SCOPE_GAME = 1 } cfg_scope;

/* config_dir: the directory the core is using (ConfigGetUserConfigPath). */
int  cfg_init(const char *config_dir);
void cfg_apply(void);            /* ini -> cached fields (+ pad settings) */
int  cfg_save(void);             /* writes only if something changed (both layers; an empty game file is deleted) */
/* Opens the game layer for the ROM with this MD5 (call once the ROM is known), then re-applies the settings. */
int  cfg_load_game(const char *md5);
int  cfg_game_available(void);   /* a game layer exists (the ROM has an MD5) */

/* Layered lookups: the game layer wins over the global layer. */
const char *cfg_get(const char *section, const char *key, const char *def);
int  cfg_get_int(const char *section, const char *key, int def);
int  cfg_get_bool(const char *section, const char *key, int def);
/* The same as seen by the menu in the current edit scope: GLOBAL looks at the global layer only, GAME at both. */
const char *cfg_get_scoped(const char *section, const char *key, const char *def);
int  cfg_get_scoped_int(const char *section, const char *key, int def);
int  cfg_get_scoped_bool(const char *section, const char *key, int def);
/* Does the game layer define it?  (Drives the "this game has its own value" marker.) */
int  cfg_overridden(const char *section, const char *key);
/* Writes to the layer of the current scope (GAME scope for a key that cannot be per-game goes to the global layer),
 * then re-applies the cached settings. */
void cfg_set(const char *section, const char *key, const char *value);
void cfg_set_int(const char *section, const char *key, int value);
void cfg_set_bool(const char *section, const char *key, int value);
/* "Reset to global": removes the game entry (nothing happens to the global layer). */
void cfg_reset(const char *section, const char *key);
int  cfg_reset_all_game(void);   /* removes every game entry (the whole file); returns how many */
void cfg_set_scope(int scope);
/* Keys that make no sense per game (menu look, recovery combo ...) always live in suxty.cfg. */
int  cfg_key_overridable(const char *section, const char *key);
int  cfg_game_override_count(void);
void cfg_shutdown(void);

/* Combo helpers for the hotkey UI.  The cfg stores combos as text: buttons joined with '+', and the word
 * "hotkey" for the hotkey button ("hotkey+r"). */
void cfg_combo_token_string(uint32_t mask, char *out, size_t size);     /* mask -> "hotkey+r" (as stored) */
void cfg_combo_display(uint32_t mask, char *out, size_t size);          /* mask -> "Select+R" (for the menu) */
void cfg_set_combo(const char *key, uint32_t mask);                     /* stores [hotkeys]/[menu] key, mask 0 = none */
uint32_t cfg_combo_mask(const char *str, uint32_t hotkey);              /* stored text -> mask with `hotkey` as the hotkey button */
const char *cfg_combo_section(const char *key);                          /* "menu" for menu_combo, else "hotkeys" */

#endif /* CFG_H */
