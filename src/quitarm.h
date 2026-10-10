/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - quitarm.h
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

/* "Confirm Quit": the quit hotkey must be pressed twice within a couple of seconds.  Pure logic (the caller supplies the clock and
 * shows the notice), so it can be unit-tested (tools/test_quitarm.c).
 *
 *   confirm off   every press quits.
 *   confirm on    the first press arms (the caller shows "press again to quit"), a second *press* (a new edge, not the combo
 *                 still being held) within QUITARM_WINDOW_MS quits; a press after the window arms again.
 */

#ifndef QUITARM_H
#define QUITARM_H

#include <stdint.h>

#define QUITARM_WINDOW_MS 2000u

typedef struct {
    int      armed;
    uint32_t armed_at;
} quitarm_t;

typedef enum { QUITARM_NONE, QUITARM_ARMED, QUITARM_QUIT } quitarm_result;

void           quitarm_reset(quitarm_t *q);
/* the quit combo was pressed (once per press) at `now_ms` */
quitarm_result quitarm_press(quitarm_t *q, int confirm, uint32_t now_ms);

#endif /* QUITARM_H */
