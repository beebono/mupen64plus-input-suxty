/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - quitarm.c
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

#include "quitarm.h"

void quitarm_reset(quitarm_t *q)
{
    q->armed = 0;
    q->armed_at = 0;
}

quitarm_result quitarm_press(quitarm_t *q, int confirm, uint32_t now_ms)
{
    if (!confirm)
    {
        quitarm_reset(q);
        return QUITARM_QUIT;
    }
    if (q->armed && (uint32_t) (now_ms - q->armed_at) <= QUITARM_WINDOW_MS)
    {
        quitarm_reset(q);
        return QUITARM_QUIT;
    }
    q->armed = 1;
    q->armed_at = now_ms;
    return QUITARM_ARMED;
}
