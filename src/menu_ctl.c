/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-ui-suxty - menu_ctl.c
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

/* The controller and hotkey pages of the pause menu: player / pad choice, presets, button mapping with the
 * capture prompt, the modifier layer, a controller test, and the hotkey combo assignment with its lock-out
 * guards.  Drawn by menu.c's page stack; see menu_int.h. */

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <SDL.h>

#include "cfg.h"
#include "menu_int.h"
#include "pad.h"
#include "padmap.h"
#include "remap.h"
#include "toast.h"
#include "ui.h"

#define CAPTURE_TIMEOUT_MS   6000u     /* a control capture gives up after this long without input */
#define COMBO_IDLE_MS        8000u     /* a combo capture gives up after this long without a button */
#define CONFIRM_TIMEOUT_MS  10000u     /* a menu-combo change must be proven by using it within this time */

static int  l_player;                  /* the player the Controls pages edit, 0-3 */
static char l_msg[160];
static int  l_msg_kind;

static void set_msg(const char *text, int kind)
{
    snprintf(l_msg, sizeof(l_msg), "%s", text ? text : "");
    l_msg_kind = kind;
}

static int popcount(uint32_t v) { return __builtin_popcount(v); }

static int scope_game(void) { return g_cfg.scope == CFG_SCOPE_GAME; }

const char *menu_scoped_title(char *buf, size_t size, const char *base)
{
    snprintf(buf, size, "%s%s", base, scope_game() ? " (game)" : "");
    return buf;
}

int menu_mark_kind(int overridden)
{
    return overridden ? (scope_game() ? 1 : 2) : 0;
}

/* ---- capture of one physical control ------------------------------------------------------------ */

static struct {
    int      layer;
    int      ctl;                      /* n64_ctl, or -1 for the modifier button */
    uint32_t deadline;
} l_cap;

static const char *ctl_title(int layer, int ctl, char *buf, size_t size)
{
    if (ctl < 0)
        snprintf(buf, size, "Modifier button");
    else
        snprintf(buf, size, "N64 %s%s", n64_ctl_name((n64_ctl) ctl), layer ? " (with modifier)" : "");
    return buf;
}

static void start_capture(int layer, int ctl)
{
    int pad = remap_player_pad(l_player);
    if (pad < 0)
    {
        set_msg("No controller for this player", -1);
        return;
    }
    l_cap.layer = layer;
    l_cap.ctl = ctl;
    l_cap.deadline = SDL_GetTicks() + CAPTURE_TIMEOUT_MS;
    pad_capture_begin(pad);
    menu_push(PAGE_CAPTURE);
}

static n64_ctl opposite(n64_ctl c)
{
    return c == N64_SLEFT ? N64_SRIGHT : c == N64_SRIGHT ? N64_SLEFT : c == N64_SUP ? N64_SDOWN : N64_SUP;
}

/* binds `s` to the target of the capture; a control can only drive one input per layer, so it moves */
static void assign_src(int p, int layer, int ctl, const padsrc *s)
{
    padmap *m = remap_edit(p);
    char what[112], text[48], from[32] = "";

    if (ctl < 0)
        m->modifier = *s;
    else
    {
        int k;
        for (k = 0; k < N64_NUM; k++)
            if (k != ctl && padsrc_same(&m->src[layer][k], s))
            {
                memset(&m->src[layer][k], 0, sizeof(padsrc));
                snprintf(from, sizeof(from), "%s", n64_ctl_name((n64_ctl) k));
            }
        m->src[layer][ctl] = *s;
        /* "Stick Left" captured as axis 0-: offer axis 0+ for "Stick Right" if that is still free */
        if (ctl >= N64_SLEFT && s->type == PAD_BIND_AXIS && m->src[layer][opposite((n64_ctl) ctl)].type == PAD_BIND_NONE)
        {
            padsrc o = *s;
            o.dir = (int8_t) -s->dir;
            m->src[layer][opposite((n64_ctl) ctl)] = o;
        }
    }
    snprintf(m->preset, sizeof(m->preset), "custom");
    remap_commit(p);
    pad_format_src(remap_player_pad(p), s, text, sizeof(text));
    snprintf(what, sizeof(what), "%s = %s", ctl < 0 ? "Modifier" : n64_ctl_name((n64_ctl) ctl), text);
    if (from[0])
        snprintf(l_msg, sizeof(l_msg), "%s (moved from %s)", what, from);
    else
        snprintf(l_msg, sizeof(l_msg), "%s", what);
    l_msg_kind = 1;
}

