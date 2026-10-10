/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - cfg.h
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

#ifndef CFG_H
#define CFG_H

#define CFG_VERSION 3

#include <stddef.h>
#include <stdint.h>

#include "inifile.h"
#include "osal_preproc.h"

typedef struct {
    char    config_dir[PATH_MAX];
    char    suxty_dir[PATH_MAX];
    char    games_dir[PATH_MAX];
    char    cfg_path[PATH_MAX];
    ini_t  *ini;

    ini_t  *game;
    char    game_path[PATH_MAX];
    char    game_md5[40];
    int     scope;

    uint32_t menu_combo;
    uint32_t recovery_combo;
    int      dim_percent;
    int      swap_ab;
    int      nav_deadzone;
    int      soft_tilt_pct;

    /* combos are pad_button masks; 0 = disabled */
    uint32_t hotkey_button;
    uint32_t hk_quick_save;
    uint32_t hk_quick_load;
    uint32_t hk_next_slot;
    uint32_t hk_prev_slot;
    uint32_t hk_fast_forward;
    uint32_t hk_quit;
    int      ff_toggle;         /* 0 = hold, 1 = toggle */
    int      ff_speed;          /* percent (50-1000); 0 = unlimited */
    int      confirm_quit;
    int      block_game_input;

    int      autosave_on_quit;
    int      show_toasts;
    int      toast_ms;
} suxty_cfg_t;

extern suxty_cfg_t g_cfg;

typedef enum { CFG_SCOPE_GLOBAL = 0, CFG_SCOPE_GAME = 1 } cfg_scope;

int  cfg_init(const char *config_dir);
void cfg_apply(void);
int  cfg_save(void);
int  cfg_load_game(const char *md5);
int  cfg_game_available(void);

const char *cfg_get(const char *section, const char *key, const char *def);
int  cfg_get_int(const char *section, const char *key, int def);
int  cfg_get_bool(const char *section, const char *key, int def);
const char *cfg_get_scoped(const char *section, const char *key, const char *def);
int  cfg_get_scoped_int(const char *section, const char *key, int def);
int  cfg_get_scoped_bool(const char *section, const char *key, int def);
int  cfg_overridden(const char *section, const char *key);
/* a key that cannot be per-game goes to the global layer even in GAME scope */
void cfg_set(const char *section, const char *key, const char *value);
void cfg_set_int(const char *section, const char *key, int value);
void cfg_set_bool(const char *section, const char *key, int value);
void cfg_reset(const char *section, const char *key);
int  cfg_reset_all_game(void);
void cfg_set_scope(int scope);
int  cfg_key_overridable(const char *section, const char *key);
int  cfg_game_override_count(void);
int  cfg_game_setting_count(void);
int  cfg_pin_profile(void);
void cfg_shutdown(void);

void cfg_combo_token_string(uint32_t mask, char *out, size_t size);
void cfg_combo_display(uint32_t mask, char *out, size_t size);
void cfg_set_combo(const char *key, uint32_t mask);
uint32_t cfg_combo_mask(const char *str, uint32_t hotkey);
const char *cfg_combo_section(const char *key);                          /* menu_combo lives in [menu], the rest in [hotkeys] */

#endif /* CFG_H */
