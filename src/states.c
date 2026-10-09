/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - states.c
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
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "autosave.h"
#include "core_interface.h"
#include "main.h"
#include "m64p_types.h"
#include "osal_files.h"
#include "states.h"

/* a raw state is ~16.8 MB; gzip makes it smaller but incompressible data is possible */
#define MIN_FREE_BYTES (24LL * 1024 * 1024)

static char l_header[64];
static char l_goodname[256];
static char l_md5[40];
static int  l_ready;

void states_init(const char *header_name, const char *goodname, const char *md5)
{
    size_t n;
    snprintf(l_header, sizeof(l_header), "%s", header_name ? header_name : "");
    /* ROM_PARAMS.headername: the 20-byte header name, with surrounding whitespace trimmed */
    n = strlen(l_header);
    while (n > 0 && (l_header[n - 1] == ' ' || (unsigned char) l_header[n - 1] < 32))
        l_header[--n] = 0;
    {
        char *s = l_header;
        while (*s == ' ' || (*s != 0 && (unsigned char) *s < 32))
            s++;
        if (s != l_header)
            memmove(l_header, s, strlen(s) + 1);
    }
    snprintf(l_goodname, sizeof(l_goodname), "%s", goodname ? goodname : "");
    snprintf(l_md5, sizeof(l_md5), "%s", md5 ? md5 : "");
    l_ready = 1;
}

static void sanitize(char *s)
{
    for (; *s; s++)
        if (strchr(":<>\"/\\|?*", *s) != NULL)
            *s = '_';
}

/* same as the core's get_save_filename() */
static void save_filename(char *out, size_t size)
{
    m64p_handle core = NULL;
    int format = 1;

    if ((*ConfigOpenSection)("Core", &core) == M64ERR_SUCCESS)
        format = (*ConfigGetParamInt)(core, "SaveFilenameFormat");
    if (format == 0)
        snprintf(out, size, "%s", l_header);
    else if (strstr(l_goodname, "(unknown rom)") == NULL)
        snprintf(out, size, "%.32s-%.8s", l_goodname, l_md5);
    else if (l_header[0] != 0)
        snprintf(out, size, "%s-%.8s", l_header, l_md5);
    else
        snprintf(out, size, "unknown-%.8s", l_md5);
    sanitize(out);
}

/* same as the core's get_savepathdefault(ConfigGetParamString("SaveStatePath")); always ends in '/' */
static void state_dir(char *out, size_t size)
{
    m64p_handle core = NULL;
    const char *cfgpath = NULL;

    if ((*ConfigOpenSection)("Core", &core) == M64ERR_SUCCESS)
        cfgpath = (*ConfigGetParamString)(core, "SaveStatePath");
    if (cfgpath == NULL || cfgpath[0] == 0)
    {
        const char *ud = (*ConfigGetUserDataPath)();
        snprintf(out, size, "%s%ssave/", ud ? ud : "", (ud && ud[0] && ud[strlen(ud) - 1] != '/') ? "/" : "");
    }
    else
    {
        size_t n = strlen(cfgpath);
        snprintf(out, size, "%s%s", cfgpath, (n > 0 && cfgpath[n - 1] == '/') ? "" : "/");
    }
}

static int file_size(const char *path, long long *size, time_t *mtime)
{
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
        return 0;
    if (size) *size = (long long) st.st_size;
    if (mtime) *mtime = st.st_mtime;
    return 1;
}

int states_slot_path(int slot, char *out, size_t size)
{
    char dir[PATH_MAX], name[300];
    long long sz = 0;
    int n;

    if (!l_ready || slot < 0 || slot >= STATES_NUM_ENTRIES)
        return -1;
    state_dir(dir, sizeof(dir));
    if (slot == STATES_AUTO_SLOT)
    {
        save_filename(name, sizeof(name));
        n = snprintf(out, size, "%s%s.auto.st", dir, name);
        return (n < 0 || (size_t) n >= size) ? -1 : 0;
    }
    /* legacy name first, like the core */
    n = snprintf(out, size, "%s%s.st%d", dir, l_goodname, slot);
    if (n > 0 && (size_t) n < size && file_size(out, &sz, NULL) && sz > 0)
        return 0;
    save_filename(name, sizeof(name));
    n = snprintf(out, size, "%s%s.st%d", dir, name, slot);
    return (n < 0 || (size_t) n >= size) ? -1 : 0;
}

