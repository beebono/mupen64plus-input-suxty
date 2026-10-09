/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-ui-suxty - cfg.c
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


#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "cfg.h"
#include "inputfilter.h"
#include "toast.h"
#include "m64p_types.h"
#include "main.h"
#include "osal_files.h"
#include "pad.h"

suxty_cfg_t g_cfg;

static const char *l_header =
    "# mupen64plus-ui-suxty configuration\n"
    "# Edited by hand or by the in-game menu.  Unknown keys are kept.\n"
    "#\n"
    "# [menu]  menu_combo   buttons that open/close the menu, joined with '+'\n"
    "#                      (up down left right a b x y l r start select l3 r3 guide l2 r2)\n"
    "#         recovery_combo  a second combo that ALWAYS opens the menu, so a bad menu_combo can never lock\n"
    "#                      you out (default select+start; 'none' turns it off, at your own risk)\n"
    "#         dim_percent  how dark the frozen game frame is behind the menu (0-90)\n"
    "#         show_hints   show the button hint footer\n"
    "#         show_toasts  brief on-screen notices (\"Saved slot 3\")\n"
    "#         toast_ms     how long they stay (300-10000)\n"
    "# [input] swap_ab      swap confirm (A) and back (B)\n"
    "#         nav_deadzone left-stick travel (%) that counts as a D-pad press (10-90)\n"
    "#         block_game_input  hide hotkey / menu buttons from the game (recommended)\n"
    "# [hotkeys]  Combos are buttons joined with '+'; the word 'hotkey' stands for hotkey_button.\n"
    "#         hotkey_button  while it is held the game sees a neutral controller (default select)\n"
    "#         quick_save quick_load next_slot prev_slot fast_forward quit   (empty = disabled)\n"
    "#         fast_forward_mode   hold | toggle\n"
    "#         fast_forward_speed  percent (50-1000), or 0 for unlimited\n"
    "#         quit_hold_ms        how long the quit combo must be held\n"
    "#         remap_input   false = leave the input plugin's own configuration alone (no remap layer)\n"
    "# [states] autosave_on_quit  save the auto slot when leaving the game (used by --resume)\n"
    "# [players]  p1..p4 = auto | none | <pad name>: which controller drives which N64 player\n"
    "# [pad:<pad name>]  the N64 mapping of one pad model (written by Controls in the menu): preset, deadzone,\n"
    "#         peak, modifier = button:N | hat:N:up | axis:N+ | none, and one key per N64 input (a b z start l r\n"
    "#         dpad_up .. c_up .. stick_left ..) = button:N | hat:N:up | axis:N- | none; mod_<key> is the same\n"
    "#         while the modifier is held";

static const char *l_game_header =
    "# mupen64plus-ui-suxty: per-game overrides for one ROM (the file name is the ROM's MD5)\n"
    "# Everything here wins over suxty.cfg / mupen64plus.cfg for this game only.  Delete a line (or the whole\n"
    "# file) to go back to the global value; the in-game menu does the same with \"Reset to global\".\n"
    "#\n"
    "# [hotkeys] [states] [input] [menu] [audio]   the same keys as suxty.cfg (volume = 0-100 is in [audio])\n"
    "# [players], [pad:<pad name>]                 controller choice / N64 mapping of one pad model\n"
    "# [cfg:<section>]                              core or plugin parameters, e.g. [cfg:Video-Foir]  Threads = -1\n"
    "#                                              (read when the game starts: change them, then restart the game)\n"
    "# [plugins]  video / audio / input / rsp = a plugin file name or path, used when none is given on the command line";

static void join(char *dst, size_t size, const char *dir, const char *name)
{
    if ((size_t) snprintf(dst, size, "%s/%s", dir, name) >= size)
        DebugMessage(M64MSG_WARNING, "path too long: %s/%s", dir, name);
}

