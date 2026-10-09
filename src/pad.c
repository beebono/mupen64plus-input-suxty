/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-ui-suxty - pad.c
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

#include <SDL.h>

#include "m64p_types.h"
#include "main.h"
#include "osal_preproc.h"
#include "inputfilter.h"
#include "pad.h"

#define MAX_PADS PAD_MAX_PADS

typedef struct {
    SDL_GameController *gc;
    SDL_Joystick       *js;        /* only for pads without a controller mapping */
    SDL_Joystick       *joy;       /* the underlying joystick (either of the above), the SDL device the game input is read from */
    SDL_JoystickID      id;
    int                 devidx;    /* SDL device index at the time it was opened */
    pad_layout          lay;       /* lay.b[] are the logical-button bindings */
} pad_dev;

static pad_dev  l_pads[MAX_PADS];
static int      l_npads;
static int      l_numjoy_seen = -1;
static int      l_generation;          /* bumped whenever the pad list was rebuilt (hot-plug) */
static int      l_swap_ab;
static int      l_deadzone = 50;
static uint32_t l_inject;
static int      l_keyboard;
static int      l_inited;

static const char *const l_names[PAD_NUM] = {
    "up", "down", "left", "right", "a", "b", "x", "y", "l", "r", "start", "select", "l3", "r3", "guide", "l2", "r2"
};

const char *pad_button_name(pad_button b)
{
    return (b >= 0 && b < PAD_NUM) ? l_names[b] : "?";
}

uint32_t pad_parse_combo(const char *str)
{
    uint32_t mask = 0;
    char buf[64];
    char *tok, *save = NULL;

    if (str == NULL)
        return 0;
    snprintf(buf, sizeof(buf), "%s", str);
    for (tok = strtok_r(buf, "+, ", &save); tok != NULL; tok = strtok_r(NULL, "+, ", &save))
    {
        int i, found = 0;
        for (i = 0; i < PAD_NUM; i++)
        {
            if (strcasecmp(tok, l_names[i]) == 0)
            {
                mask |= PADBIT(i);
                found = 1;
                break;
            }
        }
        if (!found)
        {
            DebugMessage(M64MSG_WARNING, "unknown button name '%s' in combo '%s'", tok, str);
            return 0;
        }
    }
    return mask;
}

void pad_format_combo(uint32_t mask, char *out, size_t outsize)
{
    /* names in the order of pad_button; "start+select" etc. */
    int i;
    size_t n = 0;
    if (outsize == 0)
        return;
    out[0] = 0;
    for (i = 0; i < PAD_NUM; i++)
    {
        if (mask & PADBIT(i))
            n += (size_t) snprintf(out + n, n < outsize ? outsize - n : 0, "%s%s", n ? "+" : "", l_names[i]);
    }
}

static void close_all(void)
{
    int i;
    for (i = 0; i < l_npads; i++)
    {
        if (l_pads[i].gc) SDL_GameControllerClose(l_pads[i].gc);
        if (l_pads[i].js) SDL_JoystickClose(l_pads[i].js);
    }
    l_npads = 0;
}

static void log_guid(int index)
{
    char guid[64];
    SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(index), guid, sizeof(guid));
    DebugMessage(M64MSG_VERBOSE, "pad %d guid %s", index, guid);
}

static const struct { SDL_GameControllerButton sdl; pad_button logical; } l_gcmap[] = {
    { SDL_CONTROLLER_BUTTON_DPAD_UP, PAD_UP },       { SDL_CONTROLLER_BUTTON_DPAD_DOWN, PAD_DOWN },
    { SDL_CONTROLLER_BUTTON_DPAD_LEFT, PAD_LEFT },   { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, PAD_RIGHT },
    { SDL_CONTROLLER_BUTTON_A, PAD_A },              { SDL_CONTROLLER_BUTTON_B, PAD_B },
    { SDL_CONTROLLER_BUTTON_X, PAD_X },              { SDL_CONTROLLER_BUTTON_Y, PAD_Y },
    { SDL_CONTROLLER_BUTTON_LEFTSHOULDER, PAD_L },   { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, PAD_R },
    { SDL_CONTROLLER_BUTTON_START, PAD_START },      { SDL_CONTROLLER_BUTTON_BACK, PAD_SELECT },
    { SDL_CONTROLLER_BUTTON_LEFTSTICK, PAD_L3 },     { SDL_CONTROLLER_BUTTON_RIGHTSTICK, PAD_R3 },
    { SDL_CONTROLLER_BUTTON_GUIDE, PAD_GUIDE },
};

