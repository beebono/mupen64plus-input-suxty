/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - inifile.c
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


#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "inifile.h"

typedef struct {
    char *section;
    char *key;
    char *value;
} entry_t;

struct ini {
    entry_t *e;
    int      n, cap;
    int      dirty;
};

ini_t *ini_new(void)
{
    return (ini_t *) calloc(1, sizeof(ini_t));
}

void ini_free(ini_t *ini)
{
    int i;
    if (ini == NULL)
        return;
    for (i = 0; i < ini->n; i++)
    {
        free(ini->e[i].section);
        free(ini->e[i].key);
        free(ini->e[i].value);
    }
    free(ini->e);
    free(ini);
}

int ini_dirty(const ini_t *ini)
{
    return ini->dirty;
}

void ini_clean(ini_t *ini)
{
    ini->dirty = 0;
}

static entry_t *find(const ini_t *ini, const char *section, const char *key)
{
    int i;
    for (i = 0; i < ini->n; i++)
        if (strcmp(ini->e[i].section, section) == 0 && strcmp(ini->e[i].key, key) == 0)
            return &ini->e[i];
    return NULL;
}

static char *trim(char *s)
{
    char *end;
    while (isspace((unsigned char) *s))
        s++;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char) end[-1]))
        *--end = 0;
    return s;
}

static void put(ini_t *ini, const char *section, const char *key, const char *value, int mark_dirty)
{
    entry_t *e = find(ini, section, key);
    if (e != NULL)
    {
        if (strcmp(e->value, value) != 0)
        {
            free(e->value);
            e->value = strdup(value);
            if (mark_dirty) ini->dirty = 1;
        }
        return;
    }
    if (ini->n == ini->cap)
    {
        int ncap = ini->cap ? ini->cap * 2 : 32;
        entry_t *ne = (entry_t *) realloc(ini->e, (size_t) ncap * sizeof(entry_t));
        if (ne == NULL)
            return;
        ini->e = ne;
        ini->cap = ncap;
    }
    ini->e[ini->n].section = strdup(section);
    ini->e[ini->n].key = strdup(key);
    ini->e[ini->n].value = strdup(value);
    ini->n++;
    if (mark_dirty) ini->dirty = 1;
}

int ini_load(ini_t *ini, const char *path)
{
    FILE *f = fopen(path, "r");
    char line[1024];
    char section[128] = "";

    if (f == NULL)
        return -1;
    while (fgets(line, sizeof(line), f) != NULL)
    {
        char *s = trim(line);
        if (*s == 0 || *s == '#' || *s == ';')
            continue;
        if (*s == '[')
        {
            char *close = strchr(s, ']');
            if (close != NULL)
            {
                *close = 0;
                snprintf(section, sizeof(section), "%s", trim(s + 1));
            }
            continue;
        }
        {
            char *eq = strchr(s, '=');
            char *key, *val;
            size_t vl;
            if (eq == NULL)
                continue;
            *eq = 0;
            key = trim(s);
            val = trim(eq + 1);
            vl = strlen(val);
            if (vl >= 2 && val[0] == '"' && val[vl - 1] == '"')
            {
                val[vl - 1] = 0;
                val++;
            }
            if (*key)
                put(ini, section, key, val, 0);
        }
    }
    fclose(f);
    return 0;
}

int ini_save(const ini_t *ini, const char *path, const char *header_comment)
{
    char tmp[4096];
    FILE *f;
    int i, j;
    int ok = 1;

    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    f = fopen(tmp, "w");
    if (f == NULL)
        return -1;
    if (header_comment != NULL)
        fprintf(f, "%s\n", header_comment);
    /* group by section, in order of first appearance */
    for (i = 0; i < ini->n; i++)
    {
        int seen = 0;
        for (j = 0; j < i; j++)
            if (strcmp(ini->e[j].section, ini->e[i].section) == 0) { seen = 1; break; }
        if (seen)
            continue;
        fprintf(f, "[%s]\n", ini->e[i].section);
        for (j = i; j < ini->n; j++)
            if (strcmp(ini->e[j].section, ini->e[i].section) == 0)
                fprintf(f, "%s = %s\n", ini->e[j].key, ini->e[j].value);
        fprintf(f, "\n");
    }
    if (fflush(f) != 0 || ferror(f))
        ok = 0;
    if (fclose(f) != 0)
        ok = 0;
    if (!ok || rename(tmp, path) != 0)
    {
        remove(tmp);
        return -1;
    }
    ((ini_t *) ini)->dirty = 0;
    return 0;
}