int cfg_init(const char *config_dir)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    snprintf(g_cfg.config_dir, sizeof(g_cfg.config_dir), "%s", config_dir);
    {
        size_t n = strlen(g_cfg.config_dir);
        while (n > 1 && g_cfg.config_dir[n - 1] == '/')
            g_cfg.config_dir[--n] = 0;
    }
    join(g_cfg.suxty_dir, sizeof(g_cfg.suxty_dir), g_cfg.config_dir, "suxty");
    join(g_cfg.games_dir, sizeof(g_cfg.games_dir), g_cfg.suxty_dir, "games");
    join(g_cfg.cfg_path, sizeof(g_cfg.cfg_path), g_cfg.suxty_dir, "suxty.cfg");

    if (osal_mkdir_p(g_cfg.games_dir) != 0)
        DebugMessage(M64MSG_WARNING, "couldn't create config directory '%s'", g_cfg.games_dir);

    g_cfg.ini = ini_new();
    if (g_cfg.ini == NULL)
        return -1;
    if (ini_load(g_cfg.ini, g_cfg.cfg_path) != 0)
        DebugMessage(M64MSG_INFO, "creating default config '%s'", g_cfg.cfg_path);

    ini_default(g_cfg.ini, "general", "version", "1");
    ini_default(g_cfg.ini, "menu", "menu_combo", "select+start");
    ini_default(g_cfg.ini, "menu", "recovery_combo", "select+start");
    ini_default(g_cfg.ini, "menu", "dim_percent", "65");
    ini_default(g_cfg.ini, "menu", "show_hints", "true");
    ini_default(g_cfg.ini, "input", "swap_ab", "false");
    ini_default(g_cfg.ini, "menu", "show_toasts", "true");
    ini_default(g_cfg.ini, "menu", "toast_ms", "1800");
    ini_default(g_cfg.ini, "input", "nav_deadzone", "50");
    ini_default(g_cfg.ini, "input", "block_game_input", "true");
    ini_default(g_cfg.ini, "input", "remap_input", "true");
    ini_default(g_cfg.ini, "hotkeys", "hotkey_button", "select");
    ini_default(g_cfg.ini, "hotkeys", "quick_save", "hotkey+r");
    ini_default(g_cfg.ini, "hotkeys", "quick_load", "hotkey+l");
    ini_default(g_cfg.ini, "hotkeys", "next_slot", "hotkey+right");
    ini_default(g_cfg.ini, "hotkeys", "prev_slot", "hotkey+left");
    ini_default(g_cfg.ini, "hotkeys", "fast_forward", "hotkey+y");
    ini_default(g_cfg.ini, "hotkeys", "quit", "hotkey+b");
    ini_default(g_cfg.ini, "hotkeys", "fast_forward_mode", "hold");
    ini_default(g_cfg.ini, "hotkeys", "fast_forward_speed", "300");
    ini_default(g_cfg.ini, "hotkeys", "quit_hold_ms", "1000");
    ini_default(g_cfg.ini, "states", "autosave_on_quit", "true");

    cfg_apply();
    cfg_save();
    return 0;
}

/* "hotkey+r" -> mask; the word "hotkey" is replaced by the configured hotkey button.  Empty/"none" = 0.
 * Returns 0 and warns on a syntax error. */
static uint32_t parse_combo(const char *str, uint32_t hotkey, const char *key)
{
    char buf[96], out[160];
    char *tok, *save = NULL;
    size_t n = 0;
    uint32_t mask;

    if (str == NULL || str[0] == 0 || strcasecmp(str, "none") == 0)
        return 0;
    snprintf(buf, sizeof(buf), "%s", str);
    out[0] = 0;
    for (tok = strtok_r(buf, "+, ", &save); tok != NULL; tok = strtok_r(NULL, "+, ", &save))
    {
        if (strcasecmp(tok, "hotkey") == 0)
        {
            char hk[64];
            pad_format_combo(hotkey, hk, sizeof(hk));
            n += (size_t) snprintf(out + n, sizeof(out) - n, "%s%s", n ? "+" : "", hk);
        }
        else
            n += (size_t) snprintf(out + n, sizeof(out) - n, "%s%s", n ? "+" : "", tok);
        if (n >= sizeof(out))
            break;
    }
    mask = pad_parse_combo(out);
    if (mask == 0)
        DebugMessage(M64MSG_WARNING, "invalid %s combo '%s' in suxty.cfg; hotkey disabled", key, str);
    return mask;
}