/* the core's PJ64 slot names (savestates.c savestates_generate_path()): the header name, never sanitised */
int states_pj_new_path(int slot, int zip, char *out, size_t size)
{
    char dir[PATH_MAX];
    int n;
    if (!l_ready || slot < 0 || slot >= STATES_NUM_SLOTS)
        return -1;
    state_dir(dir, sizeof(dir));
    n = snprintf(out, size, "%s%s.pj%d%s", dir, l_header, slot, zip ? ".zip" : "");
    return (n < 0 || (size_t) n >= size) ? -1 : 0;
}

/* newest of the two PJ64 spellings; returns 1 and fills what it found */
static int pj_find(int slot, char *path, size_t size, long long *sz, time_t *mt)
{
    int best = 0, zip;
    for (zip = 1; zip >= 0; zip--)
    {
        char p[PATH_MAX];
        long long s = 0;
        time_t t = 0;
        if (states_pj_new_path(slot, zip, p, sizeof(p)) != 0 || !file_size(p, &s, &t) || s <= 0)
            continue;
        if (!best || t > *mt)
        {
            best = 1;
            *sz = s;
            *mt = t;
            if (path != NULL)
                snprintf(path, size, "%s", p);
        }
    }
    return best;
}

int states_pj_path(int slot, char *out, size_t size)
{
    long long sz = 0;
    time_t mt = 0;
    return pj_find(slot, out, size, &sz, &mt) ? 0 : -1;
}

void states_stat(int slot, state_info *info)
{
    char path[PATH_MAX];
    long long sz = 0;
    time_t mt = 0;

    memset(info, 0, sizeof(*info));
    if (states_slot_path(slot, path, sizeof(path)) != 0)
        return;
    if (file_size(path, &sz, &mt) && sz > 0)
    {
        info->exists = 1;
        info->has_st = 1;
        info->size = sz;
        info->mtime = mt;
    }
    if (slot < STATES_NUM_SLOTS)
    {
        long long psz = 0;
        time_t pmt = 0;
        if (pj_find(slot, NULL, 0, &psz, &pmt))
        {
            info->has_pj = 1;
            if (!info->has_st || pmt > info->mtime)
            {
                info->exists = 1;
                info->pj64 = 1;
                info->size = psz;
                info->mtime = pmt;
            }
        }
    }
}

int states_delete(int slot)
{
    char path[PATH_MAX];
    state_info si;

    if (slot == STATES_AUTO_SLOT)
    {
        /* the auto-save and every backup of it (a deleted auto-save must not come back at the next start) */
        if (states_slot_path(slot, path, sizeof(path)) != 0)
            return -1;
        if (autosave_remove_all(path) != 0)
        {
            DebugMessage(M64MSG_WARNING, "couldn't delete '%s' and its backups: %s", path, strerror(errno));
            return -1;
        }
        DebugMessage(M64MSG_INFO, "deleted state file '%s' and its backups", path);
        return 0;
    }
    states_stat(slot, &si);
    if (si.pj64 ? states_pj_path(slot, path, sizeof(path)) != 0 : states_slot_path(slot, path, sizeof(path)) != 0)
        return si.exists ? -1 : 0;
    if (unlink(path) != 0 && errno != ENOENT)
    {
        DebugMessage(M64MSG_WARNING, "couldn't delete '%s': %s", path, strerror(errno));
        return -1;
    }
    DebugMessage(M64MSG_INFO, "deleted state file '%s'", path);
    return 0;
}

int states_check_writable(char *why, size_t size)
{
    char dir[PATH_MAX];
    struct statvfs vfs;

    state_dir(dir, sizeof(dir));
    if (osal_mkdir_p(dir) != 0 && !osal_file_exists(dir))
    {
        snprintf(why, size, "no state directory");
        return -1;
    }
    if (access(dir, W_OK) != 0)
    {
        snprintf(why, size, "state directory not writable");
        return -1;
    }
    if (statvfs(dir, &vfs) == 0 && (long long) vfs.f_bavail * (long long) vfs.f_frsize < MIN_FREE_BYTES)
    {
        snprintf(why, size, "disk full");
        return -1;
    }
    return 0;
}

void states_format_time(time_t t, char *date, size_t dsize, char *clock, size_t csize)
{
    struct tm tmv;
    if (localtime_r(&t, &tmv) == NULL)
    {
        snprintf(date, dsize, "?");
        snprintf(clock, csize, "?");
        return;
    }
    strftime(date, dsize, "%Y-%m-%d", &tmv);
    strftime(clock, csize, "%H:%M:%S", &tmv);
}
