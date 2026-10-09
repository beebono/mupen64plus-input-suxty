/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - padmap.c
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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <SDL.h>

#include "padmap.h"

static const char *const l_ctl_names[N64_NUM] = {
    "A", "B", "Z", "Start", "L", "R", "D-Up", "D-Down", "D-Left", "D-Right",
    "C-Up", "C-Down", "C-Left", "C-Right", "Stick Left", "Stick Right", "Stick Up", "Stick Down"
};
static const char *const l_ctl_keys[N64_NUM] = {
    "a", "b", "z", "start", "l", "r", "dpad_up", "dpad_down", "dpad_left", "dpad_right",
    "c_up", "c_down", "c_left", "c_right", "stick_left", "stick_right", "stick_up", "stick_down"
};

const char *n64_ctl_name(n64_ctl c) { return c >= 0 && c < N64_NUM ? l_ctl_names[c] : "?"; }
const char *n64_ctl_key(n64_ctl c)  { return c >= 0 && c < N64_NUM ? l_ctl_keys[c] : "?"; }

/* ---- evaluation ------------------------------------------------------------------------------ */

int padsrc_activation(const padsrc *s, const padmap_io *io)
{
    switch (s->type)
    {
        case PAD_BIND_BUTTON:
            return io->button(io->ctx, s->index) ? 32767 : 0;
        case PAD_BIND_HAT:
            return (io->hat(io->ctx, s->index) & s->hat_mask) ? 32767 : 0;
        case PAD_BIND_AXIS:
        {
            int v = io->axis(io->ctx, s->index);
            int rest = io->axis_rest(io->ctx, s->index);
            int d, span;
            if (s->dir >= 0)
            {
                d = v - rest;
                span = 32767 - rest;
            }
            else
            {
                d = rest - v;
                span = rest + 32767;             /* symmetric with the positive side: a stick at -32768 is just full */
            }
            if (d <= 0 || span < 1)
                return 0;
            if (d >= span)
                return 32767;
            return (int) ((int64_t) d * 32767 / span);
        }
        default:
            return 0;
    }
}

int padsrc_same(const padsrc *a, const padsrc *b)
{
    if (a->type == PAD_BIND_NONE || a->type != b->type || a->index != b->index)
        return 0;
    if (a->type == PAD_BIND_HAT)
        return (a->hat_mask & b->hat_mask) != 0;
    if (a->type == PAD_BIND_AXIS)
        return (a->dir >= 0) == (b->dir >= 0);
    return 1;
}

int padmap_shape(const padmap *m, int act)
{
    int dz = m->deadzone * 32767 / 100;
    int pk = m->peak * 32767 / 100;
    if (pk <= dz + 1)
        pk = dz + 2;
    if (act <= dz)
        return 0;
    if (act >= pk)
        return 32767;
    return (int) ((int64_t) (act - dz) * 32767 / (pk - dz));
}

/* layer 0 control `s` is silenced while the modifier is held if layer 1 or the modifier itself use it */
static int shadowed(const padmap *m, const padsrc *s)
{
    int k;
    if (s->type == PAD_BIND_NONE)
        return 0;
    if (padsrc_same(s, &m->modifier))
        return 1;
    for (k = 0; k < N64_NUM; k++)
        if (padsrc_same(s, &m->src[1][k]))
            return 1;
    return 0;
}

void padmap_eval(const padmap *m, const padmap_io *io, n64_state *out)
{
    int act[N64_NUM], n, mod;

    memset(out, 0, sizeof(*out));
    mod = m->modifier.type != PAD_BIND_NONE && padsrc_activation(&m->modifier, io) >= PADMAP_BUTTON_THRESHOLD;
    out->mod = mod;
    for (n = 0; n < N64_NUM; n++)
    {
        const padsrc *s = &m->src[0][n];
        if (mod)
        {
            if (m->src[1][n].type != PAD_BIND_NONE)
                s = &m->src[1][n];
            else if (shadowed(m, s))
                s = NULL;
        }
        act[n] = (s != NULL && s->type != PAD_BIND_NONE) ? padsrc_activation(s, io) : 0;
    }
    for (n = 0; n < N64_NBUTTONS; n++)
        if (act[n] >= PADMAP_BUTTON_THRESHOLD)
            out->buttons |= 1u << n;
    out->sx = padmap_shape(m, act[N64_SRIGHT]) - padmap_shape(m, act[N64_SLEFT]);
    out->sy = padmap_shape(m, act[N64_SDOWN]) - padmap_shape(m, act[N64_SUP]);
}

