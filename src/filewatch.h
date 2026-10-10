/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - filewatch.h
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

/* inotify on the directory (IN_CLOSE_WRITE | IN_MOVED_TO) because the core writes on a worker thread; polls
 * mtime/size without inotify */

#ifndef FILEWATCH_H
#define FILEWATCH_H

#include <limits.h>
#include <stdint.h>
#include <sys/stat.h>
#include <time.h>

#define FILEWATCH_SETTLE_MS 300u

typedef struct {
    int             fd;                  /* inotify descriptor, -1 = polling the file */
    int             wd;
    char            path[PATH_MAX];
    char            name[256];
    /* always taken: an inotify queue overflow falls back to it too */
    int             before_exists;
    struct timespec before_mtime;
    off_t           before_size;
    off_t           last_size;
    int             last_valid;
    uint32_t        last_change_ms;
} filewatch;

/* call BEFORE the write is requested */
int  filewatch_start(filewatch *w, const char *path, int force_polling);
int  filewatch_poll(filewatch *w);
void filewatch_stop(filewatch *w);
int  filewatch_uses_inotify(const filewatch *w);

#endif /* FILEWATCH_H */