static void page_capture(void)
{
    char title[64];
    padsrc s;
    int pad_idx = -1;
    int32_t left = (int32_t) (l_cap.deadline - SDL_GetTicks());

    {
        char who[96];
        snprintf(who, sizeof(who), "Player %d: %s", l_player + 1, pad_name(remap_player_pad(l_player)));
        ui_panel_begin_ex("Remap", 10, 76);
        ui_label_hi(ctl_title(l_cap.layer, l_cap.ctl, title, sizeof(title)));
        ui_label("Press the control to use");
        ui_label(who);
        ui_progress(left > 0 ? (int) (left * 1000 / (int32_t) CAPTURE_TIMEOUT_MS) : 0);
        ui_label("No input: cancel");
    }
    ui_panel_end("Move a stick, press a button");

    if (pad_capture_poll(&s, &pad_idx))
    {
        assign_src(l_player, l_cap.layer, l_cap.ctl, &s);
        menu_pop();
    }
    else if (left <= 0)
    {
        set_msg("Cancelled", 0);
        menu_pop();
    }
}

/* ---- Controls --------------------------------------------------------------------------------- */

static void pad_status(char *out, size_t size)
{
    int pad = remap_player_pad(l_player);
    if (pad >= 0)
        snprintf(out, size, "P%d: %s%s", l_player + 1, pad_name(pad), remap_player_live(l_player) ? "" : "  (from next launch)");
    else
        snprintf(out, size, "P%d: no controller", l_player + 1);
}

static void page_controls(int back)
{
    static const char *const players[4] = { "1", "2", "3", "4" };
    char nbuf[PAD_MAX_PADS + 2][72];
    const char *names[PAD_MAX_PADS + 2];
    int nn = 0, cur = 0, i, pi = l_player, p;
    const char *want;
    char info[96], title[40];
    const padmap *m;
    int dz, pk, np = pad_count(), mapmark;

    ui_panel_begin_scroll(menu_scoped_title(title, sizeof(title), "Controls"), 9, 1);
    if (ui_choice("Player", &pi, players, 4))
    {
        l_player = pi;
        set_msg("", 0);
    }
    p = l_player;
    m = remap_get(p);
    mapmark = menu_mark_kind(remap_pad_overridden(p));

    snprintf(nbuf[nn], sizeof(nbuf[nn]), "Auto");
    names[nn] = nbuf[nn]; nn++;
    snprintf(nbuf[nn], sizeof(nbuf[nn]), "None");
    names[nn] = nbuf[nn]; nn++;
    for (i = 0; i < np && nn < PAD_MAX_PADS + 2; i++)
    {
        snprintf(nbuf[nn], sizeof(nbuf[nn]), "%s", pad_name(i));
        names[nn] = nbuf[nn]; nn++;
    }
    want = remap_player_want(p);
    if (strcmp(want, "none") == 0)
        cur = 1;
    else if (want[0])
        for (i = 0; i < np; i++)
            if (strcmp(pad_name(i), want) == 0)
                cur = 2 + i;
    if (ui_choice("Controller", &cur, names, nn))
    {
        const char *w = cur == 0 ? "" : cur == 1 ? "none" : pad_name(cur - 2);
        int live = remap_set_pad(p, w);
        pad_status(info, sizeof(info));
        set_msg(info, live ? 1 : 0);
    }
    ui_mark_last(menu_mark_kind(remap_player_want_overridden(p)));

    if (ui_item("Presets", ">") == 1)
        menu_push(PAGE_PRESETS);
    ui_mark_last(mapmark);
    if (ui_item("Buttons", ">") == 1)
        menu_push(PAGE_BUTTONS);
    ui_mark_last(mapmark);
    if (ui_item("Modifier layer", m->modifier.type != PAD_BIND_NONE ? ">" : "off >") == 1)
        menu_push(PAGE_MODLAYER);
    ui_mark_last(mapmark);
    dz = m->deadzone;
    pk = m->peak;
    if (ui_slider("Stick deadzone", &dz, 0, 40, 1, "%"))
    {
        remap_edit(p)->deadzone = dz;
        remap_commit(p);
    }
    ui_mark_last(mapmark);
    if (ui_slider("Stick range", &pk, 50, 100, 1, "%"))
    {
        remap_edit(p)->peak = pk;
        remap_commit(p);
    }
    ui_mark_last(mapmark);
    if (ui_item("Test controller", ">") == 1)
        menu_push(PAGE_TEST);
    if (scope_game())
    {
        if (ui_button_dis("Reset to global", !(remap_pad_overridden(p) || remap_player_want_overridden(p))) > 0)
            menu_push(PAGE_CONFIRM_CTL_RESET);
    }
    if (ui_button("Back"))
        menu_pop();

    if (l_msg[0])
        ui_status_bar(l_msg, l_msg_kind);
    else if (!remap_enabled())
        ui_status_bar("No gamepad found", -1);
    else if (!scope_game() && (remap_pad_overridden(p) || remap_player_want_overridden(p)))
        ui_status_bar("This game has its own settings for this pad", 0);
    else
    {
        pad_status(info, sizeof(info));
        ui_status_bar(info, 0);
    }
    ui_panel_end("A Select   Left/Right Change   B Back");
    if (back)
        menu_pop();
}