const char *ini_get(const ini_t *ini, const char *section, const char *key, const char *def)
{
    entry_t *e = find(ini, section, key);
    return e ? e->value : def;
}

int ini_has(const ini_t *ini, const char *section, const char *key)
{
    return find(ini, section, key) != NULL;
}

int ini_get_int(const ini_t *ini, const char *section, const char *key, int def)
{
    const char *v = ini_get(ini, section, key, NULL);
    char *end;
    long n;
    if (v == NULL || *v == 0)
        return def;
    n = strtol(v, &end, 10);
    return (*end == 0) ? (int) n : def;
}

int ini_get_bool(const ini_t *ini, const char *section, const char *key, int def)
{
    const char *v = ini_get(ini, section, key, NULL);
    if (v == NULL)
        return def;
    if (strcasecmp(v, "true") == 0 || strcasecmp(v, "yes") == 0 || strcasecmp(v, "on") == 0 || strcmp(v, "1") == 0)
        return 1;
    if (strcasecmp(v, "false") == 0 || strcasecmp(v, "no") == 0 || strcasecmp(v, "off") == 0 || strcmp(v, "0") == 0)
        return 0;
    return def;
}

void ini_set(ini_t *ini, const char *section, const char *key, const char *value)
{
    put(ini, section, key, value, 1);
}

int ini_remove(ini_t *ini, const char *section, const char *key)
{
    entry_t *e = find(ini, section, key);
    int i;
    if (e == NULL)
        return 0;
    i = (int) (e - ini->e);
    free(e->section);
    free(e->key);
    free(e->value);
    memmove(&ini->e[i], &ini->e[i + 1], (size_t) (ini->n - i - 1) * sizeof(entry_t));
    ini->n--;
    ini->dirty = 1;
    return 1;
}

int ini_remove_section(ini_t *ini, const char *section)
{
    int i, removed = 0;
    for (i = ini->n - 1; i >= 0; i--)
        if (strcmp(ini->e[i].section, section) == 0)
            removed += ini_remove(ini, section, ini->e[i].key);
    return removed;
}

int ini_has_section(const ini_t *ini, const char *section)
{
    int i;
    for (i = 0; i < ini->n; i++)
        if (strcmp(ini->e[i].section, section) == 0)
            return 1;
    return 0;
}

int ini_count(const ini_t *ini)
{
    return ini->n;
}

int ini_count_prefix(const ini_t *ini, const char *prefix)
{
    int i, n = 0;
    size_t len = strlen(prefix);
    for (i = 0; i < ini->n; i++)
        if (strncmp(ini->e[i].section, prefix, len) == 0)
            n++;
    return n;
}

int ini_entry(const ini_t *ini, int i, const char **section, const char **key, const char **value)
{
    if (i < 0 || i >= ini->n)
        return 0;
    if (section) *section = ini->e[i].section;
    if (key) *key = ini->e[i].key;
    if (value) *value = ini->e[i].value;
    return 1;
}

void ini_set_int(ini_t *ini, const char *section, const char *key, int value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    ini_set(ini, section, key, buf);
}

void ini_set_bool(ini_t *ini, const char *section, const char *key, int value)
{
    ini_set(ini, section, key, value ? "true" : "false");
}

void ini_default(ini_t *ini, const char *section, const char *key, const char *value)
{
    if (find(ini, section, key) == NULL)
        put(ini, section, key, value, 1);
}
