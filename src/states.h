/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - states.h
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

/* file names reproduce the core's (savestates.c savestates_generate_path(), main.c get_save_filename()) so the
 * carousel and Delete find them.
 * A slot's state is the NEWEST of .st / .pj.zip / .pj (ties go to .st); a .pj is loaded by path, a save always writes
 * the .st.
 * "<name>.auto.st" is written only by Quit and loaded only by the launcher (--savestate). */

#ifndef STATES_H
#define STATES_H

#include <stddef.h>
#include <time.h>

#define STATES_NUM_SLOTS 10
#define STATES_AUTO_SLOT 10          /* pseudo slot index for the auto-save */
#define STATES_NUM_ENTRIES 11

typedef struct {
    int       exists;
    time_t    mtime;
    long long size;
    int       pj64;
    int       has_st;
    int       has_pj;
} state_info;

/* call after the ROM is open and the plugins are attached (the core config section must exist) */
void states_init(const char *header_name, const char *goodname, const char *md5);

int  states_slot_path(int slot, char *out, size_t size);
void states_stat(int slot, state_info *info);
int  states_pj_path(int slot, char *out, size_t size);
int  states_pj_new_path(int slot, int zip, char *out, size_t size);
int  states_delete(int slot);

/* the core leaves its state-file lock held when a write fails, which hangs the next load: refuse a write that is
 * likely to fail */
int  states_check_writable(char *why, size_t size);

void states_format_time(time_t t, char *date, size_t dsize, char *clock, size_t csize);

#endif /* STATES_H */