static void page_confirm_ctl_reset(int back)
{
    ui_panel_begin_ex("Confirm", 8, 72);
    ui_label("Reset this game's controls");
    ui_label("to the global ones?");
    if (ui_button("No"))
        menu_pop();
    if (ui_button("Yes"))
    {
        int n = remap_reset_game();
        remap_flush();
        set_msg(n ? "Controls reset to global" : "Nothing to reset", n ? 1 : 0);
        menu_pop();
    }
    ui_panel_end("A Select   B Back");
    if (back)
        menu_pop();
}

/* ---- presets ------------------------------------------------------------------------------------ */

static int l_preset_sel;

static void page_presets(int back)
{
    int i;
    const padmap *m = remap_get(l_player);

    ui_panel_begin_scroll("Presets", 6, 1);
    for (i = 0; i < PRESET_NUM; i++)
    {
        int cur = (i == PRESET_DEFAULT) ? strcmp(m->preset, "default") == 0 : strcmp(m->preset, padmap_preset_name(i)) == 0;
        if (ui_item(padmap_preset_name(i), cur ? "(current)" : "") == 1)
        {
            l_preset_sel = i;
            menu_push(PAGE_CONFIRM_PRESET);
        }
    }
    if (ui_button("Back"))
        menu_pop();
    {
        int f = ui_focus_get();
        ui_status_bar(f >= 0 && f < PRESET_NUM ? padmap_preset_desc(f) : "", 0);
    }
    ui_panel_end("A Apply   B Back");
    if (back)
        menu_pop();
}

static void page_confirm_preset(int back)
{
    char t[64];
    ui_panel_begin_ex("Apply preset", 8, 72);
    ui_label_hi(padmap_preset_name(l_preset_sel));
    snprintf(t, sizeof(t), "Replaces player %d's buttons", l_player + 1);
    ui_label(t);
    if (ui_button("No"))
        menu_pop();
    if (ui_button("Yes"))
    {
        char msg[96];
        if (remap_apply_preset(l_player, l_preset_sel, msg, sizeof(msg)) == 0)
        {
            snprintf(t, sizeof(t), "Preset: %s", padmap_preset_name(l_preset_sel));
            if (msg[0])
                set_msg(msg, 0);
            else
                set_msg(t, 1);
        }
        else
            set_msg(msg, -1);
        menu_pop();
        menu_pop();
    }
    ui_panel_end("A Select   B Back");
    if (back)
        menu_pop();
}

/* ---- button mapping (both layers) -------------------------------------------------------------- */

