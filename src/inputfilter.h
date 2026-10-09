/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - inputfilter.h
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
 * Problem: the game and the menu read the same physical controller.  When the user presses Select+R for a quick-save,
 * or Select+Start for the menu, the game must not also see Select, R or Start.
 *
 * suxty is the input plugin, so it decides what the game sees: GetKeys() builds the N64 state from the pad through
 * the padmap model (remap.c), reading the physical controls via inputfilter_phys_*(), which hide the controls that are
 * filtered right now.  Nothing is intercepted in SDL.
 *
 * When is input hidden?
 *   - while the hotkey button is held on a controller: the game sees that controller neutral (every button, hat
 *     and axis at rest), so "hotkey + button" combos never leak anything (other players' controllers are
 *     unaffected);
 *   - while the menu is open (inputfilter_force()); and
 *   - "sticky": a button that was pressed while one of the above applied stays hidden until it is
 *     physically released, so letting go of the hotkey before the other button does not leak that button
 *     to the game.
 * The state is advanced in the same GetKeys poll in which the game would first see the press, so not even one frame
 * leaks.  Keyboard / test-injected input has no SDL joystick to hide anything from (it never reaches the game).
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

/* While > 0 the filter is off and the real values are read (the menu's own polling, the mapping test page). */
void inputfilter_bypass(int on);

/* Feed the current digital button state of every pad (see pad_read_digital()). */
void inputfilter_update(const uint32_t digital[PAD_MAX_PADS + 1]);

/* The physical pad state as the game may see it (hotkey / menu filtering applied), for the remap layer (remap.c)
 * which builds the N64 state from it.  Under inputfilter_bypass() these are the real values. */
int      inputfilter_phys_button(SDL_Joystick *js, int button);
int      inputfilter_phys_hat(SDL_Joystick *js, int hat);
int      inputfilter_phys_axis(SDL_Joystick *js, int axis);
int      inputfilter_phys_axis_rest(SDL_Joystick *js, int axis);

void     inputfilter_after_update(void);    /* after SDL_JoystickUpdate(): advance the filter and the N64 players */

int      inputfilter_active(void);          /* currently hiding something? */
uint32_t inputfilter_hidden_count(void);    /* statistics: values hidden from the game so far */

#endif /* INPUTFILTER_H */
