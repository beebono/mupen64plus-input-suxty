/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - autosave.h
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

/* core rewrites the state in place, so a failed write loses the old one: rotate moves it aside first */

#ifndef AUTOSAVE_H
#define AUTOSAVE_H

#include <limits.h>
#include <stddef.h>

#define AUTOSAVE_MAX_BACKUPS 3

/* 44-byte header + 16788244 state, then queue 1024 (v1.0), using_tlb 4 (v1.1), extra 4096 (v1.2) */
#define AUTOSAVE_SIZE_V10_MIN  16788288LL
#define AUTOSAVE_SIZE_V10_MAX  (AUTOSAVE_SIZE_V10_MIN + 1024)
#define AUTOSAVE_SIZE_V11      (AUTOSAVE_SIZE_V10_MAX + 4)
#define AUTOSAVE_SIZE_V12      (AUTOSAVE_SIZE_V11 + 4096)

int autosave_validate(const char *path, char *why, size_t why_size);

/* i = 0 -> ".prev", 1 -> ".prev2", ... */
void autosave_backup_path(const char *path, int i, char *out, size_t size);

/* 1 moved aside, 0 nothing to move, -1 error (errno) */
int autosave_rotate(const char *path);

int autosave_unrotate(const char *path);

int autosave_remove_all(const char *path);

typedef enum {
    AUTOSAVE_ABSENT = 0,
    AUTOSAVE_GOOD,
    AUTOSAVE_RESTORED,
    AUTOSAVE_BAD
} autosave_status;

typedef struct {
    autosave_status status;
    int             removed;
    int             was_missing;
    int             backups;
    char            why[64];
    char            from[PATH_MAX + 16];
} autosave_report;

void autosave_reconcile(const char *path, autosave_report *rep);

/* /proc/self/cmdline is NUL-separated */
int autosave_cmdline_arg(const char *buf, size_t len, const char *opt, char *out, size_t out_size);
int autosave_cmdline_savestate(char *out, size_t out_size);

#endif /* AUTOSAVE_H */