/* ---- construction and presets ------------------------------------------------------------------ */

void padmap_clear(padmap *m)
{
    memset(m, 0, sizeof(*m));
    m->deadzone = PADMAP_DEFAULT_DEADZONE;
    m->peak = PADMAP_DEFAULT_PEAK;
}

static const struct { const char *name, *desc; } l_presets[PRESET_NUM] = {
    { "Default",               "Picks the best layout for this pad" },
    { "Right stick = C",       "Left stick moves, right stick is C" },
    { "Face buttons = C",      "Hold a modifier: A/B/X/Y become C" },
    { "D-pad = stick",         "D-pad is the analog stick" },
    { "Sticks swapped",        "Right stick moves, left stick is C" },
};

const char *padmap_preset_name(int p) { return p >= 0 && p < PRESET_NUM ? l_presets[p].name : "?"; }
const char *padmap_preset_desc(int p) { return p >= 0 && p < PRESET_NUM ? l_presets[p].desc : ""; }

static padsrc src_none(void) { padsrc s; memset(&s, 0, sizeof(s)); return s; }

static padsrc src_of_bind(const pad_bind *b, int dir)
{
    padsrc s = src_none();
    switch (b->type)
    {
        case PAD_BIND_BUTTON: s.type = PAD_BIND_BUTTON; s.index = (int16_t) b->index; break;
        case PAD_BIND_HAT:    s.type = PAD_BIND_HAT; s.index = (int16_t) b->index; s.hat_mask = (uint8_t) b->hat_mask; break;
        case PAD_BIND_AXIS:   s.type = PAD_BIND_AXIS; s.index = (int16_t) b->index; s.dir = (int8_t) (dir < 0 ? -1 : 1); break;
        default: break;
    }
    return s;
}

/* logical button -> source.  Directions that happen to be on an axis (rare) get the sign of their direction. */
static padsrc src_of_logical(const pad_layout *L, pad_button b)
{
    int dir = (b == PAD_UP || b == PAD_LEFT) ? -1 : 1;
    return src_of_bind(&L->b[b], dir);
}

static padsrc src_halfaxis(const pad_bind *ax, int dir)
{
    if (ax->type != PAD_BIND_AXIS)
        return src_none();
    return src_of_bind(ax, dir);
}

static void set_stick(padmap *m, int layer, const pad_bind *ax, const pad_bind *ay)
{
    m->src[layer][N64_SLEFT]  = src_halfaxis(ax, -1);
    m->src[layer][N64_SRIGHT] = src_halfaxis(ax, 1);
    m->src[layer][N64_SUP]    = src_halfaxis(ay, -1);
    m->src[layer][N64_SDOWN]  = src_halfaxis(ay, 1);
}

static void set_c_from_stick(padmap *m, int layer, const pad_bind *ax, const pad_bind *ay)
{
    m->src[layer][N64_CLEFT]  = src_halfaxis(ax, -1);
    m->src[layer][N64_CRIGHT] = src_halfaxis(ax, 1);
    m->src[layer][N64_CUP]    = src_halfaxis(ay, -1);
    m->src[layer][N64_CDOWN]  = src_halfaxis(ay, 1);
}

static void set_dpad(padmap *m, int layer, const pad_layout *L)
{
    m->src[layer][N64_DUP]    = src_of_logical(L, PAD_UP);
    m->src[layer][N64_DDOWN]  = src_of_logical(L, PAD_DOWN);
    m->src[layer][N64_DLEFT]  = src_of_logical(L, PAD_LEFT);
    m->src[layer][N64_DRIGHT] = src_of_logical(L, PAD_RIGHT);
}

