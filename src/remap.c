/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-ui-suxty - remap.c
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
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <SDL.h>

#include "cfg.h"
#include "core_interface.h"
#include "inputfilter.h"
#include "m64p_types.h"
#include "main.h"
#include "remap.h"

/* See remap.h for the design.  Everything here runs on the emulation thread (the menu and the input plugin's
 * GetKeys both do), so there are no locks. */

/* A pad model's mapping, found by the pad's SDL name (so it follows the pad when players are swapped).  Two layers
 * (M4): `glob` is the one in suxty.cfg, `game` the one in the game's file (valid when has_game).  What the game
 * plays with is `game` if there is one, else `glob`; the menu edits the layer of the current scope (g_cfg.scope). */
typedef struct {
    char   name[64];
    padmap glob;
    padmap game;
    int    has_game;
    int    draft;             /* remap_edit() copied glob into game; remap_commit() makes it real */
} mapslot;

typedef struct {
    padmap       *map;            /* this player's pad's EFFECTIVE mapping (into l_maps), or &l_scratch with no pad */
    char          want[64];       /* EFFECTIVE choice: "" = auto, "none", or the name of a pad */
    char          want_g[64];     /* the global choice */
    char          want_game[64];  /* the game's choice, valid if has_want_game */
    int           has_want_game;
    int           src_pad;        /* pad.c index of the pad that feeds this player, -1 = none */
    int           gsrc_pad;       /* ... and the one the GLOBAL choices would give (what mupen64plus.cfg describes) */
    SDL_Joystick *plug_js;        /* the joystick the input plugin reads for this player (fixed at launch) */
    int           plug_dev;       /* its SDL device index, -1 = this player has no pad */
    n64_state     st;
} rplayer;

static rplayer  l_pl[REMAP_PLAYERS];
static mapslot  l_maps[PAD_MAX_PADS];
static int      l_nmaps;
static padmap   l_scratch;           /* what the menu edits for a player without a pad (never stored) */
static int      l_enabled;
static int      l_launched;          /* plugin config exported for a launch */
static int      l_virtual_live;      /* the virtual layout is in the plugin's in-memory config right now */
static int      l_save_core = 1;
static int      l_export_pending;
static unsigned l_hits;
static int      l_gen;               /* pad_generation() the assignment was made for */

int remap_enabled(void) { return l_enabled; }

/* ---- physical state, as seen through the input filter ----------------------------------------- */

static int io_button(void *c, int i)    { return inputfilter_phys_button((SDL_Joystick *) c, i); }
static int io_hat(void *c, int i)       { return inputfilter_phys_hat((SDL_Joystick *) c, i); }
static int io_axis(void *c, int i)      { return inputfilter_phys_axis((SDL_Joystick *) c, i); }
static int io_axis_rest(void *c, int i) { return inputfilter_phys_axis_rest((SDL_Joystick *) c, i); }

static void make_io(padmap_io *io, SDL_Joystick *js)
{
    io->button = io_button;
    io->hat = io_hat;
    io->axis = io_axis;
    io->axis_rest = io_axis_rest;
    io->ctx = js;
}

/* ---- pad assignment ----------------------------------------------------------------------------- */

/* explicit choices first, then the unassigned players take the remaining pads in order */
static void assign_pads(int src[REMAP_PLAYERS], const char *want[REMAP_PLAYERS])
{
    int used[PAD_MAX_PADS] = { 0 };
    int n = pad_count(), p, i;

    for (p = 0; p < REMAP_PLAYERS; p++)
    {
        src[p] = -1;
        if (strcmp(want[p], "none") == 0 || want[p][0] == 0)
            continue;
        for (i = 0; i < n && i < PAD_MAX_PADS; i++)
            if (!used[i] && strcmp(pad_name(i), want[p]) == 0)
            {
                src[p] = i;
                used[i] = 1;
                break;
            }
        /* a named pad that is not connected: fall back to automatic below */
    }
    for (p = 0; p < REMAP_PLAYERS; p++)
    {
        if (src[p] >= 0 || strcmp(want[p], "none") == 0)
            continue;
        for (i = 0; i < n && i < PAD_MAX_PADS; i++)
        {
            pad_layout lay;
            if (!used[i] && pad_layout_get(i, &lay) == 0 && lay.plausible)    /* keyboards etc. that show up as joysticks are skipped */
            {
                src[p] = i;
                used[i] = 1;
                break;
            }
        }
    }
}