/* generic layout for pads without a mapping (see poll_js) */
static const pad_button l_jsmap[8] = { PAD_A, PAD_B, PAD_X, PAD_Y, PAD_L, PAD_R, PAD_SELECT, PAD_START };

static void bind_from_gc(SDL_GameControllerButtonBind b, pad_bind *d)
{
    switch (b.bindType)
    {
        case SDL_CONTROLLER_BINDTYPE_BUTTON:
            d->type = PAD_BIND_BUTTON; d->index = b.value.button; break;
        case SDL_CONTROLLER_BINDTYPE_AXIS:
            d->type = PAD_BIND_AXIS; d->index = b.value.axis; break;
        case SDL_CONTROLLER_BINDTYPE_HAT:
            d->type = PAD_BIND_HAT; d->index = b.value.hat.hat; d->hat_mask = b.value.hat.hat_mask; break;
        default: break;
    }
}

static void fill_bindings(pad_dev *p)
{
    size_t i;
    pad_layout *L = &p->lay;
    memset(L, 0, sizeof(*L));
    if (p->gc != NULL)
    {
        p->joy = SDL_GameControllerGetJoystick(p->gc);
        {
            /* the joystick's own name (the controller mapping's name is shared by every pad with the same GUID) */
            const char *nm = SDL_JoystickName(p->joy);
            snprintf(L->name, sizeof(L->name), "%s", nm ? nm : "Gamepad");
        }
        for (i = 0; i < sizeof(l_gcmap) / sizeof(l_gcmap[0]); i++)
            bind_from_gc(SDL_GameControllerGetBindForButton(p->gc, l_gcmap[i].sdl), &L->b[l_gcmap[i].logical]);
        bind_from_gc(SDL_GameControllerGetBindForAxis(p->gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT), &L->b[PAD_L2]);
        bind_from_gc(SDL_GameControllerGetBindForAxis(p->gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT), &L->b[PAD_R2]);
        bind_from_gc(SDL_GameControllerGetBindForAxis(p->gc, SDL_CONTROLLER_AXIS_LEFTX), &L->lx);
        bind_from_gc(SDL_GameControllerGetBindForAxis(p->gc, SDL_CONTROLLER_AXIS_LEFTY), &L->ly);
        bind_from_gc(SDL_GameControllerGetBindForAxis(p->gc, SDL_CONTROLLER_AXIS_RIGHTX), &L->rx);
        bind_from_gc(SDL_GameControllerGetBindForAxis(p->gc, SDL_CONTROLLER_AXIS_RIGHTY), &L->ry);
    }
    else if (p->js != NULL)
    {
        const char *nm = SDL_JoystickName(p->js);
        snprintf(L->name, sizeof(L->name), "%s", nm ? nm : "Joystick");
        L->generic = 1;
        p->joy = p->js;
        for (i = 0; i < 8; i++)
        {
            L->b[l_jsmap[i]].type = PAD_BIND_BUTTON;
            L->b[l_jsmap[i]].index = (int) i;
        }
        L->b[PAD_UP]    = (pad_bind) { PAD_BIND_HAT, 0, SDL_HAT_UP };
        L->b[PAD_DOWN]  = (pad_bind) { PAD_BIND_HAT, 0, SDL_HAT_DOWN };
        L->b[PAD_LEFT]  = (pad_bind) { PAD_BIND_HAT, 0, SDL_HAT_LEFT };
        L->b[PAD_RIGHT] = (pad_bind) { PAD_BIND_HAT, 0, SDL_HAT_RIGHT };
        if (SDL_JoystickNumAxes(p->js) >= 2)
        {
            L->lx = (pad_bind) { PAD_BIND_AXIS, 0, 0 };
            L->ly = (pad_bind) { PAD_BIND_AXIS, 1, 0 };
        }
    }
    {
        SDL_Joystick *jj = p->joy;
        int nb = jj ? SDL_JoystickNumButtons(jj) : 0, na = jj ? SDL_JoystickNumAxes(jj) : 0, nh = jj ? SDL_JoystickNumHats(jj) : 0;
        L->plausible = !L->generic || (na >= 2 && nb >= 4) || (nh >= 1 && nb >= 4 && nb <= 16);   /* not a keyboard's extra HID interface */
        DebugMessage(M64MSG_VERBOSE, "pad '%s': %d buttons, %d axes, %d hats%s%s", L->name, nb, na, nh,
                     L->generic ? ", no mapping" : "", L->plausible ? "" : ", not a plausible gamepad (not used for N64 players unless chosen)");
    }
    L->has_ls = L->lx.type == PAD_BIND_AXIS && L->ly.type == PAD_BIND_AXIS;
    L->has_rs = L->rx.type == PAD_BIND_AXIS && L->ry.type == PAD_BIND_AXIS;
}