static void set_dpad_as_stick(padmap *m, int layer, const pad_layout *L)
{
    m->src[layer][N64_SUP]    = src_of_logical(L, PAD_UP);
    m->src[layer][N64_SDOWN]  = src_of_logical(L, PAD_DOWN);
    m->src[layer][N64_SLEFT]  = src_of_logical(L, PAD_LEFT);
    m->src[layer][N64_SRIGHT] = src_of_logical(L, PAD_RIGHT);
}

static void set_stick_as_dpad(padmap *m, int layer, const pad_bind *ax, const pad_bind *ay)
{
    m->src[layer][N64_DLEFT]  = src_halfaxis(ax, -1);
    m->src[layer][N64_DRIGHT] = src_halfaxis(ax, 1);
    m->src[layer][N64_DUP]    = src_halfaxis(ay, -1);
    m->src[layer][N64_DDOWN]  = src_halfaxis(ay, 1);
}

static int bind_ok(const pad_layout *L, pad_button b) { return L->b[b].type != PAD_BIND_NONE; }

/* A, B, Start, L, R and Z, the same for every preset */
static void set_base_buttons(padmap *m, const pad_layout *L)
{
    m->src[0][N64_A]     = src_of_logical(L, PAD_A);
    m->src[0][N64_B]     = src_of_logical(L, PAD_X);        /* the button to the left of A, like the N64 pad */
    m->src[0][N64_START] = src_of_logical(L, PAD_START);
    m->src[0][N64_L]     = src_of_logical(L, PAD_L);
    m->src[0][N64_R]     = src_of_logical(L, PAD_R);
    if (bind_ok(L, PAD_L2))      m->src[0][N64_Z] = src_of_logical(L, PAD_L2);
    else if (bind_ok(L, PAD_R2)) m->src[0][N64_Z] = src_of_logical(L, PAD_R2);
    else if (bind_ok(L, PAD_L3)) m->src[0][N64_Z] = src_of_logical(L, PAD_L3);
}

/* a control that is free to be the modifier: a trigger or stick click that is not Z */
static padsrc pick_modifier(const padmap *m, const pad_layout *L)
{
    static const pad_button cand[] = { PAD_R2, PAD_L2, PAD_R3, PAD_L3 };
    size_t i;
    for (i = 0; i < sizeof(cand) / sizeof(cand[0]); i++)
    {
        padsrc s;
        if (!bind_ok(L, cand[i]))
            continue;
        s = src_of_logical(L, cand[i]);
        if (!padsrc_same(&s, &m->src[0][N64_Z]))
            return s;
    }
    return src_none();
}

/* layer 1: A/B/X/Y -> C-Down/C-Right/C-Left/C-Up (their positions on the pad) */
static void set_face_c_layer(padmap *m, const pad_layout *L)
{
    m->src[1][N64_CDOWN]  = src_of_logical(L, PAD_A);
    m->src[1][N64_CRIGHT] = src_of_logical(L, PAD_B);
    m->src[1][N64_CLEFT]  = src_of_logical(L, PAD_X);
    m->src[1][N64_CUP]    = src_of_logical(L, PAD_Y);
}

static void note(char *msg, size_t size, const char *text)
{
    if (msg != NULL && size > 0)
        snprintf(msg, size, "%s", text);
}

