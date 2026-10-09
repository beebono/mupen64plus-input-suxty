/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - pad.h
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


/* Gamepad abstraction for the suxty menu and the N64 controllers.
 *
 * suxty only ever *polls* controller state (SDL_GameControllerGetButton etc.).  It never consumes the SDL event
 * queue for controller input, so it cannot steal events from the core's event filter.
 */

#ifndef PAD_H
#define PAD_H

#include <stddef.h>
#include <stdint.h>

#include <SDL.h>

typedef enum {
    PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT,
    PAD_A, PAD_B, PAD_X, PAD_Y,
    PAD_L, PAD_R,
    PAD_START, PAD_SELECT,
    PAD_L3, PAD_R3, PAD_GUIDE,
    PAD_L2, PAD_R2,                 /* the triggers (analog axes or plain buttons, depending on the pad) */
    PAD_NUM
} pad_button;

#define PADBIT(b) (1u << (b))
#define PAD_DIR_MASK (PADBIT(PAD_UP) | PADBIT(PAD_DOWN) | PADBIT(PAD_LEFT) | PADBIT(PAD_RIGHT))

int  pad_init(void);
void pad_shutdown(void);

/* Bitmask of currently held logical buttons, all controllers merged.  Also folds in
 * the left stick as D-pad (beyond the nav deadzone) and any injected test buttons. */
uint32_t pad_poll(void);
void     pad_refresh(void);     /* SDL_JoystickUpdate + input filter / N64 player update, no button masks (GetKeys) */

/* The physical SDL_Joystick control behind a logical button, as the SDL game controller mapping (or the
 * generic fallback layout) defines it.  inputfilter.c uses this to hide exactly those controls from the
 * game, and the remap UI (padmap.c) uses it to build the preset layouts. */
typedef struct {
    enum { PAD_BIND_NONE = 0, PAD_BIND_BUTTON, PAD_BIND_AXIS, PAD_BIND_HAT } type;
    int index;        /* button / axis / hat index */
    int hat_mask;     /* SDL_HAT_* bits, for PAD_BIND_HAT */
} pad_bind;

/* Bindings of the pad that owns `js`, PAD_NUM entries, or NULL if suxty does not know that joystick. */
const pad_bind *pad_bindings_for(SDL_Joystick *js);

/* One physical control of an SDL joystick, in the raw domain of SDL joysticks (button / hat / axis
 * indices).  Used by the remap layer and by the capture prompt.  An axis source is a half axis: `dir` is the
 * direction (+1 / -1) in which it counts as pressed, measured from the axis' rest value (so a trigger that
 * rests at -32768 is "axis N, +"). */
typedef struct {
    uint8_t type;       /* PAD_BIND_NONE / BUTTON / AXIS / HAT */
    uint8_t hat_mask;   /* PAD_BIND_HAT: one SDL_HAT_* direction */
    int16_t index;
    int8_t  dir;        /* PAD_BIND_AXIS: +1 / -1 */
} padsrc;

/* Everything the remap layer needs to know about a connected pad. */
typedef struct {
    char     name[64];
    int      generic;        /* no SDL game controller mapping: the layout below is a guess */
    pad_bind b[PAD_NUM];     /* logical button -> physical control */
    pad_bind lx, ly, rx, ry; /* stick axes (PAD_BIND_AXIS) if the pad has them */
    int      has_ls, has_rs;
    int      plausible;      /* looks like a real gamepad (mapped, or enough buttons plus a stick / hat): auto-assignment only uses these */
} pad_layout;

int            pad_count(void);                         /* connected pads suxty knows */
int            pad_generation(void);                    /* changes when the list was rebuilt (hot-plug): indices are then stale */
const char    *pad_name(int pad);
SDL_Joystick  *pad_joystick(int pad);                   /* the SDL_Joystick behind a pad */
int            pad_device_index(int pad);               /* SDL device index */
int            pad_layout_get(int pad, pad_layout *out);   /* 0 ok */
/* Which logical button (pad_button) is this physical control?  -1 if none.  Axes match triggers only. */
int            pad_logical_for(int pad, const padsrc *s);

/* Button capture for the remap UI: remembers the current state of the pads as a baseline, then reports the
 * first control that is pressed / moved far enough.  Buttons that are already held when the capture begins
 * are ignored until they have been released.  pad < 0 listens to every pad. */
void pad_capture_begin(int pad);
int  pad_capture_poll(padsrc *out, int *pad_out);        /* 1 = captured something */
void pad_capture_inject(const padsrc *s);                /* test hook: the next poll returns this */

/* Human readable: "Btn 3", "Hat0 Up", "Axis2+", plus the logical name when there is one ("(A)"). */
void pad_format_src(int pad, const padsrc *s, char *out, size_t size);
void pad_format_src_short(const padsrc *s, char *out, size_t size);      /* without the logical name */
/* Digital buttons only (no stick-as-dpad), in the physical-button domain (A/B not swapped).  Does NOT call
 * SDL_JoystickUpdate().  out[i] is the state of pad i (i < PAD_MAX_PADS); out[PAD_MAX_PADS] holds the
 * keyboard / injected test input.  Returns the number of pads. */
#define PAD_MAX_PADS 8
int pad_read_digital(uint32_t out[PAD_MAX_PADS + 1]);
/* Index of the pad that owns joystick `js`, or -1. */
int pad_index_for(SDL_Joystick *js);
/* "A/B swap" for a logical mask: converts a mask in the user-facing domain to the physical one and back. */
uint32_t pad_swap_ab_mask(uint32_t mask);

void pad_set_swap_ab(int swap);              /* for Nintendo-labelled vs Xbox-labelled layouts */
void pad_set_nav_deadzone(int percent);      /* 10..90 */
void pad_inject(uint32_t mask);              /* test hook: virtual held buttons */
void pad_enable_keyboard(int enable);        /* desktop dev aid: arrows/Z/X/Enter/RShift */

/* "select+start" <-> mask. */
uint32_t    pad_parse_combo(const char *str);
void        pad_format_combo(uint32_t mask, char *out, size_t outsize);
const char *pad_button_name(pad_button b);

/* Edge detection with auto-repeat on the D-pad, for menu navigation. */
typedef struct {
    uint32_t held;
    uint32_t pressed;      /* rising edges this update (+ repeats for directions) */
    uint32_t released;
    uint32_t prev;
    uint32_t next_repeat_ms[PAD_NUM];
} pad_tracker;

void pad_tracker_reset(pad_tracker *t, uint32_t held_now);   /* treat currently held buttons as "already pressed" */
void pad_tracker_update(pad_tracker *t, uint32_t held, uint32_t now_ms);

#endif /* PAD_H */
