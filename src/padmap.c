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
    "A", "B", "Z", "Start", "L", "R", "DPad Up", "DPad Down", "DPad Left", "DPad Right",
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

/* the stick channel that swaps with D-pad channel n (N64_DUP .. N64_DRIGHT), and back */
static const n64_ctl l_swap_with[N64_NUM] = {
    [N64_DUP] = N64_SUP, [N64_DDOWN] = N64_SDOWN, [N64_DLEFT] = N64_SLEFT, [N64_DRIGHT] = N64_SRIGHT,
    [N64_SUP] = N64_DUP, [N64_SDOWN] = N64_DDOWN, [N64_SLEFT] = N64_DLEFT, [N64_SRIGHT] = N64_DRIGHT
};

n64_ctl padmap_chan(const padmap *m, n64_ctl c)
{
    if (m->dpad_swap && ((c >= N64_DUP && c <= N64_DRIGHT) || (c >= N64_SLEFT && c <= N64_SDOWN)))
        return l_swap_with[c];
    return c;
}

/* a normal source `s` is silenced while the C-Face shift is held if one of the shift's own sources or the shift button uses it */
static int shadowed(const padmap *m, const padsrc *s)
{
    int k;
    if (s->type == PAD_BIND_NONE)
        return 0;
    if (padsrc_same(s, &m->cface))
        return 1;
    for (k = 0; k < 4; k++)
        if (padsrc_same(s, &m->cface_src[k]))
            return 1;
    return 0;
}

static int held(const padsrc *s, const padmap_io *io)
{
    return s->type != PAD_BIND_NONE && padsrc_activation(s, io) >= PADMAP_BUTTON_THRESHOLD;
}

void padmap_eval(const padmap *m, const padmap_io *io, n64_state *out)
{
    int act[N64_NUM], n, shift, tilt, pct;

    memset(out, 0, sizeof(*out));
    shift = held(&m->cface, io);
    tilt = held(&m->softtilt, io);
    out->cshift = shift;
    out->tilt = tilt;
    for (n = 0; n < N64_NUM; n++)
    {
        const padsrc *s = &m->src[n];
        if (shift && n >= N64_CUP && n <= N64_CRIGHT && m->cface_src[n - N64_CUP].type != PAD_BIND_NONE)
            s = &m->cface_src[n - N64_CUP];                   /* the face button acts as this C button */
        else if ((shift && shadowed(m, s)) || padsrc_same(s, &m->cface) || padsrc_same(s, &m->softtilt))
            s = NULL;                                         /* what the face buttons do otherwise; a hold button is never also an input */
        act[n] = (s != NULL && s->type != PAD_BIND_NONE) ? padsrc_activation(s, io) : 0;
    }
    if (m->dpad_swap)
        for (n = N64_DUP; n <= N64_DRIGHT; n++)
        {
            int t = act[n];
            act[n] = act[l_swap_with[n]];
            act[l_swap_with[n]] = t;
        }
    for (n = 0; n < N64_NBUTTONS; n++)
        if (act[n] >= PADMAP_BUTTON_THRESHOLD)
            out->buttons |= 1u << n;
    out->sx = padmap_shape(m, act[N64_SRIGHT]) - padmap_shape(m, act[N64_SLEFT]);
    out->sy = padmap_shape(m, act[N64_SDOWN]) - padmap_shape(m, act[N64_SUP]);
    if (tilt)
    {
        pct = m->tilt_pct < 0 ? 0 : m->tilt_pct > 100 ? 100 : m->tilt_pct;
        out->sx = (int) ((int64_t) out->sx * pct / 100);
        out->sy = (int) ((int64_t) out->sy * pct / 100);
    }
}

/* ---- construction ------------------------------------------------------------------------------ */

void padmap_clear(padmap *m)
{
    memset(m, 0, sizeof(*m));
    m->deadzone = PADMAP_DEFAULT_DEADZONE;
    m->peak = PADMAP_DEFAULT_PEAK;
    m->tilt_pct = PADMAP_DEFAULT_TILT;
}

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