/* the mapping slot of the pad model `pad` (a pad.c index): both layers loaded from suxty.cfg / the game file, or the
 * default preset; NULL for no pad */
static mapslot *slot_for_pad(int pad)
{
    pad_layout lay;
    char sec[96], msg[64];
    int i;
    mapslot *s;

    if (pad < 0 || pad_layout_get(pad, &lay) != 0)
        return NULL;
    for (i = 0; i < l_nmaps; i++)
        if (strcmp(l_maps[i].name, lay.name) == 0)
            return &l_maps[i];
    if (l_nmaps >= PAD_MAX_PADS)
        return NULL;
    s = &l_maps[l_nmaps++];
    memset(s, 0, sizeof(*s));
    snprintf(s->name, sizeof(s->name), "%s", lay.name);
    padmap_clear(&s->glob);
    snprintf(s->glob.name, sizeof(s->glob.name), "%s", lay.name);
    padmap_section(lay.name, sec, sizeof(sec));
    if (padmap_load(&s->glob, g_cfg.ini, NULL, sec))
        DebugMessage(M64MSG_INFO, "mapping for '%s': %s (from suxty.cfg)", lay.name, s->glob.preset[0] ? s->glob.preset : "custom");
    else
    {
        padmap_apply_preset(&s->glob, PRESET_DEFAULT, &lay, msg, sizeof(msg));
        DebugMessage(M64MSG_INFO, "mapping for '%s': default preset", lay.name);
    }
    if (g_cfg.game != NULL && ini_has_section(g_cfg.game, sec))
    {
        /* the game file may override single keys: they win over the global ones */
        s->game = s->glob;
        padmap_load(&s->game, g_cfg.ini, g_cfg.game, sec);
        s->has_game = 1;
        DebugMessage(M64MSG_INFO, "mapping for '%s': this game has its own (%s)", lay.name, s->game.preset[0] ? s->game.preset : "custom");
    }
    return s;
}

static padmap *effective_map(mapslot *s)
{
    return s == NULL ? &l_scratch : s->has_game ? &s->game : &s->glob;
}

static void reassign(void)
{
    int src[REMAP_PLAYERS], gsrc[REMAP_PLAYERS], p;
    const char *w[REMAP_PLAYERS], *wg[REMAP_PLAYERS];
    for (p = 0; p < REMAP_PLAYERS; p++)
    {
        w[p] = l_pl[p].has_want_game ? l_pl[p].want_game : l_pl[p].want_g;
        wg[p] = l_pl[p].want_g;
        snprintf(l_pl[p].want, sizeof(l_pl[p].want), "%s", w[p]);
    }
    assign_pads(src, w);
    assign_pads(gsrc, wg);
    for (p = 0; p < REMAP_PLAYERS; p++)
    {
        l_pl[p].src_pad = src[p];
        l_pl[p].gsrc_pad = gsrc[p];
        l_pl[p].map = effective_map(slot_for_pad(src[p]));
        (void) slot_for_pad(gsrc[p]);            /* loaded, for the export of the global form */
    }
}

static const char *players_key(int p)
{
    static const char *const k[REMAP_PLAYERS] = { "p1", "p2", "p3", "p4" };
    return k[p];
}

int remap_init(void)
{
    int p;

    memset(l_pl, 0, sizeof(l_pl));
    l_nmaps = 0;
    padmap_clear(&l_scratch);
    l_enabled = ini_get_bool(g_cfg.ini, "input", "remap_input", 1);
    for (p = 0; p < REMAP_PLAYERS; p++)
    {
        const char *w = ini_get(g_cfg.ini, "players", players_key(p), "auto");
        if (strcasecmp(w, "auto") == 0)
            w = "";
        snprintf(l_pl[p].want_g, sizeof(l_pl[p].want_g), "%s", w);
        if (g_cfg.game != NULL && ini_has(g_cfg.game, "players", players_key(p)))
        {
            w = ini_get(g_cfg.game, "players", players_key(p), "auto");
            if (strcasecmp(w, "auto") == 0)
                w = "";
            snprintf(l_pl[p].want_game, sizeof(l_pl[p].want_game), "%s", w);
            l_pl[p].has_want_game = 1;
        }
        l_pl[p].plug_dev = -1;
        l_pl[p].map = &l_scratch;
        l_pl[p].gsrc_pad = -1;
    }
    reassign();
    if (!l_enabled)
        DebugMessage(M64MSG_INFO, "controller remap layer disabled ([input] remap_input = false)");
    return 0;
}