static int build_preset(padmap *m, int preset, const pad_layout *L, char *msg, size_t size)
{
    if (preset == PRESET_DEFAULT)
        preset = (L->has_ls && L->has_rs) ? PRESET_RSTICK_C : L->has_ls ? PRESET_FACE_C : PRESET_DPAD_STICK;

    set_base_buttons(m, L);
    switch (preset)
    {
        case PRESET_RSTICK_C:
            if (!L->has_ls || !L->has_rs)
                return -1;
            set_stick(m, 0, &L->lx, &L->ly);
            set_dpad(m, 0, L);
            set_c_from_stick(m, 0, &L->rx, &L->ry);
            break;
        case PRESET_SWAP_STICKS:
            if (!L->has_ls || !L->has_rs)
                return -1;
            set_stick(m, 0, &L->rx, &L->ry);
            set_dpad(m, 0, L);
            set_c_from_stick(m, 0, &L->lx, &L->ly);
            break;
        case PRESET_FACE_C:
        case PRESET_DPAD_STICK:
        {
            int dpad_is_stick = (preset == PRESET_DPAD_STICK) || !L->has_ls;
            if (dpad_is_stick)
                set_dpad_as_stick(m, 0, L);
            else
            {
                set_stick(m, 0, &L->lx, &L->ly);
                set_dpad(m, 0, L);
            }
            if (preset == PRESET_DPAD_STICK && L->has_ls)
                set_stick_as_dpad(m, 0, &L->lx, &L->ly);
            if (preset == PRESET_DPAD_STICK && L->has_rs)
            {
                set_c_from_stick(m, 0, &L->rx, &L->ry);
                break;
            }
            m->modifier = pick_modifier(m, L);
            if (m->modifier.type == PAD_BIND_NONE)
            {
                /* nothing to hold: put what fits on the face buttons and say so */
                m->src[0][N64_CUP]    = src_of_logical(L, PAD_Y);
                m->src[0][N64_CRIGHT] = src_of_logical(L, PAD_B);
                note(msg, size, "No spare button for a modifier: only C-Up and C-Right are mapped");
                break;
            }
            set_face_c_layer(m, L);
            if (dpad_is_stick && !L->has_ls)
                set_dpad(m, 1, L);              /* the D-pad is the stick; with the modifier held it is the N64 D-pad */
            break;
        }
        default:
            return -1;
    }
    return 0;
}

int padmap_apply_preset(padmap *m, int preset, const pad_layout *L, char *msg, size_t msgsize)
{
    padmap n;
    int rc;

    note(msg, msgsize, "");
    if (preset < 0 || preset >= PRESET_NUM)
        return -1;
    padmap_clear(&n);
    n.deadzone = m->deadzone;
    n.peak = m->peak;
    rc = build_preset(&n, preset, L, msg, msgsize);
    if (rc != 0)
    {
        note(msg, msgsize, "This pad has no right stick");
        return -1;
    }
    snprintf(n.name, sizeof(n.name), "%s", L->name);
    snprintf(n.preset, sizeof(n.preset), "%s", preset == PRESET_DEFAULT ? "default" : padmap_preset_name(preset));
    *m = n;
    return 0;
}

/* ---- persistence ------------------------------------------------------------------------------- */

static const char *hat_text(int mask)
{
    return mask == SDL_HAT_UP ? "up" : mask == SDL_HAT_DOWN ? "down" : mask == SDL_HAT_LEFT ? "left" :
           mask == SDL_HAT_RIGHT ? "right" : "up";
}

void padsrc_format(const padsrc *s, char *out, size_t size)
{
    switch (s->type)
    {
        case PAD_BIND_BUTTON: snprintf(out, size, "button:%d", s->index); break;
        case PAD_BIND_HAT:    snprintf(out, size, "hat:%d:%s", s->index, hat_text(s->hat_mask)); break;
        case PAD_BIND_AXIS:   snprintf(out, size, "axis:%d%c", s->index, s->dir < 0 ? '-' : '+'); break;
        default:              snprintf(out, size, "none"); break;
    }
}