static void rescan(void)
{
    int i, n = SDL_NumJoysticks();

    close_all();
    for (i = 0; i < n && l_npads < MAX_PADS; i++)
    {
        pad_dev *p = &l_pads[l_npads];
        memset(p, 0, sizeof(*p));
        p->devidx = i;
        log_guid(i);
        if (SDL_IsGameController(i))
        {
            p->gc = SDL_GameControllerOpen(i);
            if (p->gc)
            {
                p->id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(p->gc));
                DebugMessage(M64MSG_INFO, "pad %d: '%s' (game controller mapping)", i, SDL_GameControllerName(p->gc));
                fill_bindings(p);
                l_npads++;
            }
        }
        else
        {
            p->js = SDL_JoystickOpen(i);
            if (p->js)
            {
                p->id = SDL_JoystickInstanceID(p->js);
                DebugMessage(M64MSG_WARNING, "pad %d: '%s' has no SDL game controller mapping; using a generic layout guess "
                             "(buttons 0-7 = A B X Y L R Select Start, hat 0 = D-pad); the menu navigation uses it, N64 mapping uses the remap UI.",
                             i, SDL_JoystickName(p->js));
                fill_bindings(p);
                l_npads++;
            }
        }
    }
    l_numjoy_seen = n;
    l_generation++;
}

int pad_init(void)
{
    const char *envdb;

    if (l_inited)
        return 0;
    /* Without this SDL discards joystick input while our window lacks focus (which is the normal state under
     * a compositor, and the core's own VidExt sets the same hint). */
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0)
    {
        DebugMessage(M64MSG_ERROR, "SDL joystick init failed: %s", SDL_GetError());
        return -1;
    }
    /* SDL itself honours SDL_GAMECONTROLLERCONFIG / SDL_GAMECONTROLLERCONFIG_FILE; the distro sets those. */
    envdb = SDL_getenv("SDL_GAMECONTROLLERCONFIG_FILE");
    DebugMessage(M64MSG_VERBOSE, "SDL_GAMECONTROLLERCONFIG_FILE=%s", envdb ? envdb : "(unset)");
    l_inited = 1;
    rescan();
    if (l_npads == 0)
        DebugMessage(M64MSG_WARNING, "no gamepad found; the menu can only be driven by injected/keyboard input");
    return 0;
}

void pad_shutdown(void)
{
    if (!l_inited)
        return;
    close_all();
    SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);
    l_inited = 0;
}

void pad_set_swap_ab(int swap)           { l_swap_ab = swap; }
void pad_set_nav_deadzone(int percent)   { l_deadzone = percent < 10 ? 10 : percent > 90 ? 90 : percent; }
void pad_inject(uint32_t mask)           { l_inject = mask; }
void pad_enable_keyboard(int enable)     { l_keyboard = enable; }


static uint32_t stick_to_dir(int x, int y)
{
    int thr = 32767 * l_deadzone / 100;
    uint32_t m = 0;
    if (y < -thr) m |= PADBIT(PAD_UP);
    if (y >  thr) m |= PADBIT(PAD_DOWN);
    if (x < -thr) m |= PADBIT(PAD_LEFT);
    if (x >  thr) m |= PADBIT(PAD_RIGHT);
    return m;
}

static uint32_t poll_gc(SDL_GameController *gc, uint32_t *digital)
{
    uint32_t m = 0;
    size_t i;
    for (i = 0; i < sizeof(l_gcmap) / sizeof(l_gcmap[0]); i++)
        if (SDL_GameControllerGetButton(gc, l_gcmap[i].sdl))
            m |= PADBIT(l_gcmap[i].logical);
    if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16384) m |= PADBIT(PAD_L2);
    if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16384) m |= PADBIT(PAD_R2);
    *digital |= m;
    m |= stick_to_dir(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX),
                      SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY));
    return m;
}

