/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - optif.h
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

/* one-way interfaces (foir_suxty.h, altru_suxty.h): suxty never links the plugins and they never call suxty
 * ui-console loads plugins RTLD_LOCAL, so each is found by walking dl_iterate_phdr() with dlopen(RTLD_NOLOAD)
 * [foir] / [audio] <Key> lives in suxty.cfg or the game file; mupen64plus.cfg is written only by save_default
 * Both headers have the same layout (checked in optif.c), so one set of code serves both */

#ifndef OPTIF_H
#define OPTIF_H

#include <stdint.h>

#define OPTIF_MAX 32

enum { OPT_BOOL = 0, OPT_INT = 1, OPT_ENUM = 2 };
#define OPTF_LIVE 1u
#define OPT_APPLIED     0
#define OPT_NEXT_LAUNCH 1

struct optif_option {
    const char *key, *label, *help;
    int         type;
    unsigned    flags;
    int         min, max, step;
    const char *const *enum_labels;
    int         def;
};

struct optif_iface {
    uint32_t abi, size, version, option_count;
    const struct optif_option *options;
    int (*get)(unsigned index, int *value);
    int (*set)(unsigned index, int value);
    int (*save_default)(unsigned index, int value);
};

typedef struct {
    const char *symbol;                   /* exported getter */
    uint32_t    abi;
    const char *section;                  /* suxty.cfg / game file section */
    const char *name;                     /* for messages */
    const char *alias_from, *alias_to;    /* option key stored under another name (altru Volume = [audio] volume) */
    int         strict_step;              /* set() refuses values off the step */
    const struct optif_iface *iface;
    int         base[OPTIF_MAX];          /* the plugin's own value (its config) before suxty set anything */
} optif_t;

extern optif_t g_foir, g_altru;

int  optif_discover(optif_t *o);
void optif_release(optif_t *o);
void optif_discover_all(void);
void optif_release_all(void);

void optif_apply_config(optif_t *o);

/* emulation thread only */
unsigned optif_count(const optif_t *o);
const struct optif_option *optif_option(const optif_t *o, unsigned i);
const char *optif_key(const optif_t *o, unsigned i);
int  optif_shown(const optif_t *o, unsigned i);
int  optif_overridden(const optif_t *o, unsigned i);
int  optif_pending(const optif_t *o, unsigned i);
int  optif_set(optif_t *o, unsigned i, int v);
void optif_reset(optif_t *o, unsigned i);
void optif_reapply_all(optif_t *o);
int  optif_pin_game(optif_t *o);
int  optif_save_defaults(optif_t *o);
void optif_value_text(const optif_t *o, unsigned i, int v, char *out, unsigned size);
/* fast-forward: render one frame in skip+1 without touching the stored option; 0 = back to the configured value */
void optif_foir_ff_frameskip(int skip);

#endif /* OPTIF_H */
