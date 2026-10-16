/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - optif.c
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

#include <dlfcn.h>
#include <link.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "altru_suxty.h"
#include "cfg.h"
#include "foir_suxty.h"
#include "main.h"
#include "m64p_types.h"
#include "optif.h"

#define SAME_LAYOUT(a, b, f) _Static_assert(offsetof(struct a, f) == offsetof(struct b, f), #f)
#define SAME_SIZE(a, b) _Static_assert(sizeof(struct a) == sizeof(struct b), #a)
SAME_SIZE(foir_suxty_option, optif_option);
SAME_SIZE(altru_suxty_option, optif_option);
SAME_SIZE(foir_suxty_interface, optif_iface);
SAME_SIZE(altru_suxty_interface, optif_iface);
SAME_LAYOUT(foir_suxty_interface, optif_iface, get);
SAME_LAYOUT(foir_suxty_interface, optif_iface, save_default);
SAME_LAYOUT(altru_suxty_interface, optif_iface, get);
SAME_LAYOUT(altru_suxty_interface, optif_iface, save_default);
SAME_LAYOUT(foir_suxty_option, optif_option, def);
SAME_LAYOUT(altru_suxty_option, optif_option, def);
SAME_LAYOUT(foir_suxty_option, optif_option, enum_labels);
SAME_LAYOUT(altru_suxty_option, optif_option, enum_labels);

optif_t g_foir  = { .symbol = "FoirSuxtyGetInterface", .abi = FOIR_SUXTY_ABI, .section = "foir", .name = "foir" };
optif_t g_altru = { .symbol = "AltruSuxtyGetInterface", .abi = ALTRU_SUXTY_ABI, .section = "audio", .name = "altru",
                    .alias_from = "VOLUME_DEFAULT", .alias_to = "volume", .strict_step = 1 };

typedef struct {
    const optif_t *o;
    const struct optif_iface *found;
    int  wrong_abi;
    char path[256];
} scan_t;

static int scan_cb(struct dl_phdr_info *info, size_t size, void *data)
{
    scan_t *s = (scan_t *) data;
    void *h;
    const struct optif_iface *(*get)(void);

    (void) size;
    if (info->dlpi_name == NULL || info->dlpi_name[0] == 0)
        return 0;
    h = dlopen(info->dlpi_name, RTLD_NOW | RTLD_NOLOAD);       /* reference to an already loaded object */
    if (h == NULL)
        return 0;
    *(void **) (&get) = dlsym(h, s->o->symbol);
    if (get != NULL)
    {
        const struct optif_iface *i = get();
        if (i != NULL && i->abi == s->o->abi && i->size >= sizeof(*i) && i->option_count <= OPTIF_MAX)
        {
            s->found = i;
            snprintf(s->path, sizeof(s->path), "%s", info->dlpi_name);
        }
        else if (i != NULL)
            s->wrong_abi = (int) i->abi;
    }
    dlclose(h);                                                /* the plugin stays loaded: the front-end holds it */
    return s->found != NULL;
}

int optif_discover(optif_t *o)
{
    scan_t s;
    unsigned i;

    if (o->iface != NULL)
        return 1;
    memset(&s, 0, sizeof(s));
    s.o = o;
    dl_iterate_phdr(scan_cb, &s);
    if (s.found == NULL)
    {
        if (s.wrong_abi)
            DebugMessage(M64MSG_WARNING, "%s offers suxty interface ABI %d, this suxty needs %u: no options page", o->name, s.wrong_abi, o->abi);
        else
            DebugMessage(M64MSG_VERBOSE, "no %s plugin loaded: no options page", o->name);
        return 0;
    }
    o->iface = s.found;
    for (i = 0; i < o->iface->option_count; i++)
        if (o->iface->get(i, &o->base[i]) != 0)
            o->base[i] = o->iface->options[i].def;
    DebugMessage(M64MSG_INFO, "%s found (%s): %u options", o->name, s.path, o->iface->option_count);
    return 1;
}

void optif_release(optif_t *o) { o->iface = NULL; }
void optif_discover_all(void) { optif_discover(&g_foir); optif_discover(&g_altru); }
void optif_release_all(void) { optif_release(&g_foir); optif_release(&g_altru); }

unsigned optif_count(const optif_t *o) { return o->iface != NULL ? o->iface->option_count : 0; }

const struct optif_option *optif_option(const optif_t *o, unsigned i)
{
    return o->iface != NULL && i < o->iface->option_count ? &o->iface->options[i] : NULL;
}

const char *optif_key(const optif_t *o, unsigned i)
{
    const struct optif_option *p = optif_option(o, i);
    if (p == NULL)
        return "";
    return o->alias_from != NULL && strcmp(p->key, o->alias_from) == 0 ? o->alias_to : p->key;
}

void optif_value_text(const optif_t *o, unsigned i, int v, char *out, unsigned size)
{
    const struct optif_option *p = optif_option(o, i);
    if (p == NULL)
        snprintf(out, size, "%d", v);
    else if (p->type == OPT_BOOL)
        snprintf(out, size, "%s", v ? "true" : "false");
    else if (p->type == OPT_ENUM && v >= p->min && v <= p->max)
        snprintf(out, size, "%s", p->enum_labels[v]);
    else
        snprintf(out, size, "%d", v);
}

