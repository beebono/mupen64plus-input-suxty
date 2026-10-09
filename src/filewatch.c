/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-ui-suxty - filewatch.c
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

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#include "filewatch.h"

static uint32_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t) ((uint64_t) t.tv_sec * 1000u + (uint64_t) t.tv_nsec / 1000000u);
}

static void take_before(filewatch *w)
{
    struct stat st;
    memset(&w->before_mtime, 0, sizeof(w->before_mtime));
    w->before_size = 0;
    w->before_exists = stat(w->path, &st) == 0 && S_ISREG(st.st_mode);
    if (w->before_exists)
    {
        w->before_mtime = st.st_mtim;
        w->before_size = st.st_size;
    }
    w->last_valid = 0;
    w->last_change_ms = 0;
}

int filewatch_start(filewatch *w, const char *path, int force_polling)
{
    const char *slash;
    char dir[PATH_MAX];

    memset(w, 0, sizeof(*w));
    w->fd = -1;
    w->wd = -1;
    if (path == NULL || strlen(path) >= sizeof(w->path))
        return -1;
    snprintf(w->path, sizeof(w->path), "%s", path);
    slash = strrchr(path, '/');
    if (slash == NULL)
    {
        snprintf(dir, sizeof(dir), ".");
        snprintf(w->name, sizeof(w->name), "%s", path);
    }
    else
    {
        snprintf(dir, sizeof(dir), "%.*s", (int) (slash - path > 0 ? slash - path : 1), path);
        snprintf(w->name, sizeof(w->name), "%s", slash + 1);
    }
    if (w->name[0] == 0)
        return -1;
    take_before(w);
    if (!force_polling)
    {
        int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (fd >= 0)
        {
            int wd = inotify_add_watch(fd, dir, IN_CLOSE_WRITE | IN_MOVED_TO);
            if (wd >= 0)
            {
                w->fd = fd;
                w->wd = wd;
            }
            else
                close(fd);
        }
    }
    return 0;
}

int filewatch_uses_inotify(const filewatch *w)
{
    return w->fd >= 0;
}

/* the file differs from how it was at start */
static int changed(const filewatch *w, struct stat *st)
{
    if (stat(w->path, st) != 0 || !S_ISREG(st->st_mode))
        return 0;
    if (!w->before_exists)
        return 1;
    return st->st_size != w->before_size || st->st_mtim.tv_sec != w->before_mtime.tv_sec ||
           st->st_mtim.tv_nsec != w->before_mtime.tv_nsec;
}

/* polling: changed, and the size has stayed put for the settle time */
static int poll_stat(filewatch *w)
{
    struct stat st;
    uint32_t now = now_ms();
    if (!changed(w, &st))
        return 0;
    if (!w->last_valid || st.st_size != w->last_size)
    {
        w->last_valid = 1;
        w->last_size = st.st_size;
        w->last_change_ms = now;
        return 0;
    }
    return now - w->last_change_ms >= FILEWATCH_SETTLE_MS;
}

int filewatch_poll(filewatch *w)
{
    int hit = 0, overflow = 0;

    if (w->fd < 0)
        return poll_stat(w);
    for (;;)
    {
        char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
        ssize_t n = read(w->fd, buf, sizeof(buf));
        char *p;
        if (n <= 0)
            break;                                              /* EAGAIN: drained */
        for (p = buf; p < buf + n; )
        {
            const struct inotify_event *ev = (const struct inotify_event *) p;
            if (ev->mask & IN_Q_OVERFLOW)
                overflow = 1;
            else if ((ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO)) && ev->len > 0 && strcmp(ev->name, w->name) == 0)
                hit = 1;
            p += sizeof(*ev) + ev->len;
        }
    }
    if (!hit && overflow)
    {
        struct stat st;
        hit = changed(w, &st);                                  /* events were lost: look at the file itself */
    }
    return hit;
}

void filewatch_stop(filewatch *w)
{
    if (w->fd >= 0)
        close(w->fd);
    w->fd = -1;
    w->wd = -1;
}