void remap_shutdown(void)
{
    remap_flush();
    l_enabled = 0;
    l_launched = 0;
}

/* ---- evaluation (called by the SDL overrides) ---------------------------------------------------- */

int remap_player_for(SDL_Joystick *js)
{
    int p;
    if (!l_launched || js == NULL)
        return -1;
    for (p = 0; p < REMAP_PLAYERS; p++)
        if (l_pl[p].plug_js == js)
            return p;
    /* the plugin re-opened a controller after it was unplugged and plugged back in: a new handle for the same device */
    for (p = 0; p < REMAP_PLAYERS; p++)
        if (l_pl[p].plug_dev >= 0 && l_pl[p].plug_js != NULL &&
            SDL_JoystickInstanceID(js) == SDL_JoystickGetDeviceInstanceID(l_pl[p].plug_dev))
        {
            l_pl[p].plug_js = js;
            return p;
        }
    return -1;
}

void remap_update(void)
{
    int p;
    if (!l_launched)
        return;
    if (pad_generation() != l_gen)
    {
        l_gen = pad_generation();             /* hot-plug: who is player 1 now? */
        reassign();
    }
    for (p = 0; p < REMAP_PLAYERS; p++)
    {
        rplayer *pl = &l_pl[p];
        padmap_io io;
        SDL_Joystick *src;
        if (pl->plug_js == NULL)
            continue;
        src = pl->src_pad >= 0 ? pad_joystick(pl->src_pad) : NULL;
        if (src == NULL)
        {
            memset(&pl->st, 0, sizeof(pl->st));
            continue;
        }
        make_io(&io, src);
        padmap_eval(pl->map, &io, &pl->st);
    }
}

int remap_virtual_button(int p, int button)
{
    l_hits++;
    return (button >= 0 && button < N64_NBUTTONS) ? (int) ((l_pl[p].st.buttons >> button) & 1u) : 0;
}

int remap_virtual_axis(int p, int axis)
{
    l_hits++;
    return axis == 0 ? l_pl[p].st.sx : axis == 1 ? l_pl[p].st.sy : 0;
}

unsigned remap_virtual_hits(void) { return l_hits; }

void remap_probe(int p, n64_state *out)
{
    padmap_io io;
    SDL_Joystick *src = (p >= 0 && p < REMAP_PLAYERS && l_pl[p].src_pad >= 0) ? pad_joystick(l_pl[p].src_pad) : NULL;
    memset(out, 0, sizeof(*out));
    if (src == NULL)
        return;
    make_io(&io, src);
    inputfilter_bypass(1);                 /* the menu is open: show the real controller */
    padmap_eval(l_pl[p].map, &io, out);
    inputfilter_bypass(0);
}

/* ---- the plugin's Input-SDL-ControlN sections ---------------------------------------------------- */

static void cfg_str(m64p_handle h, const char *key, const char *val) { (*ConfigSetParameter)(h, key, M64TYPE_STRING, val); }
static void cfg_int(m64p_handle h, const char *key, int val)         { (*ConfigSetParameter)(h, key, M64TYPE_INT, &val); }
static void cfg_bool(m64p_handle h, const char *key, int val)        { (*ConfigSetParameter)(h, key, M64TYPE_BOOL, &val); }

static const char *hat_dir(int mask)
{
    return mask == SDL_HAT_UP ? "Up" : mask == SDL_HAT_DOWN ? "Down" : mask == SDL_HAT_LEFT ? "Left" : "Right";
}

/* "button(1)", "axis(2+)", "hat(0 Up)" */
static void fmt_button(const padsrc *s, char *out, size_t size)
{
    switch (s->type)
    {
        case PAD_BIND_BUTTON: snprintf(out, size, "button(%d)", s->index); break;
        case PAD_BIND_AXIS:   snprintf(out, size, "axis(%d%c)", s->index, s->dir < 0 ? '-' : '+'); break;
        case PAD_BIND_HAT:    snprintf(out, size, "hat(%d %s)", s->index, hat_dir(s->hat_mask)); break;
        default:              out[0] = 0; break;
    }
}

