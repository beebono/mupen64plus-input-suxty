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


/* gamepad layer: only polls controller state, never consumes the SDL event queue: cannot steal events from the core's event filter */

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
    PAD_L2, PAD_R2,                 /* analog axes or plain buttons, depending on the pad */
    PAD_NUM
} pad_button;

#define PADBIT(b) (1u << (b))
#define PAD_DIR_MASK (PADBIT(PAD_UP) | PADBIT(PAD_DOWN) | PADBIT(PAD_LEFT) | PADBIT(PAD_RIGHT))

int  pad_init(void);
void pad_shutdown(void);

/* all controllers merged; folds in the left stick as D-pad and injected test buttons */
uint32_t pad_poll(void);
void     pad_refresh(void);

typedef struct {
    enum { PAD_BIND_NONE = 0, PAD_BIND_BUTTON, PAD_BIND_AXIS, PAD_BIND_HAT } type;
    int index;
    int hat_mask;
} pad_bind;

const pad_bind *pad_bindings_for(SDL_Joystick *js);

/* an axis source is a half axis: `dir` (+1 / -1) is where it counts as pressed, from the axis' rest value (a trigger
 * resting at -32768 is "axis N, +") */
typedef struct {
    uint8_t type;
    uint8_t hat_mask;
    int16_t index;
    int8_t  dir;
} padsrc;

typedef struct {
    char     name[64];
    int      generic;        /* no SDL game controller mapping: the layout below is a guess */
    pad_bind b[PAD_NUM];
    pad_bind lx, ly, rx, ry;
    int      has_ls, has_rs;
    int      plausible;      /* auto-assignment only uses plausible pads */
} pad_layout;

int            pad_count(void);
int            pad_generation(void);                    /* indices are stale after a change */
const char    *pad_name(int pad);
SDL_Joystick  *pad_joystick(int pad);
int            pad_device_index(int pad);
int            pad_layout_get(int pad, pad_layout *out);
/* -1 if none; axes match triggers only */
int            pad_logical_for(int pad, const padsrc *s);

/* buttons already held when the capture begins are ignored until released; pad < 0 listens to every pad */
void pad_capture_begin(int pad);
int  pad_capture_poll(padsrc *out, int *pad_out);
void pad_capture_inject(const padsrc *s);

void pad_format_src(int pad, const padsrc *s, char *out, size_t size);
void pad_format_src_short(const padsrc *s, char *out, size_t size);      /* always the raw name */
const char *pad_generic_name(pad_button b);
/* physical-button domain (A/B not swapped); does NOT call SDL_JoystickUpdate(); out[PAD_MAX_PADS] is keyboard /
 * injected input */
#define PAD_MAX_PADS 8
int pad_read_digital(uint32_t out[PAD_MAX_PADS + 1]);
int pad_index_for(SDL_Joystick *js);
uint32_t pad_swap_ab_mask(uint32_t mask);

void pad_set_swap_ab(int swap);
void pad_set_nav_deadzone(int percent);
void pad_inject(uint32_t mask);
void pad_enable_keyboard(int enable);

uint32_t    pad_parse_combo(const char *str);
void        pad_format_combo(uint32_t mask, char *out, size_t outsize);
const char *pad_button_name(pad_button b);

typedef struct {
    uint32_t held;
    uint32_t pressed;      /* rising edges (+ repeats for directions) */
    uint32_t released;
    uint32_t prev;
    uint32_t next_repeat_ms[PAD_NUM];
} pad_tracker;

void pad_tracker_reset(pad_tracker *t, uint32_t held_now);   /* currently held buttons count as already pressed */
void pad_tracker_update(pad_tracker *t, uint32_t held, uint32_t now_ms);

#endif /* PAD_H */
