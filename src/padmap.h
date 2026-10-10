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


/* pure logic (no SDL: the physical state comes in through padmap_io). Mappings belong to a pad model (its SDL name),
 * not a player slot. C-Face shift: while held, the face buttons act as C buttons and their normal action is silenced.
 * Soft Tilt: while held, the stick output (after deadzone and range) is scaled by tilt_pct. */

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
    N64_SLEFT, N64_SRIGHT, N64_SUP, N64_SDOWN,
    N64_NUM
} n64_ctl;

#define N64_NBUTTONS 14                              /* N64_A .. N64_CRIGHT: digital */
#define PADMAP_DEFAULT_DEADZONE 12
#define PADMAP_DEFAULT_PEAK 100
#define PADMAP_DEFAULT_TILT 50

typedef struct {
    padsrc src[N64_NUM];
    padsrc cface;                     /* PAD_BIND_NONE = off */
    padsrc cface_src[4];              /* while held: the control acting as C-Up, C-Down, C-Left, C-Right */
    padsrc softtilt;
    int    dpad_swap;
    int    tilt_pct;                  /* not stored per pad; remap.c sets it from [input] soft_tilt_scale */
    int    deadzone;                  /* % of a half axis (0-50) */
    int    peak;                      /* % of a half axis that counts as full deflection (50-100) */
    char   name[64];
} padmap;

const char *n64_ctl_name(n64_ctl c);
const char *n64_ctl_key(n64_ctl c);

typedef struct {
    int  (*button)(void *ctx, int index);
    int  (*hat)(void *ctx, int index);
    int  (*axis)(void *ctx, int index);             /* -32768..32767 */
    int  (*axis_rest)(void *ctx, int index);        /* the value at rest (0 for sticks, -32768 for most triggers) */
    void *ctx;
} padmap_io;

typedef struct {
    uint32_t buttons;
    int      sx, sy;                  /* -32767..32767; +x right, +y DOWN (SDL convention) */
    int      cshift;
    int      tilt;                    /* sx, sy are already scaled */
} n64_state;

#define PADMAP_BUTTON_THRESHOLD 16384          /* a half axis counts as a pressed button beyond 50 % */

/* 0..32767: how far this source is pressed (digital sources give 0 or 32767) */
int  padsrc_activation(const padsrc *s, const padmap_io *io);
int  padsrc_same(const padsrc *a, const padsrc *b);
void padmap_eval(const padmap *m, const padmap_io *io, n64_state *out);
int  padmap_shape(const padmap *m, int activation);      /* deadzone + range applied to one half axis, 0..32767 */

/* c itself, or with the D-pad swap on its D-pad / stick partner: the menu edits through this so it shows what the
 * game gets */
n64_ctl padmap_chan(const padmap *m, n64_ctl c);

void padmap_clear(padmap *m);

/* deadzone and range are kept; msg may get a remark */
void padmap_default(padmap *m, const pad_layout *lay, char *msg, size_t msgsize);
/* the face buttons that become the C buttons come from `lay` (when the shift had none yet) */
void padmap_set_cface(padmap *m, const pad_layout *lay, const padsrc *button);
/* `lay` NULL: the swap flag alone decides */
int  padmap_dpad_is_stick(const padmap *m, const pad_layout *lay);

void padmap_section(const char *pad_name, char *out, size_t size);
/* keys absent from both inis keep the value already in `m`; `overlay` (may be NULL) wins; returns 1 if a stored
 * mapping was found */
int  padmap_load(padmap *m, const ini_t *ini, const ini_t *overlay, const char *section);
void padmap_store(const padmap *m, ini_t *ini, const char *section);
int  padmap_migrate_ini(ini_t *ini);

int  padsrc_parse(const char *text, padsrc *out);
void padsrc_format(const padsrc *s, char *out, size_t size);

#endif /* PADMAP_H */