/* the stick: "axis(0-,0+)", "button(a,b)", "hat(0 Left Right)"; the stock plugin takes a, b of one kind each */
static void fmt_stick(const padsrc *a, const padsrc *b, char *out, size_t size)
{
    size_t n = 0;
    out[0] = 0;
    if (a->type == PAD_BIND_BUTTON && b->type == PAD_BIND_BUTTON)
        n += (size_t) snprintf(out + n, size - n, "button(%d,%d) ", a->index, b->index);
    if (a->type == PAD_BIND_AXIS && b->type == PAD_BIND_AXIS)
        n += (size_t) snprintf(out + n, size - n, "axis(%d%c,%d%c) ", a->index, a->dir < 0 ? '-' : '+', b->index, b->dir < 0 ? '-' : '+');
    if (a->type == PAD_BIND_HAT && b->type == PAD_BIND_HAT && a->index == b->index)
        n += (size_t) snprintf(out + n, size - n, "hat(%d %s %s) ", a->index, hat_dir(a->hat_mask), hat_dir(b->hat_mask));
    if (n > 0 && out[n - 1] == ' ')
        out[n - 1] = 0;
}

/* plugin parameter names, in n64_ctl order for the 14 buttons */
static const char *const l_plug_key[N64_NBUTTONS] = {
    "A Button", "B Button", "Z Trig", "Start", "L Trig", "R Trig",
    "DPad U", "DPad D", "DPad L", "DPad R", "C Button U", "C Button D", "C Button L", "C Button R"
};

/* virt: the virtual layout the plugin reads at start-up, for the pads this game uses.  Otherwise the standard form of
 * the GLOBAL configuration (global player choices, global mappings), which is all that mupen64plus.cfg ever holds: a
 * game's own choices live in its game file only. */
static void write_section(int p, int virt)
{
    char name[32], buf[128];
    m64p_handle h;
    rplayer *pl = &l_pl[p];
    mapslot *gs = slot_for_pad(pl->gsrc_pad);
    const padmap *m = virt ? pl->map : (gs != NULL ? &gs->glob : &l_scratch);
    int dev = virt ? pl->plug_dev : (pl->gsrc_pad >= 0 ? pad_device_index(pl->gsrc_pad) : -1);
    int pad = virt ? pl->src_pad : pl->gsrc_pad;
    int n;

    snprintf(name, sizeof(name), "Input-SDL-Control%d", p + 1);
    if ((*ConfigOpenSection)(name, &h) != M64ERR_SUCCESS)
        return;
    {
        float ver = 0;
        if ((*ConfigGetParameter)(h, "version", M64TYPE_FLOAT, &ver, sizeof(ver)) != M64ERR_SUCCESS)
        {
            ver = 2.0f;
            (*ConfigSetParameter)(h, "version", M64TYPE_FLOAT, &ver);
        }
    }
    cfg_int(h, "mode", 0);                                  /* fully manual: what is below is used as it is */
    cfg_int(h, "device", dev);
    cfg_str(h, "name", dev >= 0 && pad >= 0 ? pad_name(pad) : "");
    cfg_bool(h, "plugged", dev >= 0);
    cfg_bool(h, "mouse", 0);
    cfg_str(h, "Mempak switch", "");
    cfg_str(h, "Rumblepak switch", "");
    if (virt)
    {
        for (n = 0; n < N64_NBUTTONS; n++)
        {
            snprintf(buf, sizeof(buf), "button(%d)", n);
            cfg_str(h, l_plug_key[n], buf);
        }
        cfg_str(h, "X Axis", "axis(0-,0+)");
        cfg_str(h, "Y Axis", "axis(1-,1+)");
        cfg_str(h, "AnalogDeadzone", "0,0");
        cfg_str(h, "AnalogPeak", "32767,32767");
    }
    else
    {
        for (n = 0; n < N64_NBUTTONS; n++)
        {
            fmt_button(&m->src[0][n], buf, sizeof(buf));
            cfg_str(h, l_plug_key[n], buf);
        }
        fmt_stick(&m->src[0][N64_SLEFT], &m->src[0][N64_SRIGHT], buf, sizeof(buf));
        cfg_str(h, "X Axis", buf);
        fmt_stick(&m->src[0][N64_SUP], &m->src[0][N64_SDOWN], buf, sizeof(buf));
        cfg_str(h, "Y Axis", buf);
        snprintf(buf, sizeof(buf), "%d,%d", m->deadzone * 32768 / 100, m->deadzone * 32768 / 100);
        cfg_str(h, "AnalogDeadzone", buf);
        snprintf(buf, sizeof(buf), "%d,%d", m->peak * 32768 / 100, m->peak * 32768 / 100);
        cfg_str(h, "AnalogPeak", buf);
    }
}

