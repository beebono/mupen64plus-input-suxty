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

/* state advances in the GetKeys poll where the game would first see the press, so not one frame leaks; a sticky
 * button stays hidden until physically released */

#ifndef INPUTFILTER_H
#define INPUTFILTER_H

#include <stdint.h>

#include "pad.h"

/* combo_buttons: all buttons used by hotkey combos (sticky even if pressed just before the hotkey) */
void inputfilter_configure(int enabled, uint32_t hotkey, uint32_t combo_buttons);

void inputfilter_force(int on);

void inputfilter_bypass(int on);

void inputfilter_update(const uint32_t digital[PAD_MAX_PADS + 1]);

int      inputfilter_phys_button(SDL_Joystick *js, int button);
int      inputfilter_phys_hat(SDL_Joystick *js, int hat);
int      inputfilter_phys_axis(SDL_Joystick *js, int axis);
int      inputfilter_phys_axis_rest(SDL_Joystick *js, int axis);

void     inputfilter_after_update(void);

int      inputfilter_active(void);
uint32_t inputfilter_hidden_count(void);

#endif /* INPUTFILTER_H */