static void page_map(int layer, int back)
{
    char title[40], text[56];
    int p = l_player, n, r;
    const padmap *m = remap_get(p);
    int pad = remap_player_pad(p);

    snprintf(title, sizeof(title), "%s - P%d%s", layer ? "Modifier layer" : "Buttons", p + 1, scope_game() ? " (game)" : "");
    ui_panel_begin_scroll(title, 9, 1);
    if (layer)
    {
        if (m->modifier.type != PAD_BIND_NONE)
            pad_format_src(pad, &m->modifier, text, sizeof(text));
        else
            snprintf(text, sizeof(text), "-");
        r = ui_item("Modifier", text);
        if (r == 1)
            start_capture(1, -1);
        else if (r == 2)
        {
            memset(&remap_edit(p)->modifier, 0, sizeof(padsrc));
            remap_commit(p);
            set_msg("Modifier cleared", 0);
        }
    }
    for (n = 0; n < N64_NUM; n++)
    {
        const padsrc *s = &m->src[layer][n];
        if (s->type != PAD_BIND_NONE)
            pad_format_src(pad, s, text, sizeof(text));
        else
            snprintf(text, sizeof(text), layer ? "as normal" : "-");
        r = ui_item(n64_ctl_name((n64_ctl) n), text);
        if (r == 1)
            start_capture(layer, n);
        else if (r == 2 && s->type != PAD_BIND_NONE)
        {
            memset(&remap_edit(p)->src[layer][n], 0, sizeof(padsrc));
            snprintf(remap_edit(p)->preset, sizeof(remap_edit(p)->preset), "custom");
            remap_commit(p);
            snprintf(text, sizeof(text), "%s cleared", n64_ctl_name((n64_ctl) n));
            set_msg(text, 0);
        }
    }
    if (ui_button("Back"))
        menu_pop();
    if (l_msg[0])
        ui_status_bar(l_msg, l_msg_kind);
    else if (layer)
        ui_status_bar("Held modifier: these replace the normal buttons", 0);
    ui_panel_end("A Remap   X Clear   B Back");
    if (back)
        menu_pop();
}

/* ---- controller test ------------------------------------------------------------------------------ */

static void join_names(char *out, size_t size, const char *prefix, uint32_t bits, int first, int count, const char *const *names)
{
    int i;
    size_t n = (size_t) snprintf(out, size, "%s", prefix);
    int any = 0;
    for (i = 0; i < count; i++)
        if (bits & (1u << (first + i)))
        {
            n += (size_t) snprintf(out + n, n < size ? size - n : 0, "%s%s", any ? " " : "", names[i]);
            any = 1;
        }
    if (!any)
        snprintf(out + n, n < size ? size - n : 0, "-");
}

static void page_test(int back)
{
    n64_state st;
    char l1[64], l2[64], l3[64], l4[64];
    static const char *const face[] = { "A", "B", "Z", "Start", "L", "R" };
    static const char *const dir[] = { "Up", "Down", "Left", "Right" };

    remap_probe(l_player, &st);
    join_names(l1, sizeof(l1), "", st.buttons, N64_A, 6, face);
    join_names(l2, sizeof(l2), "D-pad: ", st.buttons, N64_DUP, 4, dir);
    join_names(l3, sizeof(l3), "C: ", st.buttons, N64_CUP, 4, dir);
    snprintf(l4, sizeof(l4), "Stick %+4d %+4d%s", st.sx * 80 / 32767, -st.sy * 80 / 32767, st.mod ? "  [mod]" : "");

    ui_panel_begin("Test controller", 6);
    ui_label_hi(l1);
    ui_label(l2);
    ui_label(l3);
    ui_label(l4);
    ui_label(remap_player_pad(l_player) >= 0 ? "Press buttons on the pad" : "No controller for this player");
    if (ui_button("Back"))
        menu_pop();
    ui_panel_end("What the game would see   B Back");
    if (back)
        menu_pop();
}

/* ---- hotkeys ---------------------------------------------------------------------------------- */

/* The rows show and edit the layer of the current scope: the global hotkeys, or this game's (its own entries over the
 * global ones).  Lock-out validation is done within that layer. */

typedef struct {
    const char *key;       /* suxty.cfg key */
    const char *label;
    int         clearable;
} hk_row;

