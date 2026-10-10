/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - foir_if.h
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

/* The FOIR Options page: foir's options, through foir's one-way interface (foir_suxty.h, exported by mupen64plus-rdp-foir as
 * FoirSuxtyGetInterface).  suxty never links foir and foir never calls suxty.
 *
 * FINDING FOIR.  ui-console loads plugins RTLD_LOCAL, so dlsym(RTLD_DEFAULT) can't see them, and the file name is
 * whatever the user configured.  fif_discover() therefore walks the loaded objects with dl_iterate_phdr(), takes a
 * non-loading reference to each (dlopen(path, RTLD_NOW | RTLD_NOLOAD)), looks for the symbol, and drops the extra
 * reference again.  It runs once at InitiateControllers (foir is loaded by then and has not opened the ROM yet) and again
 * when the menu opens while foir has not been found; there is no polling.  An interface with another ABI version counts
 * as absent: no Video page.
 *
 * WHERE THE VALUES LIVE.  [foir] <Key> = value in suxty.cfg (all games) or in the game's file; Key is the [Video-Foir]
 * parameter name.  bool: true/false, int: a number, enum: the label ("Auto"), or its index.  mupen64plus.cfg is only
 * written by the explicit "Save as default" button (foir's save_default).  At game start (before foir's RomOpen) every
 * stored value is handed to foir with set(), so live and next-launch options alike are in effect from the first frame.
 */

#ifndef FOIR_IF_H
#define FOIR_IF_H

#include "foir_suxty.h"

int  fif_discover(void);                                  /* 1 if the interface is available (idempotent, cheap when found) */
const struct foir_suxty_interface *fif_get(void);         /* NULL if foir is not the video plugin / too old */
void fif_release(void);                                   /* RomClosed / shutdown: forget the interface (never call it after) */

void fif_apply_config(void);                              /* stored values -> foir (call once the game's file is loaded) */

/* menu API (emulation thread) */
unsigned fif_count(void);
const struct foir_suxty_option *fif_option(unsigned i);
int  fif_shown(unsigned i);                               /* the value the menu shows (in the edit scope) */
int  fif_overridden(unsigned i);                          /* this game stores its own value */
int  fif_pending(unsigned i);                             /* shown value != what the running game uses (next launch) */
/* Stores v in the edit scope and puts the resulting effective value into foir.  FOIR_SUXTY_APPLIED / _NEXT_LAUNCH / error. */
int  fif_set(unsigned i, int v);
void fif_reset(unsigned i);                               /* drop the game's value ("Reset to global") and re-apply */
void fif_reapply_all(void);                               /* after "Reset all to global" */
int  fif_pin_game(void);                                  /* "Save Game Profile": the game gets its own copy of every value in force; returns how many were written */
int  fif_save_defaults(void);                             /* all shown values -> [Video-Foir] in mupen64plus.cfg; 0 ok */
void fif_value_text(unsigned i, int v, char *out, unsigned size);

#endif /* FOIR_IF_H */
