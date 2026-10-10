/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - menu_ctl.c
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

/* The controller and hotkey pages of the pause menu: player / pad choice, the D-pad toggle, button mapping with the
 * capture prompt (including the C-Face shift and Soft Tilt hold buttons), a controller test, and the hotkey combo
 * assignment with its lock-out guards.  Drawn by menu.c's page stack; see menu_int.h. */

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
#include "uitext.h"

#define CAPTURE_TIMEOUT_MS   5000u     /* a control capture gives up after this long without input */
#define COMBO_IDLE_MS        7000u     /* a combo capture gives up after this long without a button */
#define TEST_EXIT_HOLD_MS    3000u     /* Test controller: hold B this long to leave (a tap of B is a button to test) */
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

/* ---- capture of one physical control ------------------------------------------------------------ */

#define CAP_CFACE (-1)                 /* the target of a capture that is not an N64 input: the hold buttons */
#define CAP_TILT  (-2)

static struct {
    int      ctl;                      /* n64_ctl (as the Buttons page lists it), CAP_CFACE or CAP_TILT */
    uint32_t deadline;
} l_cap;

static const char *ctl_title(int ctl, char *buf, size_t size)
{
    if (ctl == CAP_CFACE)
        snprintf(buf, size, "C-Face Shift");
    else if (ctl == CAP_TILT)
        snprintf(buf, size, "Soft Tilt");
    else
        snprintf(buf, size, "N64 %s", n64_ctl_name((n64_ctl) ctl));
    return buf;
}

static void start_capture(int ctl)
{
    int pad = remap_player_pad(l_player);
    if (pad < 0)
    {
        set_msg("No controller for this player", -1);
        return;
    }
    l_cap.ctl = ctl;
    l_cap.deadline = SDL_GetTicks() + CAPTURE_TIMEOUT_MS;
    pad_capture_begin(pad);
    menu_push(PAGE_CAPTURE);
}

static n64_ctl opposite(n64_ctl c)
{
    return c == N64_SLEFT ? N64_SRIGHT : c == N64_SRIGHT ? N64_SLEFT : c == N64_SUP ? N64_SDOWN : N64_SUP;
}