static const hk_row l_hk[] = {
    { "hotkey_button", "Hotkey button", 0 },
    { "menu_combo",    "Menu",          0 },
    { "quick_save",    "Quick-save",    1 },
    { "quick_load",    "Quick-load",    1 },
    { "next_slot",     "Next slot",     1 },
    { "prev_slot",     "Prev slot",     1 },
    { "fast_forward",  "Fast-forward",  1 },
    { "quit",          "Quit",          1 },
};
#define HK_COUNT ((int) (sizeof(l_hk) / sizeof(l_hk[0])))

static const char *const l_hk_default[HK_COUNT] = {
    "select", "select+start", "hotkey+r", "hotkey+l", "hotkey+right", "hotkey+left", "hotkey+y", "hotkey+b"
};

static const char *hk_section(int i) { return cfg_combo_section(l_hk[i].key); }

static uint32_t scoped_hot(void)
{
    uint32_t hot = pad_parse_combo(cfg_get_scoped("hotkeys", "hotkey_button", "select"));
    return hot != 0 ? hot : pad_parse_combo("select");
}

/* the combo of row `i` as the scope's layer has it, with `hot` as the hotkey button */
static uint32_t scoped_mask(int i, uint32_t hot)
{
    if (i == 0)
        return hot;
    return cfg_combo_mask(cfg_get_scoped(hk_section(i), l_hk[i].key, ""), hot);
}

/* unconfirmed menu combo / hotkey button change (lock-out guard).  Remembers how the layer looked before. */
static struct {
    int      active;
    int      game;                   /* the layer that was edited */
    int      had_menu, had_hot;      /* the game layer had its own entry */
    char     old_menu[96];
    char     old_hot[96];
    uint32_t deadline;
} l_pend;

static void revert_pending(void)
{
    int save_scope = g_cfg.scope;
    if (!l_pend.active)
        return;
    g_cfg.scope = l_pend.game ? CFG_SCOPE_GAME : CFG_SCOPE_GLOBAL;
    if (l_pend.game && !l_pend.had_menu)
        cfg_reset("menu", "menu_combo");
    else
        cfg_set("menu", "menu_combo", l_pend.old_menu);
    if (l_pend.game && !l_pend.had_hot)
        cfg_reset("hotkeys", "hotkey_button");
    else
        cfg_set("hotkeys", "hotkey_button", l_pend.old_hot);
    g_cfg.scope = save_scope;
    cfg_apply();
    cfg_save();
    l_pend.active = 0;
}

/* The combo that would apply for hotkey row `i` if row `changed` had `mask` and the hotkey button were `hot`. */
static uint32_t effective(int i, int changed, uint32_t mask, uint32_t hot)
{
    if (i == changed)
        return mask;
    return scoped_mask(i, hot);
}

/* Returns NULL if `mask` may be assigned to row `idx`, else why not.  An identical combo of an action row is
 * "stolen": its index goes to *steal and the caller clears it. */