void cfg_apply(void)
{
    const char *combo = cfg_get("menu", "menu_combo", "select+start");
    int dim = cfg_get_int("menu", "dim_percent", 65);

    g_cfg.hotkey_button = pad_parse_combo(cfg_get("hotkeys", "hotkey_button", "select"));
    if (g_cfg.hotkey_button == 0)
    {
        DebugMessage(M64MSG_WARNING, "invalid hotkey_button in suxty.cfg; using select");
        g_cfg.hotkey_button = pad_parse_combo("select");
    }
    /* the menu combo may use the word "hotkey" too */
    g_cfg.menu_combo = parse_combo(combo, g_cfg.hotkey_button, "menu_combo");
    if (g_cfg.menu_combo == 0)
    {
        DebugMessage(M64MSG_WARNING, "using select+start for the menu");
        g_cfg.menu_combo = pad_parse_combo("select+start");
    }
    {
        const char *rc = cfg_get("menu", "recovery_combo", "select+start");
        if (rc[0] == 0 || strcasecmp(rc, "none") == 0)
            g_cfg.recovery_combo = 0;
        else
        {
            g_cfg.recovery_combo = parse_combo(rc, g_cfg.hotkey_button, "recovery_combo");
            if (g_cfg.recovery_combo == 0)
                g_cfg.recovery_combo = pad_parse_combo("select+start");
        }
    }
    g_cfg.hk_quick_save = parse_combo(cfg_get("hotkeys", "quick_save", ""), g_cfg.hotkey_button, "quick_save");
    g_cfg.hk_quick_load = parse_combo(cfg_get("hotkeys", "quick_load", ""), g_cfg.hotkey_button, "quick_load");
    g_cfg.hk_next_slot = parse_combo(cfg_get("hotkeys", "next_slot", ""), g_cfg.hotkey_button, "next_slot");
    g_cfg.hk_prev_slot = parse_combo(cfg_get("hotkeys", "prev_slot", ""), g_cfg.hotkey_button, "prev_slot");
    g_cfg.hk_fast_forward = parse_combo(cfg_get("hotkeys", "fast_forward", ""), g_cfg.hotkey_button, "fast_forward");
    g_cfg.hk_quit = parse_combo(cfg_get("hotkeys", "quit", ""), g_cfg.hotkey_button, "quit");
    g_cfg.ff_toggle = strcasecmp(cfg_get("hotkeys", "fast_forward_mode", "hold"), "toggle") == 0;
    {
        int sp = cfg_get_int("hotkeys", "fast_forward_speed", 300);
        g_cfg.ff_speed = sp <= 0 ? 0 : sp < 50 ? 50 : sp > 1000 ? 1000 : sp;
        sp = cfg_get_int("hotkeys", "quit_hold_ms", 1000);
        g_cfg.quit_hold_ms = sp < 0 ? 0 : sp > 5000 ? 5000 : sp;
    }
    g_cfg.block_game_input = cfg_get_bool("input", "block_game_input", 1);
    g_cfg.autosave_on_quit = cfg_get_bool("states", "autosave_on_quit", 1);
    g_cfg.show_toasts = cfg_get_bool("menu", "show_toasts", 1);
    g_cfg.toast_ms = cfg_get_int("menu", "toast_ms", 1800);
    toast_set_enabled(g_cfg.show_toasts);
    toast_set_duration_ms(g_cfg.toast_ms);

    g_cfg.dim_percent = dim < 0 ? 0 : dim > 90 ? 90 : dim;
    g_cfg.show_hints = cfg_get_bool("menu", "show_hints", 1);
    g_cfg.swap_ab = cfg_get_bool("input", "swap_ab", 0);
    g_cfg.nav_deadzone = cfg_get_int("input", "nav_deadzone", 50);

    pad_set_swap_ab(g_cfg.swap_ab);
    {
        uint32_t all = g_cfg.menu_combo | g_cfg.recovery_combo | g_cfg.hk_quick_save | g_cfg.hk_quick_load | g_cfg.hk_next_slot |
                       g_cfg.hk_prev_slot | g_cfg.hk_fast_forward | g_cfg.hk_quit;
        uint32_t hot = g_cfg.hotkey_button;
        /* the filter works on physical buttons: undo the A/B swap that pad_poll applies for the user */
        if (g_cfg.swap_ab)
        {
            all = pad_swap_ab_mask(all);
            hot = pad_swap_ab_mask(hot);
        }
        inputfilter_configure(g_cfg.block_game_input, hot, all);
    }
    pad_set_nav_deadzone(g_cfg.nav_deadzone);
}

int cfg_save(void)
{
    int rc = 0;
    if (g_cfg.ini != NULL && ini_dirty(g_cfg.ini))
    {
        if (ini_save(g_cfg.ini, g_cfg.cfg_path, l_header) != 0)
        {
            DebugMessage(M64MSG_WARNING, "couldn't write '%s'", g_cfg.cfg_path);
            rc = -1;
        }
    }
    if (g_cfg.game != NULL && ini_dirty(g_cfg.game))
    {
        if (ini_count(g_cfg.game) == 0)
        {
            /* no overrides left: the game goes back to being plain global (the file would only be a header) */
            if (remove(g_cfg.game_path) != 0 && osal_file_exists(g_cfg.game_path))
                rc = -1;
            ini_clean(g_cfg.game);
        }
        else if (ini_save(g_cfg.game, g_cfg.game_path, l_game_header) != 0)
        {
            DebugMessage(M64MSG_WARNING, "couldn't write '%s'", g_cfg.game_path);
            rc = -1;
        }
    }
    return rc;
}