static void set_stick(padmap *m, const pad_bind *ax, const pad_bind *ay)
{
    m->src[N64_SLEFT]  = src_halfaxis(ax, -1);
    m->src[N64_SRIGHT] = src_halfaxis(ax, 1);
    m->src[N64_SUP]    = src_halfaxis(ay, -1);
    m->src[N64_SDOWN]  = src_halfaxis(ay, 1);
}

static void set_c_from_stick(padmap *m, const pad_bind *ax, const pad_bind *ay)
{
    m->src[N64_CLEFT]  = src_halfaxis(ax, -1);
    m->src[N64_CRIGHT] = src_halfaxis(ax, 1);
    m->src[N64_CUP]    = src_halfaxis(ay, -1);
    m->src[N64_CDOWN]  = src_halfaxis(ay, 1);
}

static void set_dpad(padmap *m, const pad_layout *L)
{
    m->src[N64_DUP]    = src_of_logical(L, PAD_UP);
    m->src[N64_DDOWN]  = src_of_logical(L, PAD_DOWN);
    m->src[N64_DLEFT]  = src_of_logical(L, PAD_LEFT);
    m->src[N64_DRIGHT] = src_of_logical(L, PAD_RIGHT);
}

static void set_dpad_as_stick(padmap *m, const pad_layout *L)
{
    m->src[N64_SUP]    = src_of_logical(L, PAD_UP);
    m->src[N64_SDOWN]  = src_of_logical(L, PAD_DOWN);
    m->src[N64_SLEFT]  = src_of_logical(L, PAD_LEFT);
    m->src[N64_SRIGHT] = src_of_logical(L, PAD_RIGHT);
}

static int bind_ok(const pad_layout *L, pad_button b) { return L->b[b].type != PAD_BIND_NONE; }

/* A, B, Start, L, R and Z */
static void set_base_buttons(padmap *m, const pad_layout *L)
{
    m->src[N64_A]     = src_of_logical(L, PAD_A);
    m->src[N64_B]     = src_of_logical(L, PAD_X);        /* the button to the left of A, like the N64 pad */
    m->src[N64_START] = src_of_logical(L, PAD_START);
    m->src[N64_L]     = src_of_logical(L, PAD_L);
    m->src[N64_R]     = src_of_logical(L, PAD_R);
    if (bind_ok(L, PAD_L2))      m->src[N64_Z] = src_of_logical(L, PAD_L2);
    else if (bind_ok(L, PAD_R2)) m->src[N64_Z] = src_of_logical(L, PAD_R2);
    else if (bind_ok(L, PAD_L3)) m->src[N64_Z] = src_of_logical(L, PAD_L3);
}

/* a control that is free to be the C-Face shift: a trigger or stick click that is not Z */
static padsrc pick_cface(const padmap *m, const pad_layout *L)
{
    static const pad_button cand[] = { PAD_R2, PAD_L2, PAD_R3, PAD_L3 };
    size_t i;
    for (i = 0; i < sizeof(cand) / sizeof(cand[0]); i++)
    {
        padsrc s;
        if (!bind_ok(L, cand[i]))
            continue;
        s = src_of_logical(L, cand[i]);
        if (!padsrc_same(&s, &m->src[N64_Z]))
            return s;
    }
    return src_none();
}

/* the face buttons A/B/X/Y become C-Down/C-Right/C-Left/C-Up (their positions on the pad) */
static void set_face_c(padmap *m, const pad_layout *L)
{
    m->cface_src[N64_CDOWN - N64_CUP]  = src_of_logical(L, PAD_A);
    m->cface_src[N64_CRIGHT - N64_CUP] = src_of_logical(L, PAD_B);
    m->cface_src[N64_CLEFT - N64_CUP]  = src_of_logical(L, PAD_X);
    m->cface_src[N64_CUP - N64_CUP]    = src_of_logical(L, PAD_Y);
}

static void note(char *msg, size_t size, const char *text)
{
    if (msg != NULL && size > 0)
        snprintf(msg, size, "%s", text);
}