static const char *validate(int idx, uint32_t mask, int *steal, char *buf, size_t size)
{
    uint32_t hot = scoped_hot();
    int i;

    *steal = -1;
    if (idx == 0)
    {
        if (popcount(mask) != 1)
            return "Use a single button";
        hot = mask;
    }
    else if (idx == 1)
    {
        if (popcount(mask) < 2)
            return "Use at least two buttons";
    }
    else if (mask != 0 && ((mask & hot) != hot || mask == hot))
    {
        char hn[40];
        cfg_combo_display(hot, hn, sizeof(hn));
        snprintf(buf, size, "Hold %s and press another button", hn);
        return buf;
    }
    if (mask == 0)
        return NULL;

    if (idx == 0)
    {
        /* every combo written with the word "hotkey" moves with the hotkey button: none may start to overlap */
        uint32_t old_hot = scoped_hot();
        int a, b;
        for (a = 1; a < HK_COUNT; a++)
            for (b = a + 1; b <= HK_COUNT; b++)
            {
                uint32_t ca = effective(a, -1, 0, hot), cb;
                uint32_t oa = scoped_mask(a, old_hot), ob;
                if (b == HK_COUNT)
                {
                    cb = ob = g_cfg.recovery_combo;
                }
                else
                {
                    cb = effective(b, -1, 0, hot);
                    ob = scoped_mask(b, old_hot);
                }
                if (ca == 0 || cb == 0 || (ca == oa && cb == ob))
                    continue;                       /* not affected by the new hotkey button */
                if (ca == cb && a == 1 && b == HK_COUNT)
                    continue;                       /* the menu combo may equal the recovery combo */
                if ((ca & cb) == ca || (ca & cb) == cb)
                {
                    snprintf(buf, size, "%s would overlap %s", l_hk[a].label, b == HK_COUNT ? "the recovery combo" : l_hk[b].label);
                    return buf;
                }
            }
        return NULL;
    }
    for (i = 1; i <= HK_COUNT; i++)
    {
        uint32_t other;
        const char *name;
        if (i == idx)
            continue;
        if (i == HK_COUNT)
        {
            other = g_cfg.recovery_combo;
            name = "the recovery combo";
        }
        else
        {
            other = effective(i, idx, mask, hot);
            name = l_hk[i].label;
        }
        if (other == 0)
            continue;
        if (other == mask)
        {
            if (idx == 1 && i == HK_COUNT)
                continue;                           /* the menu combo may equal the recovery combo */
            if (i >= 2 && i < HK_COUNT)
            {
                *steal = i;
                continue;
            }
            snprintf(buf, size, "Same as %s", name);
            return buf;
        }
        if ((other & mask) == mask || (other & mask) == other)
        {
            snprintf(buf, size, "Overlaps %s", name);
            return buf;
        }
    }
    return NULL;
}

static void commit_combo(int idx, uint32_t mask)
{
    char err[96], disp[48], old_menu[96], old_hot[96];
    int steal, had_menu, had_hot;
    uint32_t old_menu_mask = g_cfg.menu_combo;
    const char *why = validate(idx, mask, &steal, err, sizeof(err));

    if (why != NULL)
    {
        set_msg(why, -1);
        return;
    }
    if (!scope_game() && idx <= 1 && (cfg_overridden("menu", "menu_combo") || cfg_overridden("hotkeys", "hotkey_button")))
    {
        /* this session would not even use the new combo, so it could not be proven here */
        set_msg("This game has its own menu/hotkey button", -1);
        return;
    }
    snprintf(old_menu, sizeof(old_menu), "%s", cfg_get_scoped("menu", "menu_combo", "select+start"));
    snprintf(old_hot, sizeof(old_hot), "%s", cfg_get_scoped("hotkeys", "hotkey_button", "select"));
    had_menu = cfg_overridden("menu", "menu_combo");
    had_hot = cfg_overridden("hotkeys", "hotkey_button");
    if (steal >= 0)
        cfg_set_combo(l_hk[steal].key, 0);
    if (idx == 0)
        cfg_set("hotkeys", "hotkey_button", pad_button_name((pad_button) __builtin_ctz(mask)));
    else
        cfg_set_combo(l_hk[idx].key, mask);
    cfg_combo_display(mask, disp, sizeof(disp));
    if (g_cfg.menu_combo != old_menu_mask)
    {
        /* the way into this menu changed: it only stays if the new combo is used within a few seconds */
        l_pend.active = 1;
        l_pend.game = scope_game();
        l_pend.had_menu = had_menu;
        l_pend.had_hot = had_hot;
        snprintf(l_pend.old_menu, sizeof(l_pend.old_menu), "%s", old_menu);
        snprintf(l_pend.old_hot, sizeof(l_pend.old_hot), "%s", old_hot);
        l_pend.deadline = SDL_GetTicks() + CONFIRM_TIMEOUT_MS;
        menu_push(PAGE_COMBO_CONFIRM);
        return;
    }
    cfg_save();
    if (steal >= 0)
        snprintf(err, sizeof(err), "%s = %s  (%s cleared)", l_hk[idx].label, disp, l_hk[steal].label);
    else
        snprintf(err, sizeof(err), "%s = %s", l_hk[idx].label, disp);
    set_msg(err, 1);
}

