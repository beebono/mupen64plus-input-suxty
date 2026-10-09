/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-ui-suxty - menu.h
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


/* The pause menu: a stack of pages built from ui.h widgets.
 *
 * menu_frame() draws one frame and returns what the user chose.  It never
 * talks to the core itself; session.c turns the result into core commands.
 * That keeps the menu free of threading concerns.
 */

#ifndef MENU_H
#define MENU_H

#include <stdint.h>

typedef enum {
    MENU_NONE = 0,     /* stay in the menu */
    MENU_RESUME,       /* close the menu, continue the game */
    MENU_RESET,        /* hard-reset (power cycle) the emulated console, then continue */
    MENU_RESET_SOFT,   /* soft-reset: like pressing the console's reset button, RDRAM is kept */
    MENU_QUIT,         /* stop emulation (saving the auto slot first if that is on) */
    MENU_SAVE_STATE,   /* save to menu_action_slot() and close the menu */
    MENU_LOAD_STATE    /* load menu_action_slot() and close the menu */
} menu_result;

void        menu_set_game_name(const char *name);
void        menu_set_quit_saves(int saves);        /* the quit confirmation mentions the auto-save */
void        menu_open(int current_slot);           /* back to the top page */
int         menu_action_slot(void);                /* for MENU_SAVE_STATE / MENU_LOAD_STATE */
int         menu_selected_slot(void);              /* the carousel's slot (0-9), -1 if the auto slot is selected */
/* `pressed`: new presses this frame (with auto-repeat on the D-pad); `held`: everything held now */
menu_result menu_frame(int w, int h, uint32_t pressed, uint32_t held);
/* The menu combo was pressed while the menu is open.  1 = the menu used it (it confirmed a new combo), 0 = close. */
int         menu_combo_pressed(uint32_t held);
/* The menu is closing: unconfirmed changes are reverted, pending config is written. */
void        menu_closing(void);

#endif /* MENU_H */
