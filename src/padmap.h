/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - padmap.h
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
 *   - D-pad swap: a software swap of the four stick channels with the four D-pad channels (what the D-pad of the pad
 *     drives, the other way round), so one toggle makes the D-pad act as the stick or the stick as the D-pad.
 *   - C-Face shift: an optional hold button.  While it is held the four face buttons (sources of their own, set from the
 *     pad's layout) act as C-Down / C-Right / C-Left / C-Up, and what they normally do is silenced meanwhile.
 *   - Soft Tilt: an optional hold button.  While it is held the analog stick output (after deadzone and range, so a stick
 *     with little travel and a D-pad driving the stick at full deflection alike) is scaled by tilt_pct percent.
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
#define PADMAP_DEFAULT_DEADZONE 12
#define PADMAP_DEFAULT_PEAK 100
#define PADMAP_DEFAULT_TILT 50                       /* Soft Tilt: percent of the stick's strength while the button is held */

typedef struct {
    padsrc src[N64_NUM];
    padsrc cface;                     /* the C-Face shift button; PAD_BIND_NONE = off */
    padsrc cface_src[4];              /* while it is held: the control that acts as C-Up, C-Down, C-Left, C-Right */
    padsrc softtilt;                  /* the Soft Tilt button; PAD_BIND_NONE = off */
    int    dpad_swap;                 /* the stick channels and the D-pad channels are exchanged */
    int    tilt_pct;                  /* 0-100: not stored per pad; remap.c sets it from [input] soft_tilt_scale */
    int    deadzone;                  /* % of a half axis (0-50) */
    int    peak;                      /* % of a half axis that counts as full deflection (50-100) */
    char   name[64];                  /* the pad this mapping was made for */
} padmap;

const char *n64_ctl_name(n64_ctl c);     /* "A", "C-Up", "DPad Up", "Stick Left" */
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
    int      cshift;                  /* the C-Face shift is held */
    int      tilt;                    /* Soft Tilt is held (sx, sy are already scaled) */
} n64_state;

#define PADMAP_BUTTON_THRESHOLD 16384          /* a half axis counts as a pressed button beyond 50 % */

/* 0..32767: how far this source is pressed (digital sources give 0 or 32767) */
int  padsrc_activation(const padsrc *s, const padmap_io *io);
int  padsrc_same(const padsrc *a, const padsrc *b);      /* same physical control (axes: same half) */
void padmap_eval(const padmap *m, const padmap_io *io, n64_state *out);
int  padmap_shape(const padmap *m, int activation);      /* deadzone + range applied to one half axis, 0..32767 */

/* The N64 channel whose source the row for `c` stands for: c itself, or with the D-pad swap on, its D-pad / stick partner.
 * The menu lists and edits the mapping through this, so it always shows what the game gets. */
n64_ctl padmap_chan(const padmap *m, n64_ctl c);

/* ---- construction ---- */
void padmap_clear(padmap *m);                            /* nothing bound; default deadzone / range / tilt */

/* Rebuilds the bindings of `m` for the pad described by `lay` (deadzone and range are kept): left stick = stick, D-pad = D-pad,
 * right stick = C; a pad with one stick gets a C-Face shift button instead, a pad with none has its D-pad as the stick.  msg may be
 * filled with a remark ("no spare button for the C-Face shift: ..."). */
void padmap_default(padmap *m, const pad_layout *lay, char *msg, size_t msgsize);
/* Sets (or clears, with PAD_BIND_NONE) the C-Face shift button; the face buttons that become the C buttons come from `lay`
 * (when the shift had none yet). */
void padmap_set_cface(padmap *m, const pad_layout *lay, const padsrc *button);
/* Does the pad's own D-pad drive the stick channels (1), or the D-pad channels (0)?  `lay` NULL: the swap flag alone decides. */
int  padmap_dpad_is_stick(const padmap *m, const pad_layout *lay);

/* ---- persistence: one ini section per pad model, "pad:<pad name>" (suxty.cfg now, a per-game file later) ---- */
void padmap_section(const char *pad_name, char *out, size_t size);      /* "pad:retrogame_joypad" */
/* Reads the section.  Keys absent from both inis keep the value already in `m`.  `overlay` (may be NULL) wins over
 * `ini`.  Returns 1 if a stored mapping was found, 0 if there is none. */
int  padmap_load(padmap *m, const ini_t *ini, const ini_t *overlay, const char *section);
void padmap_store(const padmap *m, ini_t *ini, const char *section);
/* Old pad sections (the modifier layer and presets) in `ini`: a modifier whose layer mapped the C buttons becomes the C-Face
 * shift (keys cface, cface_c_*), anything else of the layer, and the preset name, is dropped.  Returns the keys changed. */
int  padmap_migrate_ini(ini_t *ini);

int  padsrc_parse(const char *text, padsrc *out);        /* "button:3", "hat:0:up", "axis:2-", "none" -> 0 ok */
void padsrc_format(const padsrc *s, char *out, size_t size);

#endif /* PADMAP_H */