/* combo capture: hold the buttons, let go */
static struct {
    int      idx;
    uint32_t acc, ignore;
    uint32_t deadline;
} l_cc;

static void start_combo_capture(int idx, uint32_t held)
{
    l_cc.idx = idx;
    l_cc.acc = 0;
    l_cc.ignore = held;                      /* the A press that brought us here */
    l_cc.deadline = SDL_GetTicks() + COMBO_IDLE_MS;
    menu_push(PAGE_COMBO_CAPTURE);
}

static void page_combo_capture(uint32_t held)
{
    char disp[64], title[48];
    uint32_t now = SDL_GetTicks(), eff;
    int32_t left;

    l_cc.ignore &= held;
    eff = held & ~l_cc.ignore;
    if (eff)
    {
        l_cc.acc |= eff;
        l_cc.deadline = now + COMBO_IDLE_MS;
    }
    left = (int32_t) (l_cc.deadline - now);
    snprintf(title, sizeof(title), "Set: %s", l_hk[l_cc.idx].label);
    cfg_combo_display(l_cc.acc | eff, disp, sizeof(disp));

    ui_panel_begin_ex("Hotkey", 10, 76);
    ui_label_hi(title);
    ui_label(l_cc.idx == 0 ? "Press the hotkey button" : "Hold the buttons, then let go");
    ui_label_hi((l_cc.acc | eff) ? disp : "...");
    ui_progress(left > 0 ? (int) (left * 1000 / (int32_t) COMBO_IDLE_MS) : 0);
    ui_label("No input: cancel");
    ui_panel_end("Hold the combo, then release");

    if (l_cc.acc != 0 && eff == 0)
    {
        uint32_t mask = l_cc.acc;
        menu_pop();
        commit_combo(l_cc.idx, mask);
    }
    else if (left <= 0)
    {
        menu_pop();
        set_msg("Cancelled", 0);
    }
}

static void page_combo_confirm(uint32_t pressed)
{
    char disp[48], t[48];
    int32_t left = (int32_t) (l_pend.deadline - SDL_GetTicks());

    cfg_combo_display(g_cfg.menu_combo, disp, sizeof(disp));
    snprintf(t, sizeof(t), "Reverting in %d s", left > 0 ? (left + 999) / 1000 : 0);
    ui_panel_begin_ex("Keep this menu combo?", 10, 80);
    ui_label("Press the new combo now:");
    ui_label_hi(disp);
    ui_progress(left > 0 ? (int) (left * 1000 / (int32_t) CONFIRM_TIMEOUT_MS) : 0);
    ui_label(t);
    ui_label("(B: revert)");
    ui_panel_end("Use it to keep it");

    if (left <= 0 || ((pressed & PADBIT(PAD_B)) && !(g_cfg.menu_combo & PADBIT(PAD_B))))
    {
        revert_pending();
        menu_pop();
        set_msg("Reverted: the old menu combo is back", -1);
    }
}

int ctl_combo_pressed(uint32_t held)
{
    char disp[48], t[80];
    if (!l_pend.active)
        return 0;
    if ((held & g_cfg.menu_combo) != g_cfg.menu_combo)
        return 0;                            /* e.g. only the recovery combo: the menu closes and the change is reverted */
    l_pend.active = 0;
    cfg_save();
    cfg_combo_display(g_cfg.menu_combo, disp, sizeof(disp));
    snprintf(t, sizeof(t), "Menu = %s", disp);
    set_msg(t, 1);
    menu_pop();
    return 1;
}

/* global scope: back to the built-in defaults.  Game scope: drop the game's entries so the global ones apply again. */
static void reset_hotkeys(void)
{
    int i;
    revert_pending();
    if (scope_game())
    {
        for (i = 0; i < HK_COUNT; i++)
            cfg_reset(hk_section(i), l_hk[i].key);
        set_msg("Hotkeys: back to the global ones", 1);
    }
    else
    {
        for (i = 0; i < HK_COUNT; i++)
            cfg_set(hk_section(i), l_hk[i].key, l_hk_default[i]);
        set_msg("Hotkeys reset to the defaults", 1);
    }
    cfg_apply();
    cfg_save();
}

