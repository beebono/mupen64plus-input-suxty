/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - menu.h
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


/* never talks to the core: session.c turns the result into core commands */

#ifndef MENU_H
#define MENU_H

#include <stdint.h>

typedef enum {
    MENU_NONE = 0,
    MENU_RESUME,
    MENU_RESET,
    MENU_RESET_SOFT,   /* soft reset: RDRAM is kept */
    MENU_QUIT,
    MENU_SAVE_STATE,
    MENU_LOAD_STATE
} menu_result;

void        menu_set_game_name(const char *name);
void        menu_set_quit_saves(int saves);
void        menu_open(int current_slot);
int         menu_action_slot(void);
int         menu_selected_slot(void);              /* -1 if the auto slot is selected */
/* pressed: new presses (D-pad auto-repeats); held: everything held now */
menu_result menu_frame(int w, int h, uint32_t pressed, uint32_t held);
/* 1 = the menu used it (confirmed a new combo), 0 = close */
int         menu_combo_pressed(uint32_t held);
void        menu_closing(void);

#endif /* MENU_H */
