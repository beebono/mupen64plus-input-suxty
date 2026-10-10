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

/* pure logic: the caller supplies the clock and shows the notice. The second press must be a new edge (not the combo
 * still held) within QUITARM_WINDOW_MS */

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
quitarm_result quitarm_press(quitarm_t *q, int confirm, uint32_t now_ms);

#endif /* QUITARM_H */