void cfg_shutdown(void)
{
    cfg_save();
    ini_free(g_cfg.ini);
    g_cfg.ini = NULL;
    ini_free(g_cfg.game);
    g_cfg.game = NULL;
}

/* ---- the game layer ---------------------------------------------------------------------------- */

int cfg_load_game(const char *md5)
{
    char name[40];
    size_t i, n = 0;

    ini_free(g_cfg.game);
    g_cfg.game = NULL;
    g_cfg.game_md5[0] = 0;
    g_cfg.game_path[0] = 0;
    g_cfg.scope = CFG_SCOPE_GLOBAL;
    for (i = 0; md5 != NULL && md5[i] && n + 1 < sizeof(name); i++)
    {
        char c = md5[i];
        if (c >= 'A' && c <= 'F')
            c = (char) (c - 'A' + 'a');
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
            name[n++] = c;
    }
    name[n] = 0;
    if (n < 8)
    {
        DebugMessage(M64MSG_WARNING, "the ROM has no usable MD5: per-game settings are unavailable");
        cfg_apply();
        return -1;
    }
    snprintf(g_cfg.game_md5, sizeof(g_cfg.game_md5), "%s", name);
    {
        char file[48];
        snprintf(file, sizeof(file), "%s.cfg", name);
        join(g_cfg.game_path, sizeof(g_cfg.game_path), g_cfg.games_dir, file);
    }
    g_cfg.game = ini_new();
    if (g_cfg.game == NULL)
        return -1;
    if (ini_load(g_cfg.game, g_cfg.game_path) == 0)
        DebugMessage(M64MSG_INFO, "game profile: %s (%d overrides)", g_cfg.game_path, ini_count(g_cfg.game));
    else
        DebugMessage(M64MSG_VERBOSE, "game profile: none yet (%s)", g_cfg.game_path);
    cfg_apply();
    return 0;
}

int cfg_game_available(void)
{
    return g_cfg.game != NULL;
}

int cfg_game_override_count(void)
{
    return g_cfg.game != NULL ? ini_count(g_cfg.game) : 0;
}

int cfg_key_overridable(const char *section, const char *key)
{
    static const char *const global_only[][2] = {
        { "menu", "dim_percent" }, { "menu", "show_hints" }, { "menu", "show_toasts" }, { "menu", "toast_ms" },
        { "menu", "recovery_combo" },                       /* lock-out protection must not depend on the game */
        { "input", "swap_ab" }, { "input", "nav_deadzone" }, { "input", "block_game_input" }, { "input", "remap_input" },
        { "general", "version" }
    };
    size_t i;
    for (i = 0; i < sizeof(global_only) / sizeof(global_only[0]); i++)
        if (strcmp(section, global_only[i][0]) == 0 && strcmp(key, global_only[i][1]) == 0)
            return 0;
    return 1;
}

const char *cfg_get(const char *section, const char *key, const char *def)
{
    if (g_cfg.game != NULL && ini_has(g_cfg.game, section, key) && cfg_key_overridable(section, key))
        return ini_get(g_cfg.game, section, key, def);
    return ini_get(g_cfg.ini, section, key, def);
}

int cfg_get_int(const char *section, const char *key, int def)
{
    if (g_cfg.game != NULL && ini_has(g_cfg.game, section, key) && cfg_key_overridable(section, key))
        return ini_get_int(g_cfg.game, section, key, def);
    return ini_get_int(g_cfg.ini, section, key, def);
}

int cfg_get_bool(const char *section, const char *key, int def)
{
    if (g_cfg.game != NULL && ini_has(g_cfg.game, section, key) && cfg_key_overridable(section, key))
        return ini_get_bool(g_cfg.game, section, key, def);
    return ini_get_bool(g_cfg.ini, section, key, def);
}

const char *cfg_get_scoped(const char *section, const char *key, const char *def)
{
    return g_cfg.scope == CFG_SCOPE_GAME ? cfg_get(section, key, def) : ini_get(g_cfg.ini, section, key, def);
}

int cfg_get_scoped_int(const char *section, const char *key, int def)
{
    return g_cfg.scope == CFG_SCOPE_GAME ? cfg_get_int(section, key, def) : ini_get_int(g_cfg.ini, section, key, def);
}