static uint32_t poll_js(SDL_Joystick *js, uint32_t *digital)
{
    uint32_t m = 0;
    int i, nb = SDL_JoystickNumButtons(js);
    for (i = 0; i < nb && i < 8; i++)
        if (SDL_JoystickGetButton(js, i))
            m |= PADBIT(l_jsmap[i]);
    if (SDL_JoystickNumHats(js) > 0)
    {
        Uint8 h = SDL_JoystickGetHat(js, 0);
        if (h & SDL_HAT_UP) m |= PADBIT(PAD_UP);
        if (h & SDL_HAT_DOWN) m |= PADBIT(PAD_DOWN);
        if (h & SDL_HAT_LEFT) m |= PADBIT(PAD_LEFT);
        if (h & SDL_HAT_RIGHT) m |= PADBIT(PAD_RIGHT);
    }
    *digital |= m;
    if (SDL_JoystickNumAxes(js) >= 2)
        m |= stick_to_dir(SDL_JoystickGetAxis(js, 0), SDL_JoystickGetAxis(js, 1));
    return m;
}

static uint32_t poll_keyboard(void)
{
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    uint32_t m = 0;
    if (k == NULL)
        return 0;
    if (k[SDL_SCANCODE_UP]) m |= PADBIT(PAD_UP);
    if (k[SDL_SCANCODE_DOWN]) m |= PADBIT(PAD_DOWN);
    if (k[SDL_SCANCODE_LEFT]) m |= PADBIT(PAD_LEFT);
    if (k[SDL_SCANCODE_RIGHT]) m |= PADBIT(PAD_RIGHT);
    if (k[SDL_SCANCODE_Z]) m |= PADBIT(PAD_A);
    if (k[SDL_SCANCODE_X]) m |= PADBIT(PAD_B);
    if (k[SDL_SCANCODE_RETURN]) m |= PADBIT(PAD_START);
    if (k[SDL_SCANCODE_RSHIFT]) m |= PADBIT(PAD_SELECT);
    return m;
}

uint32_t pad_swap_ab_mask(uint32_t m)
{
    uint32_t a = (m >> PAD_A) & 1, b = (m >> PAD_B) & 1;
    m &= ~(PADBIT(PAD_A) | PADBIT(PAD_B));
    return m | (a << PAD_B) | (b << PAD_A);
}

/* all pads + test input, without calling SDL_JoystickUpdate().  *digital gets the buttons only; if per_pad is
 * not NULL it receives each pad's own digital state (and per_pad[MAX_PADS] the keyboard/injected input). */
static uint32_t read_all(uint32_t *digital, uint32_t *per_pad)
{
    uint32_t m = 0, d = 0;
    int i;

    if (per_pad != NULL)
        memset(per_pad, 0, sizeof(uint32_t) * (MAX_PADS + 1));
    if (l_inited)
    {
        if (SDL_NumJoysticks() != l_numjoy_seen)     /* hot-plug */
            rescan();
        for (i = 0; i < l_npads; i++)
        {
            uint32_t pd = 0;
            m |= l_pads[i].gc ? poll_gc(l_pads[i].gc, &pd) : poll_js(l_pads[i].js, &pd);
            d |= pd;
            if (per_pad != NULL)
                per_pad[i] = pd;
        }
    }
    {
        uint32_t k = l_keyboard ? poll_keyboard() : 0;
        m |= k | l_inject;
        d |= k | l_inject;
        if (per_pad != NULL)
            per_pad[MAX_PADS] = k | l_inject;
    }
    if (digital != NULL)
        *digital = d;
    return m;
}

int pad_read_digital(uint32_t out[PAD_MAX_PADS + 1])
{
    read_all(NULL, out);
    return l_npads;
}

int pad_index_for(SDL_Joystick *js)
{
    int i;
    for (i = 0; i < l_npads; i++)
        if (l_pads[i].joy == js)
            return i;
    return -1;
}

const pad_bind *pad_bindings_for(SDL_Joystick *js)
{
    int i = pad_index_for(js);
    return i >= 0 ? l_pads[i].lay.b : NULL;
}

