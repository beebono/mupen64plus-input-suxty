/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - signals.h
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

/* SIGUSR1 / SIGUSR2 and M64P_AUTOLOAD the way spruce's Game Switcher and "Emulator menu" button use them. The handlers
 * only set flags; session.c acts on them from the swap, on the emulation thread. */

#ifndef SIGNALS_H
#define SIGNALS_H

typedef enum {
    SIGS_NONE = 0,
    SIGS_MENU,              /* SIGUSR2: open the menu, or close it if it is open (SIGUSR1 with the menu open closes it, too) */
    SIGS_SAVE_QUIT          /* SIGUSR1: write the auto-save, then stop */
} sigs_action;

typedef struct {
    int menu_open;
    int quitting;           /* a quit is already under way: later signals are consumed and ignored */
    int busy;               /* a save or load is in flight: SIGUSR1 stays pending */
} sigs_state;

void sigs_install(int enable);          /* sigaction, SA_RESTART; also what the core's own handler is replaced with */
void sigs_arm(void);                    /* once, from the first frame: the core installs its SIGUSR1 handler after PluginStartup */
void sigs_release(void);                /* PluginShutdown: the handlers must not outlive the library */
sigs_action sigs_poll(const sigs_state *st);

/* M64P_AUTOLOAD starting with '1': 1 and unset, so the core does not load slot 0 */
int  sigs_autoload_claim(void);

#endif /* SIGNALS_H */
