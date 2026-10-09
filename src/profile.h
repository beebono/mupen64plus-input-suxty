/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-ui-suxty - profile.h
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

/* Per-game core / plugin parameters (M4): the "cfg:" part of the game layer (cfg.h).
 *
 * WHAT IS STORED WHERE
 *   mupen64plus.cfg    the GLOBAL values of every core/plugin parameter.  Nothing per-game is ever written to it.
 *   games/<md5>.cfg    [cfg:Video-Foir] Threads = -1   the values this game uses instead.
 *
 * WHEN THEY TAKE EFFECT
 *   profile_apply() puts the game's values into the core's *in-memory* config (what ConfigGetParameter() returns), after
 *   the plugins have created their defaults (PluginStartup) and before they read them: the plugins read their sections
 *   in InitiateGFX / RomOpen / InitiateAudio (when the emulator starts) or, for the input plugin, in CoreAttachPlugin().
 *   That is the same point at which ui-console applies --set, and the command line still wins (it is applied after).
 *   A plugin that reads its parameters once cannot see a later change, so editing a plugin parameter in the menu only
 *   changes the stores; profile_pending() counts what differs from what the running plugin was started with, and the
 *   menu offers to restart the game (session.c, "restart and resume") to apply it.
 *
 * HOW THE GLOBAL FILE STAYS GLOBAL
 *   The core writes all of its in-memory sections when asked to save (ConfigSaveFile / ConfigSaveSection).  Every entry
 *   profile_apply() changed remembers its global value; profile_save_file() / profile_save_section() put the globals
 *   back, save, and put the game values back, and the front-end calls only those (never ConfigSaveFile directly).
 *   Editing a *global* value for a parameter the game overrides updates the remembered global, not the live value.
 */

#ifndef PROFILE_H
#define PROFILE_H

#include <stddef.h>

#include "m64p_types.h"

#define PROFILE_MAX_PARAMS 192

typedef struct {
    char       name[48];
    m64p_type  type;
} profile_param;

void profile_set_save_core(int save);               /* --nosaveoptions: never write mupen64plus.cfg */
void profile_apply(void);                           /* game layer -> core in-memory config (see above) */
void profile_note_external_changes(void);           /* the command line changed an applied value: that one is not ours */
void profile_snapshot(void);                        /* just before the emulator starts: what the plugins will read */
void profile_save_file(void);                       /* ConfigSaveFile() with the global values in place */
void profile_save_section(const char *section);     /* ConfigSaveSection() likewise */
void profile_shutdown(void);

/* The plugin file name chosen for this game ("video", "audio", "input", "rsp") in [plugins], or NULL. */
const char *profile_plugin_choice(const char *kind);

/* The config section of the loaded plugin of this type ("Video-Foir", "Audio-SDL"), found from the plugin's file name; 0 if
 * there is none. */
int  profile_plugin_section(m64p_plugin_type type, char *out, size_t size);
/* Lists the parameters of a section (not "version"); returns how many. */
int  profile_list_params(const char *section, profile_param *out, int max);
const char *profile_param_help(const char *section, const char *name);

/* Values as text ("True"/"False", integers, "%g" floats, strings).  Each returns 1 if there is such a value.
 *   global: what mupen64plus.cfg has/will have     game: the game layer's    scoped: what the menu shows in cfg's scope */
int  profile_get_global(const char *section, const char *name, char *out, size_t size);
int  profile_get_game(const char *section, const char *name, char *out, size_t size);
int  profile_get_scoped(const char *section, const char *name, char *out, size_t size);
int  profile_overridden(const char *section, const char *name);
/* Sets in the current cfg scope (game layer, or the global config and mupen64plus.cfg).  Nothing is applied to a running
 * plugin; see profile_pending(). */
void profile_set(const char *section, const char *name, const char *value);
void profile_reset(const char *section, const char *name);   /* drops the game entry */
void profile_reset_all(void);                                /* drops every [cfg:...] game entry (they count for profile_pending()) */
int  profile_pending(void);                                  /* edited values that need a restart to reach the plugins */
void profile_flush(void);                                    /* write the global core config sections edited since the last flush */
/* Number of "cfg:" entries / of everything the game layer holds */
int  profile_entry_count(void);
int  profile_normalize(m64p_type type, const char *in, char *out, size_t size);

/* The core's own audio volume, applied to the game at start and when it changes (suxty option [audio] volume, 0-100;
 * unset = whatever the audio plugin starts with). */
void profile_apply_volume(void);
int  profile_volume_now(void);                       /* 0-100, -1 if the core can't tell */

#endif /* PROFILE_H */