int pad_count(void)                         { return l_npads; }
int pad_generation(void)                    { return l_generation; }
const char *pad_name(int pad)               { return pad >= 0 && pad < l_npads ? l_pads[pad].lay.name : ""; }
SDL_Joystick *pad_joystick(int pad)         { return pad >= 0 && pad < l_npads ? l_pads[pad].joy : NULL; }
int pad_device_index(int pad)               { return pad >= 0 && pad < l_npads ? l_pads[pad].devidx : -1; }

int pad_layout_get(int pad, pad_layout *out)
{
    if (pad < 0 || pad >= l_npads)
        return -1;
    *out = l_pads[pad].lay;
    return 0;
}

int pad_logical_for(int pad, const padsrc *s)
{
    int i;
    if (pad < 0 || pad >= l_npads || s == NULL || s->type == PAD_BIND_NONE)
        return -1;
    for (i = 0; i < PAD_NUM; i++)
    {
        const pad_bind *b = &l_pads[pad].lay.b[i];
        if (b->type != (int) s->type || b->index != s->index)
            continue;
        if (b->type == PAD_BIND_HAT && !(b->hat_mask & s->hat_mask))
            continue;
        if (b->type == PAD_BIND_AXIS && i != PAD_L2 && i != PAD_R2)
            continue;
        return i;
    }
    return -1;
}

/* SDL_JoystickUpdate, then advance the hotkey filter and the N64 players (inputfilter_after_update) */
void pad_refresh(void)
{
    if (l_inited)
    {
        SDL_JoystickUpdate();
        inputfilter_after_update();
    }
}

uint32_t pad_poll(void)
{
    uint32_t m;

    pad_refresh();
    m = read_all(NULL, NULL);
    if (l_swap_ab)
        m = pad_swap_ab_mask(m);
    return m;
}

/* ---- capture (remap UI) ---- */
#define CAP_MAXBTN 64
#define CAP_MAXAX  16
#define CAP_MAXHAT 4
#define CAP_AXIS_THRESHOLD 20000

static padsrc l_cap_inject;
static int    l_cap_injected;

static struct {
    int      pad;
    uint64_t btn_ignore[MAX_PADS];
    uint8_t  hat_ignore[MAX_PADS][CAP_MAXHAT];
    int      base[MAX_PADS][CAP_MAXAX];
} l_cap;

void pad_capture_begin(int pad)
{
    int i, k;
    memset(&l_cap, 0, sizeof(l_cap));
    l_cap.pad = pad;
    if (!l_inited)
        return;
    for (i = 0; i < l_npads; i++)
    {
        SDL_Joystick *j = l_pads[i].joy;
        int nb, nh, na;
        if (j == NULL)
            continue;
        nb = SDL_JoystickNumButtons(j); nh = SDL_JoystickNumHats(j); na = SDL_JoystickNumAxes(j);
        for (k = 0; k < nb && k < CAP_MAXBTN; k++)
            if (SDL_JoystickGetButton(j, k))
                l_cap.btn_ignore[i] |= (uint64_t) 1 << k;
        for (k = 0; k < nh && k < CAP_MAXHAT; k++)
            l_cap.hat_ignore[i][k] = SDL_JoystickGetHat(j, k);
        for (k = 0; k < na && k < CAP_MAXAX; k++)
            l_cap.base[i][k] = SDL_JoystickGetAxis(j, k);
    }
}

void pad_capture_inject(const padsrc *s)
{
    l_cap_inject = *s;
    l_cap_injected = 1;
}