static void write_all(int virt)
{
    int p;
    for (p = 0; p < REMAP_PLAYERS; p++)
        write_section(p, virt);
    l_virtual_live = virt;
}

static void save_sections(void)
{
    int p;
    char name[32];
    if (!l_save_core || ConfigSaveSection == NULL)
        return;
    for (p = 0; p < REMAP_PLAYERS; p++)
    {
        snprintf(name, sizeof(name), "Input-SDL-Control%d", p + 1);
        if ((*ConfigSaveSection)(name) != M64ERR_SUCCESS)
        {
            DebugMessage(M64MSG_WARNING, "couldn't save config section %s", name);
            return;
        }
    }
}

void remap_set_save_core_config(int save) { l_save_core = save; }

void remap_prepare_launch(void)
{
    int p, assigned = 0;

    if (!l_enabled)
        return;
    reassign();
    for (p = 0; p < REMAP_PLAYERS; p++)
        if (l_pl[p].src_pad >= 0)
            assigned++;
    if (assigned == 0)
    {
        DebugMessage(M64MSG_INFO, "no usable gamepad: leaving the input plugin's own configuration alone");
        l_enabled = 0;
        return;
    }
    for (p = 0; p < REMAP_PLAYERS; p++)
    {
        rplayer *pl = &l_pl[p];
        pl->plug_dev = pl->src_pad >= 0 ? pad_device_index(pl->src_pad) : -1;
        pl->plug_js = pl->src_pad >= 0 ? pad_joystick(pl->src_pad) : NULL;
        if (pl->src_pad >= 0)
            DebugMessage(M64MSG_INFO, "N64 player %d: '%s' (SDL device %d)", p + 1, pad_name(pl->src_pad), pl->plug_dev);
    }
    /* persist the standard form first (so it is on disk whatever happens later) ... */
    write_all(0);
    save_sections();
    remap_flush();
    /* ... then give the plugin its virtual layout for InitiateControllers */
    write_all(1);
    l_gen = pad_generation();
    l_launched = 1;
    remap_update();
}

void remap_after_plugin_init(void)
{
    if (l_launched && l_virtual_live)
        write_all(0);
}

void remap_finish(void)
{
    remap_flush();
    if (l_launched && l_virtual_live)
        write_all(0);
    l_launched = 0;
}

/* ---- menu API ------------------------------------------------------------------------------------- */

/* The menu works on one layer at a time (g_cfg.scope): the global mapping/choices, or the game's.  What the game plays
 * with is the effective layer (game wins), updated by remap_commit(). */
static int scope_game(void)
{
    return g_cfg.scope == CFG_SCOPE_GAME && g_cfg.game != NULL;
}

static mapslot *player_slot(int p)
{
    return slot_for_pad(l_pl[p].src_pad);
}

const padmap *remap_get(int p)
{
    mapslot *s = player_slot(p);
    if (s == NULL)
        return &l_scratch;
    return scope_game() && s->has_game ? &s->game : &s->glob;
}

padmap *remap_edit(int p)
{
    mapslot *s = player_slot(p);
    if (s == NULL)
        return &l_scratch;
    if (!scope_game())
        return &s->glob;
    if (!s->has_game && !s->draft)
    {
        s->game = s->glob;              /* copy on write: the game starts from the global mapping */
        s->draft = 1;
    }
    return &s->game;
}

int  remap_player_pad(int p)           { return l_pl[p].src_pad; }
int  remap_player_live(int p)          { return l_pl[p].plug_js != NULL; }

const char *remap_player_want(int p)
{
    return scope_game() && l_pl[p].has_want_game ? l_pl[p].want_game : l_pl[p].want_g;
}