static void page_hotkeys(uint32_t held, int back)
{
    int i, r;
    char disp[48], title[40];
    uint32_t hot = scoped_hot();

    ui_panel_begin_scroll(menu_scoped_title(title, sizeof(title), "Hotkeys"), 9, 1);
    for (i = 0; i < HK_COUNT; i++)
    {
        uint32_t mask = scoped_mask(i, hot);
        int over = cfg_overridden(hk_section(i), l_hk[i].key);
        cfg_combo_display(mask, disp, sizeof(disp));
        r = ui_item(l_hk[i].label, disp);
        ui_mark_last(menu_mark_kind(over));
        if (r == 1)
            start_combo_capture(i, held);
        else if (r == 2)
        {
            if (scope_game() && over)
            {
                /* the game's own value goes away: this game follows the global hotkey again */
                cfg_reset(hk_section(i), l_hk[i].key);
                cfg_save();
                snprintf(disp, sizeof(disp), "%s: global again", l_hk[i].label);
                set_msg(disp, 1);
            }
            else if (!l_hk[i].clearable)
                set_msg("This one can't be cleared", -1);
            else if (mask != 0)
            {
                cfg_set_combo(l_hk[i].key, 0);
                cfg_save();
                snprintf(disp, sizeof(disp), "%s cleared", l_hk[i].label);
                set_msg(disp, 0);
            }
        }
    }
    if (ui_button(scope_game() ? "Reset to global" : "Reset to defaults"))
        reset_hotkeys();
    if (ui_button("Back"))
        menu_pop();
    if (l_msg[0])
        ui_status_bar(l_msg, l_msg_kind);
    else if (ui_focus_get() == 1)
    {
        char rc[48], t[96];
        cfg_combo_display(g_cfg.recovery_combo, rc, sizeof(rc));
        snprintf(t, sizeof(t), g_cfg.recovery_combo ? "%s also always opens the menu" : "No recovery combo set", rc);
        ui_status_bar(t, 0);
    }
    else
        ui_status_bar(scope_game() ? "Green bar: this game's own hotkey" : "Hold the combo, then let go", 0);
    ui_panel_end(scope_game() ? "A Change   X Reset/Clear   B Back" : "A Change   X Clear   B Back");
    if (back)
        menu_pop();
}

/* ---- dispatch ------------------------------------------------------------------------------------- */

int ctl_owns_page(menu_page p) { return p >= PAGE_CONTROLS && p <= PAGE_CONFIRM_CTL_RESET; }

void ctl_frame(menu_page page, uint32_t pressed, uint32_t held, int back)
{
    switch (page)
    {
        case PAGE_CONTROLS:       page_controls(back); break;
        case PAGE_PRESETS:        page_presets(back); break;
        case PAGE_CONFIRM_PRESET: page_confirm_preset(back); break;
        case PAGE_BUTTONS:        page_map(0, back); break;
        case PAGE_MODLAYER:       page_map(1, back); break;
        case PAGE_CAPTURE:        page_capture(); break;             /* B is a capturable control here */
        case PAGE_TEST:           page_test(back); break;
        case PAGE_HOTKEYS:        page_hotkeys(held, back); break;
        case PAGE_COMBO_CAPTURE:  page_combo_capture(held); break;
        case PAGE_COMBO_CONFIRM:  page_combo_confirm(pressed); break;
        case PAGE_CONFIRM_CTL_RESET: page_confirm_ctl_reset(back); break;
        default: break;
    }
}

void ctl_reset(void)
{
    revert_pending();
    set_msg("", 0);
}

void ctl_menu_closing(void)
{
    if (l_pend.active)
    {
        revert_pending();
        toast_show(TOAST_ERROR, "Menu combo change reverted");
    }
}

void ctl_leave_page(menu_page p)
{
    if (p == PAGE_CONTROLS || p == PAGE_BUTTONS || p == PAGE_MODLAYER || p == PAGE_PRESETS)
        remap_flush();
    if (p == PAGE_HOTKEYS)
        cfg_save();
    if (p == PAGE_CONTROLS || p == PAGE_HOTKEYS)
        set_msg("", 0);
}
