/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-ui-suxty - states.h
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

/* Save-state files.
 *
 * Numbered slots (0-9) are owned by the core: suxty selects the slot (M64CMD_STATE_SET_SLOT) and
 * asks for M64CMD_STATE_SAVE / M64CMD_STATE_LOAD, and the core picks the file.  To show the slot
 * carousel (existence + last-saved time) and to delete slots, suxty needs to know which file the
 * core uses, so states_slot_path() reproduces the core's naming exactly (see
 * mupen64plus-core/src/main/savestates.c: savestates_generate_path() and
 * main.c: get_save_filename()):
 *
 *     <SaveStatePath or ${UserDataPath}save/> / <name>.st<slot>
 *       name = ROM header name                          (Core[SaveFilenameFormat] = 0)
 *            = "<goodname, 32 chars>-<MD5, 8 chars>"    (format 1, ROM known to the ini)
 *            = "<header name>-<MD5, 8 chars>"           (format 1, unknown ROM)
 *       and a legacy "<goodname>.st<slot>" is used instead if it exists and is not empty.
 *
 * PROJECT64 STATES.  The core can also load (and save, by explicit path) Project64 states.  For a numbered slot its
 * M64CMD_STATE_LOAD (no path) tries, in this order, "<name>.st<N>" (gzip, Mupen64Plus), "<header name>.pj<N>.zip" (zip) and
 * "<header name>.pj<N>" (uncompressed) in the same directory, where "<header name>" is the ROM header name, always without
 * the MD5/goodname forms of the .st files (savestates.c savestates_load(): m64p first, then pj64 zip, then pj64).  A slot save
 * always writes the Mupen64Plus .st<N> file.  So that a slot that exists only as a Project64 state is not shown empty,
 * states_stat() looks at all three files and reports the NEWEST one as the slot's state (ties go to the .st file):
 *   - state_info.pj64 says the shown state is a Project64 one (the carousel tags the card "PJ64"),
 *   - loading it is then done by path (states_pj_path()), so that a .pj newer than an existing .st really wins,
 *   - saving writes the .st, which is then the newest (the .pj file is left alone), and Delete removes the shown file only.
 *
 * The dedicated auto-save slot is suxty's own: "<name>.auto.st" next to the numbered slots, saved
 * through a "<file>.tmp" and renamed on success, so that a failed or interrupted write never
 * destroys the previous auto-save.
 */

#ifndef STATES_H
#define STATES_H

#include <stddef.h>
#include <time.h>

#define STATES_NUM_SLOTS 10
#define STATES_AUTO_SLOT 10          /* pseudo slot index for the auto-save */
#define STATES_NUM_ENTRIES 11

typedef struct {
    int       exists;       /* the slot has a state (the newest of its files) */
    time_t    mtime;        /* ... and this is its time stamp */
    long long size;
    int       pj64;         /* the state above is a Project64 one (.pj / .pj.zip) */
    int       has_st;       /* a Mupen64Plus-format file exists as well/instead */
    int       has_pj;       /* a Project64 file exists as well/instead */
} state_info;

/* Call after the ROM is open and the plugins are attached (the core config section must exist). */
void states_init(const char *header_name, const char *goodname, const char *md5);

int  states_slot_path(int slot, char *out, size_t size);     /* slot 0-10; 0 on success */
void states_stat(int slot, state_info *info);
/* The newest Project64 state file of a numbered slot ("<header>.pj<N>.zip" or "<header>.pj<N>"); 0 if there is one. */
int  states_pj_path(int slot, char *out, size_t size);
/* Where a Project64 state of this slot is written by the debug hook (uncompressed or zip). */
int  states_pj_new_path(int slot, int zip, char *out, size_t size);
int  states_delete(int slot);                                /* 0 on success (or already absent) */

/* Checks that a save can be written to the state directory.  The core leaves its state-file lock
 * held when a write fails, which hangs the next load, so we refuse to start a write that is
 * likely to fail.  Returns 0 if OK, else fills `why`. */
int  states_check_writable(char *why, size_t size);

/* "2026-10-08" / "14:23:07" */
void states_format_time(time_t t, char *date, size_t dsize, char *clock, size_t csize);

#endif /* STATES_H */