/* binds `s` to the target of the capture; a control can only drive one input (or hold button), so it moves */
static void assign_src(int p, int ctl, const padsrc *s)
{
    padmap *m = remap_edit(p);
    char text[48], from[32] = "";
    char title[48];
    pad_layout lay;
    int have_lay = pad_layout_get(remap_player_pad(p), &lay) == 0;
    int k;

    /* the same control used elsewhere goes: another input, or a hold button */
    for (k = 0; k < N64_NUM; k++)
        if (k != (ctl >= 0 ? (int) padmap_chan(m, (n64_ctl) ctl) : -1) && padsrc_same(&m->src[k], s))
        {
            memset(&m->src[k], 0, sizeof(padsrc));
            snprintf(from, sizeof(from), "%s", n64_ctl_name(padmap_chan(m, (n64_ctl) k)));
        }
    if (ctl != CAP_CFACE && padsrc_same(&m->cface, s))
    {
        padmap_set_cface(m, NULL, &(padsrc) { 0 });
        snprintf(from, sizeof(from), "C-Face Shift");
    }
    if (ctl != CAP_TILT && padsrc_same(&m->softtilt, s))
    {
        memset(&m->softtilt, 0, sizeof(padsrc));
        snprintf(from, sizeof(from), "Soft Tilt");
    }
    if (ctl == CAP_CFACE)
        padmap_set_cface(m, have_lay ? &lay : NULL, s);
    else if (ctl == CAP_TILT)
        m->softtilt = *s;
    else
    {
        n64_ctl c = padmap_chan(m, (n64_ctl) ctl);
        m->src[c] = *s;
        /* "Stick Left" captured as axis 0-: offer axis 0+ for "Stick Right" if that is still free */
        if (ctl >= N64_SLEFT && s->type == PAD_BIND_AXIS)
        {
            n64_ctl o = padmap_chan(m, opposite((n64_ctl) ctl));
            if (m->src[o].type == PAD_BIND_NONE)
            {
                padsrc op = *s;
                op.dir = (int8_t) -s->dir;
                m->src[o] = op;
            }
        }
    }
    remap_commit(p);
    pad_format_src(remap_player_pad(p), s, text, sizeof(text));
    /* "South -> A (was B)": the control, what it drives now, and what it drove before (the status line wraps it when it is long) */
    snprintf(title, sizeof(title), "%s", ctl == CAP_CFACE ? "C-Face Shift" : ctl == CAP_TILT ? "Soft Tilt" : n64_ctl_name((n64_ctl) ctl));
    uitext_remap_msg(l_msg, sizeof(l_msg), text, title, from);
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
        ui_panel_begin_ex("Remap", 10, UI_W_WIDE);
        ui_label_hi(ctl_title(l_cap.ctl, title, sizeof(title)));
        ui_label("Press the control to use");
        ui_label(who);
        ui_progress(left > 0 ? (int) (left * 1000 / (int32_t) CAPTURE_TIMEOUT_MS) : 0);
        ui_label("No Input: Cancel");
    }
    ui_panel_end("Move a stick, press a button");

    if (pad_capture_poll(&s, &pad_idx))
    {
        assign_src(l_player, l_cap.ctl, &s);
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
    static const char *const dpad_names[2] = { "D-pad", "Stick" };
    char nbuf[PAD_MAX_PADS + 2][72];
    const char *names[PAD_MAX_PADS + 2];
    int nn = 0, cur = 0, i, pi = l_player, p;
    const char *want;
    char info[96], title[40];
    const padmap *m;
    int dz, pk, np = pad_count(), mapmark, rows, role;
    pad_layout lay;

    rows = 8 + (scope_game() ? 1 : 0);
    ui_next_panel_width(UI_W_WIDE);
    ui_panel_begin_scroll(menu_scoped_title(title, sizeof(title), "Controls"), rows, 1);
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

    if (ui_item("Buttons", ">") == 1)
        menu_push(PAGE_BUTTONS);
    ui_mark_last(mapmark);
    /* what the D-pad of this pad drives: the N64 D-pad, or the stick (a software swap of the two groups of channels) */
    role = padmap_dpad_is_stick(m, remap_player_pad(p) >= 0 && pad_layout_get(remap_player_pad(p), &lay) == 0 ? &lay : NULL);
    if (ui_choice("D-pad Acts As", &role, dpad_names, 2))
    {
        remap_edit(p)->dpad_swap = !m->dpad_swap;
        remap_commit(p);
        set_msg(role ? "D-pad = stick, stick = D-pad" : "D-pad = N64 D-pad", 1);
    }
    ui_mark_last(mapmark);
    dz = m->deadzone;
    pk = m->peak;
    if (ui_slider("Stick Deadzone", &dz, 0, 40, 1, "%"))
    {
        remap_edit(p)->deadzone = dz;
        remap_commit(p);
    }
    ui_mark_last(mapmark);
    if (ui_slider("Stick Range", &pk, 50, 100, 1, "%"))
    {
        remap_edit(p)->peak = pk;
        remap_commit(p);
    }
    ui_mark_last(mapmark);
    if (ui_item("Test Controller", ">") == 1)
        menu_push(PAGE_TEST);
    if (ui_button_dis("Reset to Default Mapping", remap_player_pad(p) < 0) > 0)
        menu_push(PAGE_CONFIRM_MAPRESET);
    if (scope_game())
    {
        if (ui_button_dis("Reset to Global", !(remap_pad_overridden(p) || remap_player_want_overridden(p))) > 0)
            menu_push(PAGE_CONFIRM_CTL_RESET);
    }
    if (ui_button("Back"))
        menu_pop();

    if (l_msg[0])
        ui_status_bar(l_msg, l_msg_kind);
    else if (!remap_enabled())
        ui_status_bar("No gamepad found", -1);
    else if (ui_focus_mark() != 0)
        ui_status_bar(menu_scope_status(), 0);
    else
    {
        pad_status(info, sizeof(info));
        ui_status_bar(info, 0);
    }
    ui_panel_end(scope_game() ? "A Select  <> Change  X Reset  B Back" : "A Select  <> Change  B Back");
    if (back)
        menu_pop();
}