void padmap_set_cface(padmap *m, const pad_layout *L, const padsrc *button)
{
    int k, any = 0;

    if (button == NULL || button->type == PAD_BIND_NONE)
    {
        m->cface = src_none();
        for (k = 0; k < 4; k++)
            m->cface_src[k] = src_none();
        return;
    }
    m->cface = *button;
    for (k = 0; k < 4; k++)
        any |= m->cface_src[k].type != PAD_BIND_NONE;
    if (!any && L != NULL)
        set_face_c(m, L);
}

void padmap_default(padmap *m, const pad_layout *L, char *msg, size_t msgsize)
{
    padmap n;

    note(msg, msgsize, "");
    padmap_clear(&n);
    n.deadzone = m->deadzone;
    n.peak = m->peak;
    n.tilt_pct = m->tilt_pct;
    snprintf(n.name, sizeof(n.name), "%s", L->name);
    set_base_buttons(&n, L);
    if (L->has_ls)
    {
        set_stick(&n, &L->lx, &L->ly);
        set_dpad(&n, L);
    }
    else
        set_dpad_as_stick(&n, L);                  /* a pad without a stick: its D-pad is the stick */
    if (L->has_rs)
        set_c_from_stick(&n, &L->rx, &L->ry);
    else
    {
        /* no right stick: the C buttons are the face buttons while a spare button is held */
        padsrc s = pick_cface(&n, L);
        if (s.type != PAD_BIND_NONE)
            padmap_set_cface(&n, L, &s);
        else
        {
            /* nothing to hold: put what fits on the face buttons and say so */
            n.src[N64_CUP]    = src_of_logical(L, PAD_Y);
            n.src[N64_CRIGHT] = src_of_logical(L, PAD_B);
            note(msg, msgsize, "No spare button for the C-Face Shift: only C-Up and C-Right are mapped");
        }
    }
    *m = n;
}

