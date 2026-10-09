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

/* Safety net for the auto-save file "<name>.auto.st" (written by Quit with [states] autosave_on_quit on).
 *
 * The core writes a state in place (it truncates the file and rewrites it), so a failed or interrupted write loses the
 * previous auto-save.  suxty therefore renames the existing file to a backup ("<name>.auto.st.prev", then ".prev2",
 * ".prev3" if backups pile up) just before asking the core to write the new one.  suxty never loads the auto file, so it
 * cannot see whether the launcher's load worked.  Instead, at the next game start:
 *
 *   - "<name>.auto.st" validates (gzip stream decompresses completely with a good CRC, Mupen64Plus savestate magic, a
 *     version of the format family the core reads, and the exact size the core writes for that version) -> the new file
 *     is good, the backups are deleted;
 *   - it does not validate (or is missing) and a backup does -> the newest valid backup is renamed back to "<name>.auto.st".
 *
 * Only this game's files are touched.  This module needs nothing from the core (no logging either), so it can be unit
 * tested on its own.  The gzip/deflate reader is self-contained: suxty depends on SDL2 only.
 */

#ifndef AUTOSAVE_H
#define AUTOSAVE_H

#include <limits.h>
#include <stddef.h>

#define AUTOSAVE_MAX_BACKUPS 3

/* savestates.c: header 44 bytes (magic 8, version 4, ROM MD5 32) + 16788244 bytes of state, then the event queue
 * (1024 bytes, version >= 1.0), the using_tlb flags (4, >= 1.1) and the extra state (4096, >= 1.2). */
#define AUTOSAVE_SIZE_V10_MIN  16788288LL
#define AUTOSAVE_SIZE_V10_MAX  (AUTOSAVE_SIZE_V10_MIN + 1024)
#define AUTOSAVE_SIZE_V11      (AUTOSAVE_SIZE_V10_MAX + 4)
#define AUTOSAVE_SIZE_V12      (AUTOSAVE_SIZE_V11 + 4096)           /* 16793412: what the core writes today (1.9) */

/* 0 = a valid Mupen64Plus state; else -1 and `why` says what is wrong ("truncated", "bad magic", "bad crc", ...). */
int autosave_validate(const char *path, char *why, size_t why_size);

/* "<path>.prev" for i = 0, ".prev2" for i = 1, ... */
void autosave_backup_path(const char *path, int i, char *out, size_t size);

/* Before the core writes `path`: if a regular file is there, rename it to a free backup name (when all are taken, the
 * oldest backup is dropped).  Returns 1 if the file was moved aside, 0 if there was nothing to move, -1 on error (errno). */
int autosave_rotate(const char *path);

/* The write did not happen: if `path` is absent and there is a backup, put the newest one back.  Returns 1 if it did. */
int autosave_unrotate(const char *path);

/* Deletes `path` and all its backups.  Returns 0, or -1 if one could not be removed. */
int autosave_remove_all(const char *path);

typedef enum {
    AUTOSAVE_ABSENT = 0,     /* neither the file nor a backup: nothing to do (or `path` is not a regular file: untouched) */
    AUTOSAVE_GOOD,           /* the file is valid; `removed` backups were deleted */
    AUTOSAVE_RESTORED,       /* the file was bad or missing; the backup `from` is back in its place (`was_missing` says which) */
    AUTOSAVE_BAD             /* the file is bad or missing and no backup validates: everything is left as it is */
} autosave_status;

typedef struct {
    autosave_status status;
    int             removed;
    int             was_missing;
    int             backups;                 /* how many backup files there were */
    char            why[64];                 /* what was wrong with the file (RESTORED / BAD) */
    char            from[PATH_MAX + 16];              /* the backup that was restored */
} autosave_report;

/* The game-start check described above. */
void autosave_reconcile(const char *path, autosave_report *rep);

/* `--savestate <file>` / `--savestate=<file>` in a NUL-separated command line (/proc/self/cmdline).  1 if found. */
int autosave_cmdline_arg(const char *buf, size_t len, const char *opt, char *out, size_t out_size);
/* The same for this process's own command line; 0 if it can't be read. */
int autosave_cmdline_savestate(char *out, size_t out_size);

#endif /* AUTOSAVE_H */