int cfg_get_scoped_bool(const char *section, const char *key, int def)
{
    return g_cfg.scope == CFG_SCOPE_GAME ? cfg_get_bool(section, key, def) : ini_get_bool(g_cfg.ini, section, key, def);
}

int cfg_overridden(const char *section, const char *key)
{
    return g_cfg.game != NULL && ini_has(g_cfg.game, section, key) && cfg_key_overridable(section, key);
}

void cfg_set_scope(int scope)
{
    g_cfg.scope = (scope == CFG_SCOPE_GAME && g_cfg.game != NULL) ? CFG_SCOPE_GAME : CFG_SCOPE_GLOBAL;
}

void cfg_set(const char *section, const char *key, const char *value)
{
    if (g_cfg.scope == CFG_SCOPE_GAME && g_cfg.game != NULL && cfg_key_overridable(section, key))
        ini_set(g_cfg.game, section, key, value);
    else
        ini_set(g_cfg.ini, section, key, value);
    cfg_apply();
}

void cfg_set_int(const char *section, const char *key, int value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    cfg_set(section, key, buf);
}

void cfg_set_bool(const char *section, const char *key, int value)
{
    cfg_set(section, key, value ? "true" : "false");
}

void cfg_reset(const char *section, const char *key)
{
    if (g_cfg.game != NULL && ini_remove(g_cfg.game, section, key))
        cfg_apply();
}

int cfg_reset_all_game(void)
{
    int n = cfg_game_override_count();
    if (g_cfg.game != NULL)
    {
        const char *sec, *key;
        while (ini_entry(g_cfg.game, 0, &sec, &key, NULL))
        {
            char s[128], k[128];
            snprintf(s, sizeof(s), "%s", sec);
            snprintf(k, sizeof(k), "%s", key);
            ini_remove(g_cfg.game, s, k);
        }
        cfg_apply();
    }
    return n;
}

/* ---- combo helpers ---------------------------------------------------------------------------- */

/* the hotkey button of the layer the menu is editing */
static uint32_t scoped_hotkey(void)
{
    uint32_t h = pad_parse_combo(cfg_get_scoped("hotkeys", "hotkey_button", "select"));
    return h != 0 ? h : pad_parse_combo("select");
}

void cfg_combo_token_string(uint32_t mask, char *out, size_t size)
{
    uint32_t hot = scoped_hotkey();
    if (size == 0)
        return;
    out[0] = 0;
    if (mask == 0)
    {
        snprintf(out, size, "none");
        return;
    }
    if (hot != 0 && (mask & hot) == hot && mask != hot)
    {
        char rest[96];
        pad_format_combo(mask & ~hot, rest, sizeof(rest));
        snprintf(out, size, "hotkey+%.80s", rest);
    }
    else
        pad_format_combo(mask, out, size);
}

void cfg_combo_display(uint32_t mask, char *out, size_t size)
{
    /* hotkey button first, then Select, then the rest in button order: "Select+R", "Select+Start" */
    uint32_t first = 0, sel = PADBIT(PAD_SELECT);
    uint32_t order[PAD_NUM];
    int n = 0, i;
    size_t len = 0;

    if (size == 0)
        return;
    out[0] = 0;
    if (mask == 0)
    {
        snprintf(out, size, "(none)");
        return;
    }
    first = mask & scoped_hotkey();
    for (i = 0; i < PAD_NUM; i++)
        if (first & PADBIT(i))
            order[n++] = (uint32_t) i;
    if ((mask & sel) && !(first & sel))
        order[n++] = PAD_SELECT;
    for (i = 0; i < PAD_NUM; i++)
        if ((mask & PADBIT(i)) && !(first & PADBIT(i)) && !(i == PAD_SELECT))
            order[n++] = (uint32_t) i;
    for (i = 0; i < n; i++)
    {
        char name[8];
        snprintf(name, sizeof(name), "%s", pad_button_name((pad_button) order[i]));
        if (name[0] >= 'a' && name[0] <= 'z')
            name[0] = (char) (name[0] - 32);
        len += (size_t) snprintf(out + len, len < size ? size - len : 0, "%s%s", i ? "+" : "", name);
    }
}

const char *cfg_combo_section(const char *key)
{
    return strcmp(key, "menu_combo") == 0 ? "menu" : "hotkeys";
}

void cfg_set_combo(const char *key, uint32_t mask)
{
    char tok[96];
    cfg_combo_token_string(mask, tok, sizeof(tok));
    cfg_set(cfg_combo_section(key), key, tok);
}

uint32_t cfg_combo_mask(const char *str, uint32_t hotkey)
{
    return parse_combo(str, hotkey, "combo");
}
