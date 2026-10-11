/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - signals.c
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

#include <signal.h>
#include <stdlib.h>

#include "signals.h"

static volatile sig_atomic_t l_usr1, l_usr2;
static int l_enabled, l_armed;

static void on_signal(int sig)
{
    if (sig == SIGUSR1)
        l_usr1 = 1;
    else
        l_usr2 = 1;
}

static void set_handler(void (*fn)(int))
{
    struct sigaction sa;
    sa.sa_handler = fn;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);
}

void sigs_install(int enable)
{
    l_enabled = enable;
    l_armed = 0;
    l_usr1 = l_usr2 = 0;
    if (enable)
        set_handler(on_signal);
}

void sigs_arm(void)
{
    if (!l_enabled || l_armed)
        return;
    l_armed = 1;
    set_handler(on_signal);
}

void sigs_release(void)
{
    if (!l_enabled)
        return;
    l_enabled = 0;
    set_handler(SIG_IGN);
}

static int take(volatile sig_atomic_t *f)
{
    return __atomic_exchange_n(f, 0, __ATOMIC_RELAXED);
}

sigs_action sigs_poll(const sigs_state *st)
{
    if (!l_enabled)
        return SIGS_NONE;
    if (l_usr1)
    {
        if (st->quitting)
        {
            take(&l_usr1);
            take(&l_usr2);
            return SIGS_NONE;
        }
        if (st->menu_open)
            return SIGS_MENU;               /* stays pending: the save starts once the menu is closed */
        if (st->busy)
            return SIGS_NONE;
        take(&l_usr1);
        take(&l_usr2);
        return SIGS_SAVE_QUIT;
    }
    if (take(&l_usr2) && !st->quitting)
        return SIGS_MENU;
    return SIGS_NONE;
}

int sigs_autoload_claim(void)
{
    const char *v = getenv("M64P_AUTOLOAD");
    if (v == NULL || v[0] != '1')
        return 0;
    unsetenv("M64P_AUTOLOAD");
    return 1;
}
