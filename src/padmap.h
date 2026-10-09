/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-ui-suxty - padmap.h
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


/* The N64 controller mapping model: which physical control drives which N64 input.
 *
 * This file is pure logic (no SDL calls; the physical state comes in through padmap_io), so it can be
 * unit-tested and so per-game profiles (M4, remap.c) can layer on it: a padmap is loaded from, and stored to,
 * an ini_t section, and padmap_load() takes an optional overlay ini (the per-game file) that wins over the global
 * one.  Mappings belong to a pad *model* (its SDL name), not to a player slot, so swapping which pad is player 1
 * keeps every pad's own mapping.
 *
 * Model
 *   - 14 N64 buttons (A B Z Start L R, D-pad x4, C x4) and the 4 directions of the analog stick.
 *   - Each N64 input has one physical source (padsrc): a joystick button, one direction of a hat, or one half
 *     of an axis (so triggers and "right stick up" work, and a stick direction can be a button).
 *   - A second layer, active while the optional *modifier* control is held: where layer 1 defines a source it
 *     replaces layer 0 for that N64 input, and any layer-0 input whose physical control is used by layer 1 (or
 *     is the modifier itself) is silenced meanwhile.  That is how "face buttons -> C buttons while R2 is held"
 *     works without anything else having to know about modifiers.
 *   - Analog stick: deadzone and range ("peak") in percent of the travel of each half axis.
 */

#ifndef PADMAP_H
#define PADMAP_H

#include <stddef.h>
#include <stdint.h>

#include "inifile.h"
#include "pad.h"

typedef enum {
    N64_A, N64_B, N64_Z, N64_START, N64_L, N64_R,
    N64_DUP, N64_DDOWN, N64_DLEFT, N64_DRIGHT,
    N64_CUP, N64_CDOWN, N64_CLEFT, N64_CRIGHT,
    N64_SLEFT, N64_SRIGHT, N64_SUP, N64_SDOWN,       /* the analog stick, one direction each */
    N64_NUM
} n64_ctl;

#define N64_NBUTTONS 14                              /* N64_A .. N64_CRIGHT: digital */
#define PADMAP_LAYERS 2
#define PADMAP_DEFAULT_DEADZONE 12
#define PADMAP_DEFAULT_PEAK 100

typedef struct {
    padsrc src[PADMAP_LAYERS][N64_NUM];
    padsrc modifier;                  /* PAD_BIND_NONE = no modifier layer */
    int    deadzone;                  /* % of a half axis (0-50) */
    int    peak;                      /* % of a half axis that counts as full deflection (50-100) */
    char   name[64];                  /* the pad this mapping was made for */
    char   preset[24];                /* the preset it started from (information only) */
} padmap;

const char *n64_ctl_name(n64_ctl c);     /* "A", "C-Up", "Stick Left" */
const char *n64_ctl_key(n64_ctl c);      /* ini key: "a", "c_up", "stick_left" */

/* ---- physical state access ---- */
typedef struct {
    int  (*button)(void *ctx, int index);
    int  (*hat)(void *ctx, int index);              /* SDL_HAT_* bits */
    int  (*axis)(void *ctx, int index);             /* -32768..32767 */
    int  (*axis_rest)(void *ctx, int index);        /* the value at rest (0 for sticks, -32768 for most triggers) */
    void *ctx;
} padmap_io;

typedef struct {
    uint32_t buttons;                 /* bit n = N64 button n (n64_ctl) */
    int      sx, sy;                  /* stick, -32767..32767; +x right, +y DOWN (SDL convention) */
    int      mod;                     /* the modifier layer is active */
} n64_state;

#define PADMAP_BUTTON_THRESHOLD 16384          /* a half axis counts as a pressed button beyond 50 % */

/* 0..32767: how far this source is pressed (digital sources give 0 or 32767) */
int  padsrc_activation(const padsrc *s, const padmap_io *io);
int  padsrc_same(const padsrc *a, const padsrc *b);      /* same physical control (axes: same half) */
void padmap_eval(const padmap *m, const padmap_io *io, n64_state *out);
int  padmap_shape(const padmap *m, int activation);      /* deadzone + range applied to one half axis, 0..32767 */

/* ---- construction ---- */
void padmap_clear(padmap *m);                            /* nothing bound; default deadzone / range */

enum { PRESET_DEFAULT, PRESET_RSTICK_C, PRESET_FACE_C, PRESET_DPAD_STICK, PRESET_SWAP_STICKS, PRESET_NUM };
const char *padmap_preset_name(int preset);
const char *padmap_preset_desc(int preset);
/* Rebuilds the bindings of `m` for `preset` on the pad described by `lay` (deadzone and range are kept).
 * Returns 0, or -1 with `msg` explaining why the pad cannot do it (then `m` is untouched).  msg may also be
 * filled on success with a remark ("no right stick: ..."). */
int  padmap_apply_preset(padmap *m, int preset, const pad_layout *lay, char *msg, size_t msgsize);

/* ---- persistence: one ini section per pad model, "pad:<pad name>" (suxty.cfg now, a per-game file later) ---- */
void padmap_section(const char *pad_name, char *out, size_t size);      /* "pad:retrogame_joypad" */
/* Reads the section.  Keys absent from both inis keep the value already in `m`.  `overlay` (may be NULL) wins over
 * `ini`.  Returns 1 if a stored mapping was found, 0 if there is none. */
int  padmap_load(padmap *m, const ini_t *ini, const ini_t *overlay, const char *section);
void padmap_store(const padmap *m, ini_t *ini, const char *section);

int  padsrc_parse(const char *text, padsrc *out);        /* "button:3", "hat:0:up", "axis:2-", "none" -> 0 ok */
void padsrc_format(const padsrc *s, char *out, size_t size);

#endif /* PADMAP_H */
