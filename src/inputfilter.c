/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - inputfilter.c
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
#include <string.h>

#include <SDL.h>

#include "inputfilter.h"
#include "m64p_types.h"
#include "main.h"
#include "pad.h"
#include "remap.h"

/* All of this runs on the emulation thread (the one that calls GetKeys and the session hook).
 *
 * State is kept per controller: holding the hotkey on pad 1 neutralizes pad 1 only, so a second player's
 * controller keeps working.  Slot PAD_MAX_PADS is the keyboard / test-injected input, which has no SDL joystick
 * to hide anything from. */

#define NDEV (PAD_MAX_PADS + 1)

typedef struct {
    uint32_t prev;           /* digital state at the last update */
    uint32_t sticky;         /* buttons hidden until they are physically released */
    int      hot;            /* hotkey physically held */
} dev_state;

static volatile int l_bypass;
static int          l_enabled;
static uint32_t     l_hot_mask;
static uint32_t     l_combo_buttons;
static int          l_force;
static dev_state    l_dev[NDEV];
static int          l_any;               /* cached: anything hidden right now? */
static uint32_t     l_hidden_count;

static void recompute_any(void)
{
    int i, any = 0;
    for (i = 0; i < NDEV; i++)
        if (l_dev[i].hot || l_dev[i].sticky)
            any = 1;
    l_any = any;
}

void inputfilter_configure(int enabled, uint32_t hotkey, uint32_t combo_buttons)
{
    l_enabled = enabled;
    l_hot_mask = hotkey;
    l_combo_buttons = combo_buttons | hotkey;
    if (!enabled)
    {
        memset(l_dev, 0, sizeof(l_dev));
        l_any = 0;
    }
}

void inputfilter_bypass(int on)          { l_bypass += on ? 1 : -1; if (l_bypass < 0) l_bypass = 0; }

void inputfilter_force(int on)
{
    int i;
    l_force = on;
    for (i = 0; i < NDEV; i++)
        l_dev[i].sticky |= l_dev[i].prev;    /* whatever is held right now (the combo, the menu keys) is hidden until released */
    recompute_any();
    remap_update();                          /* the virtual pad the plugin reads follows at once */
}

int      inputfilter_active(void)        { return l_enabled && (l_force || l_any); }
uint32_t inputfilter_hidden_count(void)  { return l_hidden_count; }

void inputfilter_update(const uint32_t d[PAD_MAX_PADS + 1])
{
    int i;

    if (!l_enabled)
        return;
    for (i = 0; i < NDEV; i++)
    {
        dev_state *v = &l_dev[i];
        int hot = l_hot_mask != 0 && (d[i] & l_hot_mask) == l_hot_mask;
        uint32_t rising = d[i] & ~v->prev;

        if (hot)
        {
            if (!v->hot)
                v->sticky |= d[i] & l_combo_buttons;     /* e.g. the face button pressed a hair before the hotkey */
            v->sticky |= rising | (d[i] & l_hot_mask);
        }
        if (l_force)
            v->sticky |= d[i];
        v->sticky &= d[i];                               /* released buttons are visible again */
        if (hot != v->hot)
            DebugMessage(M64MSG_VERBOSE, "input filter: hotkey %s on pad %d (sticky mask %08x, %u values hidden from the game so far)",
                         hot ? "held" : "released", i, (unsigned) v->sticky, (unsigned) l_hidden_count);
        v->hot = hot;
        v->prev = d[i];
    }
    recompute_any();
}

static int filtering(void)
{
    return l_bypass == 0 && l_enabled && (l_force || l_any);
}

/* the game sees a neutral controller: the menu is open, or the hotkey is held on this very pad */
static int full_mask(SDL_Joystick *js)
{
    int dev;
    if (l_force)
        return 1;
    dev = pad_index_for(js);
    return dev >= 0 && l_dev[dev].hot;
}

/* is control `idx` of type `type` bound to a button of this pad that is hidden until released?  For hats, adds the
 * direction bits to hide to *hat_bits. */
static int sticky_hides(SDL_Joystick *js, int type, int idx, int *hat_bits)
{
    const pad_bind *b = pad_bindings_for(js);
    int dev = pad_index_for(js);
    uint32_t sticky;
    int i, hide = 0;

    if (b == NULL || dev < 0)
        return 0;
    sticky = l_dev[dev].sticky;
    for (i = 0; i < PAD_NUM; i++)
    {
        if (!(sticky & PADBIT(i)) || (int) b[i].type != type || b[i].index != idx)
            continue;
        if (type == PAD_BIND_HAT)
        {
            if (hat_bits != NULL)
                *hat_bits |= b[i].hat_mask;
        }
        hide = 1;
    }
    return hide;
}

static Sint16 axis_rest(SDL_Joystick *js, int axis)
{
    Sint16 st = 0;
    /* triggers rest at their end stop (-32768), sticks at 0 */
    if (SDL_JoystickGetAxisInitialState(js, axis, &st) == SDL_TRUE && (st > 20000 || st < -20000))
        return st;
    return 0;
}

/* ---- the physical pad as the plugin would see it (also what the remap layer evaluates) -------------- */

static Uint8 phys_button(SDL_Joystick *joystick, int button)
{
    Uint8 v = SDL_JoystickGetButton(joystick, button);
    if (v && filtering() && (full_mask(joystick) || sticky_hides(joystick, PAD_BIND_BUTTON, button, NULL)))
    {
        l_hidden_count++;
        return 0;
    }
    return v;
}

static Sint16 phys_axis(SDL_Joystick *joystick, int axis)
{
    Sint16 v = SDL_JoystickGetAxis(joystick, axis);
    if (filtering() && (full_mask(joystick) || sticky_hides(joystick, PAD_BIND_AXIS, axis, NULL)))
    {
        Sint16 rest = axis_rest(joystick, axis);
        if (v != rest)
            l_hidden_count++;
        return rest;
    }
    return v;
}

static Uint8 phys_hat(SDL_Joystick *joystick, int hat)
{
    Uint8 v = SDL_JoystickGetHat(joystick, hat);
    if (v != SDL_HAT_CENTERED && filtering())
    {
        if (full_mask(joystick))
        {
            l_hidden_count++;
            return SDL_HAT_CENTERED;
        }
        {
            int bits = 0;
            if (sticky_hides(joystick, PAD_BIND_HAT, hat, &bits) && (v & bits))
            {
                l_hidden_count++;
                return (Uint8) (v & ~bits);
            }
        }
    }
    return v;
}

int inputfilter_phys_button(SDL_Joystick *js, int b)  { return phys_button(js, b); }
int inputfilter_phys_hat(SDL_Joystick *js, int h)     { return phys_hat(js, h); }
int inputfilter_phys_axis(SDL_Joystick *js, int a)    { return phys_axis(js, a); }
int inputfilter_phys_axis_rest(SDL_Joystick *js, int a)
{
    return axis_rest(js, a);
}

/* After SDL_JoystickUpdate() (pad_refresh): advance the hotkey filter, then evaluate the N64 mapping of every player. */
void inputfilter_after_update(void)
{
    if (l_bypass == 0)
    {
        if (l_enabled)
        {
            uint32_t d[PAD_MAX_PADS + 1];
            pad_read_digital(d);
            inputfilter_update(d);
        }
        remap_update();
    }
}