static int parse_value(const optif_t *o, const struct optif_option *p, const char *text, int *v)
{
    char *end;
    long n;
    int k;

    if (text == NULL || text[0] == 0)
        return 0;
    if (p->type == OPT_BOOL)
    {
        if (!strcasecmp(text, "true") || !strcasecmp(text, "yes") || !strcasecmp(text, "on") || !strcmp(text, "1")) { *v = 1; return 1; }
        if (!strcasecmp(text, "false") || !strcasecmp(text, "no") || !strcasecmp(text, "off") || !strcmp(text, "0")) { *v = 0; return 1; }
        return 0;
    }
    if (p->type == OPT_ENUM)
        for (k = p->min; k <= p->max; k++)
            if (!strcasecmp(text, p->enum_labels[k]))
            {
                *v = k;
                return 1;
            }
    n = strtol(text, &end, 10);
    if (*end != 0 || n < p->min || n > p->max)
        return 0;
    if (o->strict_step && p->type == OPT_INT && p->step > 1)   /* a hand-edited 63 where altru only takes steps of 5 */
        n = p->min + ((n - p->min + p->step / 2) / p->step) * p->step;
    *v = (int) (n > p->max ? p->max : n);
    return 1;
}

/* unparsable values are skipped so a hand-edit mistake in the game file doesn't hide a good global one */
static int layer_value(const ini_t *ini, const optif_t *o, unsigned i, int *v)
{
    const char *key = optif_key(o, i);
    return ini != NULL && ini_has(ini, o->section, key) && parse_value(o, optif_option(o, i), ini_get(ini, o->section, key, ""), v);
}

static int stored(const optif_t *o, unsigned i, int scoped, int *v)
{
    if (optif_option(o, i) == NULL)
        return 0;
    if ((!scoped || g_cfg.scope == CFG_SCOPE_GAME) && layer_value(g_cfg.game, o, i, v))
        return 1;
    return layer_value(g_cfg.ini, o, i, v);
}

static int effective(const optif_t *o, unsigned i)
{
    int v;
    return stored(o, i, 0, &v) ? v : o->base[i];
}

void optif_apply_config(optif_t *o)
{
    unsigned i;
    int n = 0;
    if (o->iface == NULL)
        return;
    for (i = 0; i < o->iface->option_count; i++)
    {
        int v;
        const char *key = optif_key(o, i);
        if (cfg_get(o->section, key, "")[0] && !stored(o, i, 0, &v))
            DebugMessage(M64MSG_WARNING, "[%s] %s = '%s' is not a valid value; ignored", o->section, key, cfg_get(o->section, key, ""));
        if (stored(o, i, 0, &v) && o->iface->set(i, v) >= 0)
            n++;
    }
    if (n > 0)
        DebugMessage(M64MSG_INFO, "%s: %d option(s) set from the suxty configuration", o->name, n);
}

int optif_shown(const optif_t *o, unsigned i)
{
    int v;
    if (stored(o, i, 1, &v))
        return v;
    /* unset in this scope: GAME shows what the game plays with, global shows the plugin's own value */
    if (g_cfg.scope == CFG_SCOPE_GAME)
        return effective(o, i);
    return o->base[i];
}

int optif_overridden(const optif_t *o, unsigned i)
{
    return optif_option(o, i) != NULL && cfg_overridden(o->section, optif_key(o, i));
}

int optif_pending(const optif_t *o, unsigned i)
{
    int running = 0;
    const struct optif_option *p = optif_option(o, i);
    if (p == NULL || (p->flags & OPTF_LIVE) || o->iface->get(i, &running) != 0)
        return 0;
    return effective(o, i) != running;
}

int optif_set(optif_t *o, unsigned i, int v)
{
    char text[48];
    int r;
    if (optif_option(o, i) == NULL)
        return -1;
    optif_value_text(o, i, v, text, sizeof(text));
    cfg_set(o->section, optif_key(o, i), text);
    r = o->iface->set(i, effective(o, i));
    DebugMessage(M64MSG_INFO, "%s: %s = %s (%s)", o->name, optif_key(o, i), text,
                 r == OPT_APPLIED ? "applied" : r == OPT_NEXT_LAUNCH ? "next launch" : "refused");
    return r;
}

void optif_reset(optif_t *o, unsigned i)
{
    if (optif_option(o, i) == NULL)
        return;
    cfg_reset(o->section, optif_key(o, i));
    o->iface->set(i, effective(o, i));
}

void optif_foir_ff_frameskip(int skip)
{
    unsigned i;
    int v;
    for (i = 0; g_foir.iface != NULL && i < g_foir.iface->option_count; i++)
        if (strcmp(g_foir.iface->options[i].key, "FrameSkip") == 0)
            break;
    if (g_foir.iface == NULL || i == g_foir.iface->option_count)
        return;
    v = effective(&g_foir, i);                       /* 0-3 fixed, 4 auto */
    if (skip > 0 && (v == 4 || v < skip))
        v = skip;
    g_foir.iface->set(i, v);
}

void optif_reapply_all(optif_t *o)
{
    unsigned i;
    for (i = 0; o->iface != NULL && i < o->iface->option_count; i++)
        o->iface->set(i, effective(o, i));
}

int optif_pin_game(optif_t *o)
{
    unsigned i;
    int n = 0;
    if (o->iface == NULL || g_cfg.game == NULL)
        return 0;
    for (i = 0; i < o->iface->option_count; i++)
    {
        char text[48];
        if (ini_has(g_cfg.game, o->section, optif_key(o, i)))
            continue;
        optif_value_text(o, i, effective(o, i), text, sizeof(text));
        ini_set(g_cfg.game, o->section, optif_key(o, i), text);
        n++;
    }
    return n;
}

int optif_save_defaults(optif_t *o)
{
    unsigned i;
    int rc = 0;
    if (o->iface == NULL)
        return -1;
    for (i = 0; i < o->iface->option_count; i++)
    {
        int v = optif_shown(o, i);
        if (o->iface->save_default(i, v) != 0)
            rc = -1;
        else if (g_cfg.scope != CFG_SCOPE_GAME || !optif_overridden(o, i))
            o->base[i] = v;
    }
    return rc;
}