int remap_player_want_overridden(int p)  { return l_pl[p].has_want_game; }

int remap_pad_overridden(int p)
{
    mapslot *s = player_slot(p);
    return s != NULL && s->has_game;
}

static void refresh_effective(void)
{
    int p;
    for (p = 0; p < REMAP_PLAYERS; p++)
        l_pl[p].map = effective_map(slot_for_pad(l_pl[p].src_pad));
}

/* persist the player's pad's mapping and the player choices, into the layer of the scope */
void remap_commit(int p)
{
    int i;
    mapslot *s = player_slot(p);
    if (s != NULL)
    {
        char sec[96];
        padmap_section(s->name, sec, sizeof(sec));
        if (scope_game())
        {
            s->has_game = 1;
            s->draft = 0;
            padmap_store(&s->game, g_cfg.game, sec);
        }
        else
            padmap_store(&s->glob, g_cfg.ini, sec);
    }
    for (i = 0; i < REMAP_PLAYERS; i++)
    {
        if (scope_game())
        {
            if (l_pl[i].has_want_game)
                ini_set(g_cfg.game, "players", players_key(i), l_pl[i].want_game[0] ? l_pl[i].want_game : "auto");
        }
        else
            ini_set(g_cfg.ini, "players", players_key(i), l_pl[i].want_g[0] ? l_pl[i].want_g : "auto");
    }
    refresh_effective();
    l_export_pending = 1;
    if (l_launched)
        remap_update();                      /* the next poll already sees the change */
}

void remap_flush(void)
{
    if (l_export_pending)
    {
        l_export_pending = 0;
        if (l_enabled && l_launched && !l_virtual_live)
        {
            /* memory holds the real form after the plugin has started; refresh it and write the sections */
            int pp;
            for (pp = 0; pp < REMAP_PLAYERS; pp++)
                write_section(pp, 0);
            save_sections();
        }
    }
    cfg_save();
}

int remap_set_pad(int p, const char *want)
{
    if (scope_game())
    {
        snprintf(l_pl[p].want_game, sizeof(l_pl[p].want_game), "%s", want ? want : "");
        l_pl[p].has_want_game = 1;
    }
    else
        snprintf(l_pl[p].want_g, sizeof(l_pl[p].want_g), "%s", want ? want : "");
    reassign();
    remap_commit(p);
    return l_pl[p].plug_js != NULL || !l_launched;
}

/* "Reset to global" for the controllers of this game: the player choices and every pad's mapping in the game file */
int remap_reset_game(void)
{
    int p, i, n = 0;
    if (g_cfg.game == NULL)
        return 0;
    for (p = 0; p < REMAP_PLAYERS; p++)
        if (l_pl[p].has_want_game)
        {
            ini_remove(g_cfg.game, "players", players_key(p));
            l_pl[p].has_want_game = 0;
            n++;
        }
    for (i = 0; i < l_nmaps; i++)
        if (l_maps[i].has_game || l_maps[i].draft)
        {
            char sec[96];
            padmap_section(l_maps[i].name, sec, sizeof(sec));
            ini_remove_section(g_cfg.game, sec);
            l_maps[i].has_game = l_maps[i].draft = 0;
            n++;
        }
    reassign();
    if (l_launched)
        remap_update();
    return n;
}

int remap_apply_preset(int p, int preset, char *msg, size_t msgsize)
{
    pad_layout lay;
    if (l_pl[p].src_pad < 0 || pad_layout_get(l_pl[p].src_pad, &lay) != 0)
    {
        snprintf(msg, msgsize, "No controller for player %d", p + 1);
        return -1;
    }
    if (padmap_apply_preset(remap_edit(p), preset, &lay, msg, msgsize) != 0)
        return -1;
    remap_commit(p);
    return 0;
}

/* 1 = the plugin reads the virtual pad, -1 = players are plugged but it never did (a plugin that bypasses SDL's
 * joystick calls), 0 = nothing to check */
int remap_health_check(void)
{
    int p, plugged = 0;
    if (!l_launched)
        return 0;
    for (p = 0; p < REMAP_PLAYERS; p++)
        if (l_pl[p].plug_js != NULL)
            plugged++;
    if (!plugged)
        return 0;
    return l_hits > 0 ? 1 : -1;
}