int padmap_dpad_is_stick(const padmap *m, const pad_layout *L)
{
    static const struct { n64_ctl stick; pad_button dir; } pairs[4] = {
        { N64_SUP, PAD_UP }, { N64_SDOWN, PAD_DOWN }, { N64_SLEFT, PAD_LEFT }, { N64_SRIGHT, PAD_RIGHT }
    };
    int i;

    if (L == NULL)
        return m->dpad_swap;
    for (i = 0; i < 4; i++)
    {
        padsrc phys = src_of_logical(L, pairs[i].dir);
        const padsrc *eff = &m->src[padmap_chan(m, pairs[i].stick)];
        if (eff->type != PAD_BIND_NONE && padsrc_same(eff, &phys))
            return 1;
    }
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

/* ini keys of the C-Face shift's own sources, in cface_src order */
static const char *const l_cface_keys[4] = { "cface_c_up", "cface_c_down", "cface_c_left", "cface_c_right" };

static void load_src(padsrc *dst, const ini_t *ini, const ini_t *overlay, const char *sec, const char *key)
{
    const char *v = lookup(ini, overlay, sec, key);
    if (v != NULL && padsrc_parse(v, dst) != 0)
        *dst = src_none();
}

int padmap_load(padmap *m, const ini_t *ini, const ini_t *overlay, const char *sec)
{
    const char *v;
    int n, found;

    found = lookup(ini, overlay, sec, "deadzone") != NULL || lookup(ini, overlay, sec, "a") != NULL;
    if ((v = lookup(ini, overlay, sec, "deadzone")) != NULL)
        m->deadzone = atoi(v) < 0 ? 0 : atoi(v) > 50 ? 50 : atoi(v);
    if ((v = lookup(ini, overlay, sec, "peak")) != NULL)
        m->peak = atoi(v) < 50 ? 50 : atoi(v) > 100 ? 100 : atoi(v);
    if ((v = lookup(ini, overlay, sec, "dpad_swap")) != NULL)
        m->dpad_swap = strcasecmp(v, "true") == 0 || strcmp(v, "1") == 0;
    load_src(&m->cface, ini, overlay, sec, "cface");
    load_src(&m->softtilt, ini, overlay, sec, "softtilt");
    for (n = 0; n < 4; n++)
        load_src(&m->cface_src[n], ini, overlay, sec, l_cface_keys[n]);
    for (n = 0; n < N64_NUM; n++)
        load_src(&m->src[n], ini, overlay, sec, l_ctl_keys[n]);
    return found;
}

void padmap_store(const padmap *m, ini_t *ini, const char *sec)
{
    char val[40];
    int n;

    ini_set_int(ini, sec, "deadzone", m->deadzone);
    ini_set_int(ini, sec, "peak", m->peak);
    ini_set_bool(ini, sec, "dpad_swap", m->dpad_swap);
    padsrc_format(&m->cface, val, sizeof(val));
    ini_set(ini, sec, "cface", val);
    for (n = 0; n < 4; n++)
    {
        padsrc_format(&m->cface_src[n], val, sizeof(val));
        ini_set(ini, sec, l_cface_keys[n], val);
    }
    padsrc_format(&m->softtilt, val, sizeof(val));
    ini_set(ini, sec, "softtilt", val);
    for (n = 0; n < N64_NUM; n++)
    {
        padsrc_format(&m->src[n], val, sizeof(val));
        ini_set(ini, sec, l_ctl_keys[n], val);
    }
}

/* ---- old files ----------------------------------------------------------------------------------- */

static int is_none(const char *v) { return v == NULL || v[0] == 0 || strcasecmp(v, "none") == 0; }

/* One pad section of the front-end / early plugin builds: `modifier` plus a layer of `mod_<key>` sources, and a `preset` name. */
static int migrate_section(ini_t *ini, const char *sec)
{
    static const char *const cs[4] = { "c_up", "c_down", "c_left", "c_right" };
    char key[48], old[48];
    const char *v;
    int k, n = 0, has_c = 0, i;
    const char *s, *kk;

    for (k = 0; k < 4; k++)
    {
        snprintf(key, sizeof(key), "mod_%s", cs[k]);
        has_c |= ini_has(ini, sec, key) && !is_none(ini_get(ini, sec, key, ""));
    }
    v = ini_get(ini, sec, "modifier", "");
    if (!is_none(v) && has_c && !ini_has(ini, sec, "cface"))
    {
        /* "Face buttons = C": the modifier is the C-Face shift, its C sources are the face buttons that become C */
        snprintf(old, sizeof(old), "%s", v);
        ini_set(ini, sec, "cface", old);
        for (k = 0; k < 4; k++)
        {
            snprintf(key, sizeof(key), "mod_%s", cs[k]);
            if (ini_has(ini, sec, key))
                ini_set(ini, sec, l_cface_keys[k], ini_get(ini, sec, key, "none"));
        }
        n++;
    }
    /* what is left of the layer, and the preset name, has no meaning any more */
    for (;;)
    {
        int found = 0;
        for (i = 0; ini_entry(ini, i, &s, &kk, NULL); i++)
            if (strcmp(s, sec) == 0 && (strncmp(kk, "mod_", 4) == 0 || strcmp(kk, "modifier") == 0 || strcmp(kk, "preset") == 0))
            {
                char sc[128], kc[48];
                snprintf(sc, sizeof(sc), "%s", s);
                snprintf(kc, sizeof(kc), "%s", kk);
                ini_remove(ini, sc, kc);
                n++;
                found = 1;
                break;
            }
        if (!found)
            break;
    }
    return n;
}

int padmap_migrate_ini(ini_t *ini)
{
    char names[16][128];
    int nn = 0, i, n = 0;
    const char *s, *kk;

    if (ini == NULL)
        return 0;
    for (i = 0; ini_entry(ini, i, &s, &kk, NULL); i++)
    {
        int j, known = 0;
        if (strncmp(s, "pad:", 4) != 0)
            continue;
        for (j = 0; j < nn; j++)
            known |= strcmp(names[j], s) == 0;
        if (!known && nn < 16)
            snprintf(names[nn++], sizeof(names[0]), "%s", s);
    }
    for (i = 0; i < nn; i++)
        n += migrate_section(ini, names[i]);
    return n;
}
