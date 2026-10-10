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

/* one-way interface (foir_suxty.h): suxty never links foir and foir never calls suxty
 * ui-console loads plugins RTLD_LOCAL, so foir is found by walking dl_iterate_phdr() with dlopen(RTLD_NOLOAD)
 * [foir] <Key> lives in suxty.cfg or the game file; mupen64plus.cfg is written only by save_default */

#ifndef FOIR_IF_H
#define FOIR_IF_H

#include "foir_suxty.h"

int  fif_discover(void);
const struct foir_suxty_interface *fif_get(void);
void fif_release(void);

void fif_apply_config(void);

/* emulation thread only */
unsigned fif_count(void);
const struct foir_suxty_option *fif_option(unsigned i);
int  fif_shown(unsigned i);
int  fif_overridden(unsigned i);
int  fif_pending(unsigned i);
int  fif_set(unsigned i, int v);
void fif_reset(unsigned i);
void fif_reapply_all(void);
int  fif_pin_game(void);
int  fif_save_defaults(void);
void fif_value_text(unsigned i, int v, char *out, unsigned size);

#endif /* FOIR_IF_H */
