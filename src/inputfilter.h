/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-ui-suxty - inputfilter.h
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

/* Hiding hotkey / menu input from the game.
 *
 * Problem: the input plugin and suxty read the same physical controller.  When the user presses
 * Select+R for a quick-save, or Select+Start for the menu, the game must not also see Select, R or
 * Start.
 *
 * Options considered (stock mupen64plus-input-sdl must keep working, unmodified):
 *   - SDL hints: none exist that hide buttons from one consumer.
 *   - Remapping the controller: the plugin reads raw SDL_Joystick indices through its own mapping;
 *     changing the SDL mapping does not affect it, and rewriting the plugin's config per hotkey press is
 *     not feasible.
 *   - A uinput proxy device with EVIOCGRAB: works in principle, but needs root/uinput and makes the
 *     plugin's device selection (by name/index) fragile.
 *   - Filtering the plugin's view of the joystick API (chosen).  The plugin, the core and suxty live in
 *     one process.  The input plugin only reads state through SDL_JoystickGetButton / GetAxis / GetHat
 *     after an SDL_JoystickUpdate().  suxty's executable defines those four functions (listed in
 *     projects/unix/suxty.exports so they are exported), so the dynamic linker binds the plugin's calls to
 *     them instead of the ones in libSDL2, and they forward to the real functions through
 *     dlsym(RTLD_NEXT, ...).  The overrides hide controls from the plugin while the filter is active, and
 *     suxty itself always reads the real state (inputfilter_bypass()).
 *
 * When is input hidden?
 *   - while the hotkey button is held on a controller: the game sees that controller neutral (every button, hat
 *     and axis at rest), so "hotkey + button" combos never leak anything (other players' controllers are
 *     unaffected);
 *   - while the menu is open (inputfilter_force()); and
 *   - "sticky": a button that was pressed while one of the above applied stays hidden until it is
 *     physically released, so letting go of the hotkey before the other button does not leak that button
 *     to the game.
 * The state is advanced on every SDL_JoystickUpdate(), i.e. in the same poll in which the plugin would
 * first see the press, so not even one frame leaks.
 *
 * Since M3 the same overrides also *are* the controller the input plugin reads for each N64 player: remap.c
 * configures the plugin for a fixed virtual pad and these functions answer for that joystick with the N64 state
 * computed from the player's mapping and the filtered real pad (see remap.h).
 *
 * Limits: only controllers whose layout suxty knows (SDL game controller mapping, or the generic layout)
 * are filtered per button; plugins that use the SDL_GameController API or evdev directly are not covered.
 * Keyboard input is never filtered.
 */

#ifndef INPUTFILTER_H
#define INPUTFILTER_H

#include <stdint.h>

#include "pad.h"

/* enabled: master switch.  hotkey: the hotkey button mask (physical A/B domain).  combo_buttons: union of
 * all buttons used by hotkey combos (they are made sticky even if pressed just before the hotkey). */
void inputfilter_configure(int enabled, uint32_t hotkey, uint32_t combo_buttons);

/* The menu is open (or has just closed): everything currently held becomes sticky. */
void inputfilter_force(int on);

/* While > 0, the SDL overrides pass the real values through (suxty's own polling). */
void inputfilter_bypass(int on);

/* Feed the current digital button state of every pad (see pad_read_digital(); called by the SDL_JoystickUpdate
 * override). */
void inputfilter_update(const uint32_t digital[PAD_MAX_PADS + 1]);

/* The physical pad state as the input plugin would see it (hotkey / menu filtering applied), for the remap
 * layer (remap.c) which builds the virtual N64 pad from it.  Under inputfilter_bypass() these are the real values. */
int      inputfilter_phys_button(SDL_Joystick *js, int button);
int      inputfilter_phys_hat(SDL_Joystick *js, int hat);
int      inputfilter_phys_axis(SDL_Joystick *js, int axis);
int      inputfilter_phys_axis_rest(SDL_Joystick *js, int axis);

int      inputfilter_active(void);          /* currently hiding something? */
uint32_t inputfilter_hidden_count(void);    /* statistics: values hidden from the plugin so far */

#endif /* INPUTFILTER_H */