int pad_capture_poll(padsrc *out, int *pad_out)
{
    int i, k, found = 0;

    if (l_cap_injected)                       /* test hook: --debug-script cap:button:3 */
    {
        l_cap_injected = 0;
        *out = l_cap_inject;
        if (pad_out != NULL)
            *pad_out = l_cap.pad;
        return 1;
    }
    if (!l_inited)
        return 0;
    for (i = 0; i < l_npads && !found; i++)
    {
        SDL_Joystick *j = l_pads[i].joy;
        int nb, nh, na;
        if (j == NULL || (l_cap.pad >= 0 && l_cap.pad != i))
            continue;
        nb = SDL_JoystickNumButtons(j); nh = SDL_JoystickNumHats(j); na = SDL_JoystickNumAxes(j);
        for (k = 0; k < nb && k < CAP_MAXBTN && !found; k++)
        {
            uint64_t bit = (uint64_t) 1 << k;
            if (!SDL_JoystickGetButton(j, k))
                l_cap.btn_ignore[i] &= ~bit;                /* released: usable again */
            else if (!(l_cap.btn_ignore[i] & bit))
            {
                *out = (padsrc) { PAD_BIND_BUTTON, 0, (int16_t) k, 0 };
                found = 1;
            }
        }
        for (k = 0; k < nh && k < CAP_MAXHAT && !found; k++)
        {
            Uint8 h = SDL_JoystickGetHat(j, k);
            static const Uint8 dirs[4] = { SDL_HAT_UP, SDL_HAT_RIGHT, SDL_HAT_DOWN, SDL_HAT_LEFT };
            int d;
            l_cap.hat_ignore[i][k] &= h;
            for (d = 0; d < 4 && !found; d++)
                if ((h & dirs[d]) && !(l_cap.hat_ignore[i][k] & dirs[d]))
                {
                    *out = (padsrc) { PAD_BIND_HAT, dirs[d], (int16_t) k, 0 };
                    found = 1;
                }
        }
        for (k = 0; k < na && k < CAP_MAXAX && !found; k++)
        {
            int d = SDL_JoystickGetAxis(j, k) - l_cap.base[i][k];
            if (d >= CAP_AXIS_THRESHOLD || d <= -CAP_AXIS_THRESHOLD)
            {
                *out = (padsrc) { PAD_BIND_AXIS, 0, (int16_t) k, (int8_t) (d > 0 ? 1 : -1) };
                found = 1;
            }
        }
        if (found && pad_out != NULL)
            *pad_out = i;
    }
    return found;
}

static const char *hat_dir_name(int mask)
{
    return mask == SDL_HAT_UP ? "Up" : mask == SDL_HAT_DOWN ? "Down" : mask == SDL_HAT_LEFT ? "Left" :
           mask == SDL_HAT_RIGHT ? "Right" : "?";
}

void pad_format_src_short(const padsrc *s, char *out, size_t size)
{
    switch (s->type)
    {
        case PAD_BIND_BUTTON: snprintf(out, size, "Btn %d", s->index); break;
        case PAD_BIND_HAT:    snprintf(out, size, "Hat%d %s", s->index, hat_dir_name(s->hat_mask)); break;
        case PAD_BIND_AXIS:   snprintf(out, size, "Axis%d%c", s->index, s->dir < 0 ? '-' : '+'); break;
        default:              snprintf(out, size, "-"); break;
    }
}

void pad_format_src(int pad, const padsrc *s, char *out, size_t size)
{
    char raw[32];
    int lg;
    pad_format_src_short(s, raw, sizeof(raw));
    lg = pad_logical_for(pad, s);
    if (s->type != PAD_BIND_NONE && lg >= 0)
    {
        char up[8];
        size_t n;
        snprintf(up, sizeof(up), "%s", l_names[lg]);
        for (n = 0; up[n]; n++)
            up[n] = (char) toupper((unsigned char) up[n]);
        snprintf(out, size, "%s (%s)", raw, up);
    }
    else
        snprintf(out, size, "%s", raw);
}

/* ---- edge detection ---- */
#define REPEAT_DELAY_MS 380
#define REPEAT_RATE_MS  90

void pad_tracker_reset(pad_tracker *t, uint32_t held_now)
{
    memset(t, 0, sizeof(*t));
    t->held = t->prev = held_now;
}

void pad_tracker_update(pad_tracker *t, uint32_t held, uint32_t now_ms)
{
    int i;
    uint32_t rising = held & ~t->prev;

    t->pressed = rising;
    t->released = t->prev & ~held;
    for (i = 0; i < PAD_NUM; i++)
    {
        uint32_t bit = PADBIT(i);
        if (rising & bit)
        {
            t->next_repeat_ms[i] = now_ms + REPEAT_DELAY_MS;
        }
        else if ((held & bit) && (PAD_DIR_MASK & bit) && (int32_t) (now_ms - t->next_repeat_ms[i]) >= 0)
        {
            t->pressed |= bit;
            t->next_repeat_ms[i] = now_ms + REPEAT_RATE_MS;
        }
    }
    t->held = held;
    t->prev = held;
}