static void page_confirm_ctl_reset(int back)
{
    ui_panel_begin_ex("Confirm", 6, UI_W_DIALOG);
    ui_grid(2);
    ui_label("Reset this game's");
    ui_label("controls to global?");
    if (menu_yes_no())
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

/* ---- reset to the default mapping ------------------------------------------------------------------ */

static void page_confirm_mapreset(int back)
{
    ui_panel_begin_ex("Confirm", 6, UI_W_DIALOG);
    ui_grid(2);
    ui_label("Reset the mapping?");
    ui_label("(to this pad's default)");
    if (menu_yes_no())
    {
        char msg[96];
        if (remap_default_mapping(l_player, msg, sizeof(msg)) == 0)
            set_msg(msg[0] ? msg : "Back to the default mapping", msg[0] ? 0 : 1);
        else
            set_msg(msg, -1);
        menu_pop();
    }
    ui_panel_end("A Select   B Back");
    if (back)
        menu_pop();
}

/* ---- button mapping ---------------------------------------------------------------------------------- */

static void page_map(int back)
{
    char title[40], text[56];
    int p = l_player, n, r;
    const padmap *m = remap_get(p);
    int pad = remap_player_pad(p);

    snprintf(title, sizeof(title), "Buttons - P%d%s", p + 1, scope_game() ? " (Game)" : "");
    ui_next_panel_width(UI_W_WIDE);
    ui_panel_begin_scroll(title, N64_NUM + 3, 1);
    for (n = 0; n < N64_NUM; n++)
    {
        /* with the D-pad swap on, the row of the D-pad shows (and edits) what the stick has stored, and the other way round */
        const padsrc *s = &m->src[padmap_chan(m, (n64_ctl) n)];
        if (s->type != PAD_BIND_NONE)
            pad_format_src(pad, s, text, sizeof(text));
        else
            snprintf(text, sizeof(text), "-");
        r = ui_item(n64_ctl_name((n64_ctl) n), text);
        if (r == 1)
            start_capture(n);
        else if (r == 2 && s->type != PAD_BIND_NONE)
        {
            memset(&remap_edit(p)->src[padmap_chan(m, (n64_ctl) n)], 0, sizeof(padsrc));
            remap_commit(p);
            snprintf(text, sizeof(text), "%s cleared", n64_ctl_name((n64_ctl) n));
            set_msg(text, 0);
        }
    }
    /* the two hold buttons: while C-Face shift is held the face buttons act as C buttons; Soft Tilt halves the stick */
    if (m->cface.type != PAD_BIND_NONE)
        pad_format_src(pad, &m->cface, text, sizeof(text));
    else
        snprintf(text, sizeof(text), "-");
    r = ui_item("C-Face Shift", text);
    if (r == 1)
        start_capture(CAP_CFACE);
    else if (r == 2 && m->cface.type != PAD_BIND_NONE)
    {
        padmap_set_cface(remap_edit(p), NULL, NULL);
        remap_commit(p);
        set_msg("C-Face Shift cleared", 0);
    }
    if (m->softtilt.type != PAD_BIND_NONE)
        pad_format_src(pad, &m->softtilt, text, sizeof(text));
    else
        snprintf(text, sizeof(text), "-");
    r = ui_item("Soft Tilt", text);
    if (r == 1)
        start_capture(CAP_TILT);
    else if (r == 2 && m->softtilt.type != PAD_BIND_NONE)
    {
        memset(&remap_edit(p)->softtilt, 0, sizeof(padsrc));
        remap_commit(p);
        set_msg("Soft Tilt cleared", 0);
    }
    if (ui_button("Back"))
        menu_pop();
    if (l_msg[0])
        ui_status_bar(l_msg, l_msg_kind);
    else if (ui_focus_get() == N64_NUM)
        ui_status_bar("Hold it: the face buttons act as C", 0);
    else if (ui_focus_get() == N64_NUM + 1)
        ui_status_bar("Hold it: the stick is halved", 0);
    else if (ui_focus_mark() != 0)
        ui_status_bar(menu_scope_status(), 0);
    else
    {
        char info[96];
        pad_status(info, sizeof(info));
        ui_status_bar(info, 0);
    }
    ui_panel_end("A Remap   X Clear   B Back");
    if (back)
        menu_pop();
}

/* ---- controller test ------------------------------------------------------------------------------ */

/* The N64 buttons of the Buttons row, in the order of the row, with the N64's own (short) names */
static const uitext_btn l_test_btns[] = {
    { 1u << N64_A, "A" }, { 1u << N64_B, "B" }, { 1u << N64_Z, "Z" }, { 1u << N64_START, "St" }, { 1u << N64_L, "L" }, { 1u << N64_R, "R" },
};

static uint32_t l_test_hold;           /* when B went down on the Test page (0 = not held) */

static int clamp99(int v) { return v < -99 ? -99 : v > 99 ? 99 : v; }

static void page_test(uint32_t held)
{
    n64_state st;
    char bghost[40], bon[40], dpad[UITEXT_ARROWS_LEN + 1], cpad[UITEXT_ARROWS_LEN + 1], stick[24], l5[48];
    uint32_t b;
    uint32_t now = SDL_GetTicks();
    int permille = 0;

    remap_probe(l_player, &st);
    b = st.buttons;

    /* B is an N64 button here, so a tap must not leave: hold it */
    if (held & PADBIT(PAD_B))
    {
        if (l_test_hold == 0)
            l_test_hold = now ? now : 1;
        permille = (int) ((now - l_test_hold) * 1000u / TEST_EXIT_HOLD_MS);
    }
    else
        l_test_hold = 0;

    ui_next_panel_no_footer();                    /* the hold bar says everything the footer would */
    ui_panel_begin_ex("Test Controller", 10, UI_W_DIALOG);
    {
        /* every cell has a fixed width: an arrow keeps its place (dim when up), nothing shifts as inputs change */
        ui_cell top[2], mid[3], bt;
        uitext_arrows((b >> N64_DUP) & 1, (b >> N64_DDOWN) & 1, (b >> N64_DLEFT) & 1, (b >> N64_DRIGHT) & 1, dpad);
        uitext_arrows((b >> N64_CUP) & 1, (b >> N64_CDOWN) & 1, (b >> N64_CLEFT) & 1, (b >> N64_CRIGHT) & 1, cpad);
        top[0] = (ui_cell) { "D-pad", UITEXT_ARROWS_ALL, dpad };
        top[1] = (ui_cell) { "C", UITEXT_ARROWS_ALL, cpad };
        ui_cells(top, 2);

        /* all the N64 buttons, always in the same places: dim, and lit while they are down */
        uitext_btn_cells(l_test_btns, (int) (sizeof(l_test_btns) / sizeof(l_test_btns[0])), b, bghost, bon, sizeof(bghost));
        bt = (ui_cell) { "Buttons", bghost, bon };
        ui_cells(&bt, 1);

        snprintf(stick, sizeof(stick), "%+3d %+3d", clamp99(st.sx * 80 / 32767), clamp99(-st.sy * 80 / 32767));
        mid[0] = (ui_cell) { "Stick", NULL, stick };
        mid[1] = (ui_cell) { NULL, "C-Face", st.cshift ? "C-Face" : "" };
        mid[2] = (ui_cell) { NULL, "Soft Tilt", st.tilt ? "Soft Tilt" : "" };
        ui_cells(mid, 3);
    }
    snprintf(l5, sizeof(l5), "%s", remap_player_pad(l_player) >= 0 ? "Press buttons on the pad" : "No pad for this player");
    ui_label(l5);
    ui_hold_bar("Hold B 3 Sec. to Go Back", permille);
    ui_panel_end(NULL);
    if (l_test_hold != 0 && now - l_test_hold >= TEST_EXIT_HOLD_MS)
    {
        l_test_hold = 0;
        menu_pop();
    }
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
    { "hotkey_button", "Hotkey Button", 0 },
    { "menu_combo",    "Menu",          0 },
    { "quick_save",    "Quick-Save",    1 },
    { "quick_load",    "Quick-Load",    1 },
    { "next_slot",     "Next Slot",     1 },
    { "prev_slot",     "Prev Slot",     1 },
    { "fast_forward",  "Fast-Forward",  1 },
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
        set_msg("This game has its own menu combo", -1);
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

    ui_panel_begin_ex("Hotkey", 10, UI_W_WIDE);
    ui_label_hi(title);
    ui_label(l_cc.idx == 0 ? "Press the hotkey button" : "Hold the buttons, then let go");
    ui_label_hi((l_cc.acc | eff) ? disp : "...");
    ui_progress(left > 0 ? (int) (left * 1000 / (int32_t) COMBO_IDLE_MS) : 0);
    ui_label("No Input: Cancel");
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
    ui_panel_begin_ex("Keep This Menu Combo?", 10, UI_W_WIDE);
    ui_label("Press the new combo now:");
    ui_label_hi(disp);
    ui_progress(left > 0 ? (int) (left * 1000 / (int32_t) CONFIRM_TIMEOUT_MS) : 0);
    ui_label(t);
    ui_label("(B: Revert)");
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

    ui_panel_begin_scroll(menu_scoped_title(title, sizeof(title), "Hotkeys"), HK_COUNT + 2, 1);
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
    if (ui_button_center("Reset All"))
        reset_hotkeys();
    if (ui_button("Back"))
        menu_pop();
    if (l_msg[0])
        ui_status_bar(l_msg, l_msg_kind);
    else if (ui_focus_get() == 1)
    {
        char rc[48], t[96];
        cfg_combo_display(g_cfg.recovery_combo, rc, sizeof(rc));
        snprintf(t, sizeof(t), g_cfg.recovery_combo ? "%s always opens the menu" : "No recovery combo set", rc);
        ui_status_bar(t, 0);
    }
    else if (ui_focus_mark() != 0)
        ui_status_bar(menu_scope_status(), 0);
    else
        ui_status_bar("Hold the combo, then let go", 0);
    ui_panel_end(scope_game() ? "A Change  X Reset/Clear  B Back" : "A Change  X Clear  B Back");
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
        case PAGE_CONFIRM_MAPRESET: page_confirm_mapreset(back); break;
        case PAGE_BUTTONS:        page_map(back); break;
        case PAGE_CAPTURE:        page_capture(); break;             /* B is a capturable control here */
        case PAGE_TEST:           page_test(held); break;            /* ... and a button to test here: hold it to leave */
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
    l_test_hold = 0;
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
    if (p == PAGE_CONTROLS || p == PAGE_BUTTONS)
        remap_flush();
    if (p == PAGE_HOTKEYS)
        cfg_save();
    if (p == PAGE_CONTROLS || p == PAGE_HOTKEYS)
        set_msg("", 0);
    if (p == PAGE_TEST)
        l_test_hold = 0;
}