int padsrc_parse(const char *text, padsrc *out)
{
    int a, n = 0;
    char c, dir[16];

    *out = src_none();
    if (text == NULL || text[0] == 0 || strcasecmp(text, "none") == 0)
        return 0;
    if (sscanf(text, "button:%d%n", &a, &n) == 1 && a >= 0 && a < 4096)
    {
        out->type = PAD_BIND_BUTTON; out->index = (int16_t) a;
        return 0;
    }
    if (sscanf(text, "hat:%d:%15s", &a, dir) == 2 && a >= 0 && a < 64)
    {
        int mask = strcasecmp(dir, "up") == 0 ? SDL_HAT_UP : strcasecmp(dir, "down") == 0 ? SDL_HAT_DOWN :
                   strcasecmp(dir, "left") == 0 ? SDL_HAT_LEFT : strcasecmp(dir, "right") == 0 ? SDL_HAT_RIGHT : 0;
        if (mask != 0)
        {
            out->type = PAD_BIND_HAT; out->index = (int16_t) a; out->hat_mask = (uint8_t) mask;
            return 0;
        }
    }
    if (sscanf(text, "axis:%d%c", &a, &c) == 2 && a >= 0 && a < 64 && (c == '+' || c == '-'))
    {
        out->type = PAD_BIND_AXIS; out->index = (int16_t) a; out->dir = (int8_t) (c == '-' ? -1 : 1);
        return 0;
    }
    return -1;
}

static const char *lookup(const ini_t *ini, const ini_t *overlay, const char *sec, const char *key)
{
    if (overlay != NULL && ini_has(overlay, sec, key))
        return ini_get(overlay, sec, key, "");
    if (ini != NULL && ini_has(ini, sec, key))
        return ini_get(ini, sec, key, "");
    return NULL;
}

void padmap_section(const char *pad_name, char *out, size_t size)
{
    size_t n = (size_t) snprintf(out, size, "pad:"), i;
    for (i = 0; pad_name != NULL && pad_name[i] && n + 1 < size; i++)
    {
        unsigned char ch = (unsigned char) pad_name[i];
        out[n++] = (ch < 32 || ch == '[' || ch == ']' || ch == '#' || ch == ';' || ch == '=') ? '_' : (char) ch;
    }
    out[n] = 0;
}

int padmap_load(padmap *m, const ini_t *ini, const ini_t *overlay, const char *sec)
{
    char key[40];
    const char *v;
    int layer, n, found;

    found = lookup(ini, overlay, sec, "deadzone") != NULL || lookup(ini, overlay, sec, "a") != NULL;
    if ((v = lookup(ini, overlay, sec, "preset")) != NULL)
        snprintf(m->preset, sizeof(m->preset), "%s", v);
    if ((v = lookup(ini, overlay, sec, "deadzone")) != NULL)
        m->deadzone = atoi(v) < 0 ? 0 : atoi(v) > 50 ? 50 : atoi(v);
    if ((v = lookup(ini, overlay, sec, "peak")) != NULL)
        m->peak = atoi(v) < 50 ? 50 : atoi(v) > 100 ? 100 : atoi(v);
    if ((v = lookup(ini, overlay, sec, "modifier")) != NULL && padsrc_parse(v, &m->modifier) != 0)
        m->modifier = src_none();
    for (layer = 0; layer < PADMAP_LAYERS; layer++)
        for (n = 0; n < N64_NUM; n++)
        {
            snprintf(key, sizeof(key), "%s%s", layer ? "mod_" : "", l_ctl_keys[n]);
            if ((v = lookup(ini, overlay, sec, key)) != NULL && padsrc_parse(v, &m->src[layer][n]) != 0)
                m->src[layer][n] = src_none();
        }
    return found;
}

void padmap_store(const padmap *m, ini_t *ini, const char *sec)
{
    char key[40], val[40];
    int layer, n;

    ini_set(ini, sec, "preset", m->preset);
    ini_set_int(ini, sec, "deadzone", m->deadzone);
    ini_set_int(ini, sec, "peak", m->peak);
    padsrc_format(&m->modifier, val, sizeof(val));
    ini_set(ini, sec, "modifier", val);
    for (layer = 0; layer < PADMAP_LAYERS; layer++)
        for (n = 0; n < N64_NUM; n++)
        {
            snprintf(key, sizeof(key), "%s%s", layer ? "mod_" : "", l_ctl_keys[n]);
            padsrc_format(&m->src[layer][n], val, sizeof(val));
            ini_set(ini, sec, key, val);
        }
}
