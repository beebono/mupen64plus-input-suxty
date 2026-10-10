/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - remap.c
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
#include "inputfilter.h"
#include "m64p_types.h"
#include "main.h"
#include "remap.h"

/* See remap.h for the design.  Everything here runs on the emulation thread (the menu and GetKeys both do), so there
 * are no locks. */

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
    int           live;           /* this player was reported as present to the core at launch (a pad added later is only used from the next launch) */
    n64_state     st;
} rplayer;

static rplayer  l_pl[REMAP_PLAYERS];
static mapslot  l_maps[PAD_MAX_PADS];
static int      l_nmaps;
static padmap   l_scratch;           /* what the menu edits for a player without a pad (never stored) */
static int      l_enabled;
static int      l_launched;          /* remap_prepare_launch() ran: the players are fixed */
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
 * default mapping; NULL for no pad */
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
        DebugMessage(M64MSG_INFO, "mapping for '%s': from suxty.cfg", lay.name);
    else
    {
        padmap_default(&s->glob, &lay, msg, sizeof(msg));
        DebugMessage(M64MSG_INFO, "mapping for '%s': default", lay.name);
    }
    if (g_cfg.game != NULL && ini_has_section(g_cfg.game, sec))
    {
        /* the game file may override single keys: they win over the global ones */
        s->game = s->glob;
        padmap_load(&s->game, g_cfg.ini, g_cfg.game, sec);
        s->has_game = 1;
        DebugMessage(M64MSG_INFO, "mapping for '%s': this game has its own", lay.name);
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
    l_enabled = 1;
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
        l_pl[p].map = &l_scratch;
        l_pl[p].gsrc_pad = -1;
    }
    reassign();
    return 0;
}

void remap_shutdown(void)
{
    remap_flush();
    l_enabled = 0;
    l_launched = 0;
}

/* ---- evaluation (GetKeys) -------------------------------------------------------------------- */

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
        src = pl->src_pad >= 0 ? pad_joystick(pl->src_pad) : NULL;
        if (src == NULL)
        {
            memset(&pl->st, 0, sizeof(pl->st));
            continue;
        }
        make_io(&io, src);
        pl->map->tilt_pct = g_cfg.soft_tilt_pct;
        padmap_eval(pl->map, &io, &pl->st);
    }
}

/* GetKeys: the N64 state of player p as of the last remap_update(); 0 if the player has no pad */
int remap_player_state(int p, n64_state *out)
{
    memset(out, 0, sizeof(*out));
    if (!l_launched || p < 0 || p >= REMAP_PLAYERS || l_pl[p].src_pad < 0)
        return 0;
    *out = l_pl[p].st;
    return 1;
}

static int       l_probe_on;          /* debug script "probe:": a made-up state for the Test controller page */
static n64_state l_probe;

void remap_set_probe_override(int on, const n64_state *st)
{
    l_probe_on = on;
    if (on && st != NULL)
        l_probe = *st;
}

void remap_probe(int p, n64_state *out)
{
    padmap_io io;

    if (l_probe_on)
    {
        *out = l_probe;
        return;
    }
    SDL_Joystick *src = (p >= 0 && p < REMAP_PLAYERS && l_pl[p].src_pad >= 0) ? pad_joystick(l_pl[p].src_pad) : NULL;
    memset(out, 0, sizeof(*out));
    if (src == NULL)
        return;
    make_io(&io, src);
    inputfilter_bypass(1);                 /* the menu is open: show the real controller */
    l_pl[p].map->tilt_pct = g_cfg.soft_tilt_pct;
    padmap_eval(l_pl[p].map, &io, out);
    inputfilter_bypass(0);
}

/* ---- launch ------------------------------------------------------------------------------------- */

/* Decides who plays (InitiateControllers): the players that have a pad now.  A pad connected later is used by the menu
 * and by the mapping of an existing player, but the core is told about the players only once. */
void remap_prepare_launch(void)
{
    int p, assigned = 0;

    reassign();
    for (p = 0; p < REMAP_PLAYERS; p++)
    {
        l_pl[p].live = l_pl[p].src_pad >= 0;
        if (l_pl[p].live)
        {
            assigned++;
            DebugMessage(M64MSG_INFO, "N64 player %d: '%s'", p + 1, pad_name(l_pl[p].src_pad));
        }
    }
    l_enabled = assigned > 0;
    if (!l_enabled)
        DebugMessage(M64MSG_WARNING, "no usable gamepad: the game gets no controller input");
    l_gen = pad_generation();
    l_launched = 1;
    remap_update();
}

void remap_finish(void)
{
    remap_flush();
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
int  remap_player_live(int p)          { return l_pl[p].live; }

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
    if (l_launched)
        remap_update();                      /* the next poll already sees the change */
}

void remap_flush(void)
{
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
    return l_pl[p].live || !l_launched;
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

/* "Reset to default mapping": the buttons of the pad's default layout (the stick settings and the rest of the pad's
 * choices are kept by padmap_default) */
int remap_default_mapping(int p, char *msg, size_t msgsize)
{
    pad_layout lay;
    if (l_pl[p].src_pad < 0 || pad_layout_get(l_pl[p].src_pad, &lay) != 0)
    {
        snprintf(msg, msgsize, "No controller for player %d", p + 1);
        return -1;
    }
    padmap_default(remap_edit(p), &lay, msg, msgsize);
    remap_commit(p);
    return 0;
}

/* "Save Game Profile": what is in force for every pad model seen and every player becomes this game's own value.
 * Returns the number of mappings and player choices written. */
int remap_pin_game(void)
{
    int i, p, n = 0;
    if (g_cfg.game == NULL)
        return 0;
    for (i = 0; i < l_nmaps; i++)
    {
        char sec[96];
        mapslot *s = &l_maps[i];
        if (!s->has_game)
        {
            s->game = s->glob;
            s->has_game = 1;
        }
        s->draft = 0;
        padmap_section(s->name, sec, sizeof(sec));
        padmap_store(&s->game, g_cfg.game, sec);
        n++;
    }
    for (p = 0; p < REMAP_PLAYERS; p++)
    {
        if (!l_pl[p].has_want_game)
        {
            snprintf(l_pl[p].want_game, sizeof(l_pl[p].want_game), "%s", l_pl[p].want_g);
            l_pl[p].has_want_game = 1;
        }
        ini_set(g_cfg.game, "players", players_key(p), l_pl[p].want_game[0] ? l_pl[p].want_game : "auto");
        n++;
    }
    reassign();
    if (l_launched)
        remap_update();
    return n;
}
