/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - inifile.h
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


/* A tiny INI reader/writer for suxty.cfg and the per-game override files.
 *
 * Format:  [section] headers, "key = value" lines, '#' or ';' comments.
 * Values are stored as strings; comments are not preserved on rewrite
 * (the file starts with a generated header instead).
 */

#ifndef INIFILE_H
#define INIFILE_H

typedef struct ini ini_t;

ini_t      *ini_new(void);
void        ini_free(ini_t *ini);
int         ini_load(ini_t *ini, const char *path);          /* 0 ok, -1 can't open */
int         ini_save(const ini_t *ini, const char *path, const char *header_comment);  /* atomic; 0 ok */
int         ini_dirty(const ini_t *ini);
void        ini_clean(ini_t *ini);                           /* forget unsaved changes' dirty flag (after a manual delete) */

const char *ini_get(const ini_t *ini, const char *section, const char *key, const char *def);
int         ini_get_int(const ini_t *ini, const char *section, const char *key, int def);
int         ini_get_bool(const ini_t *ini, const char *section, const char *key, int def);
int         ini_has(const ini_t *ini, const char *section, const char *key);

void        ini_set(ini_t *ini, const char *section, const char *key, const char *value);
void        ini_set_int(ini_t *ini, const char *section, const char *key, int value);
void        ini_set_bool(ini_t *ini, const char *section, const char *key, int value);
/* removal and enumeration (the per-game layer deletes its entries to "reset to global") */
int         ini_remove(ini_t *ini, const char *section, const char *key);           /* 1 if the entry existed */
int         ini_remove_section(ini_t *ini, const char *section);                    /* number of entries removed */
int         ini_has_section(const ini_t *ini, const char *section);
int         ini_count(const ini_t *ini);                                            /* number of entries */
int         ini_count_prefix(const ini_t *ini, const char *section_prefix);         /* entries in sections starting with it */
/* i-th entry in file order; 1 on success */
int         ini_entry(const ini_t *ini, int i, const char **section, const char **key, const char **value);
/* set only if absent (used to fill in defaults without touching user edits) */
void        ini_default(ini_t *ini, const char *section, const char *key, const char *value);

#endif /* INIFILE_H */
