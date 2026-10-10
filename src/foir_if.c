/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - foir_if.c
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cfg.h"
#include "foir_if.h"
#include "main.h"
#include "m64p_types.h"

#define MAX_OPTS 32

static const struct foir_suxty_interface *l_if;
static int  l_base[MAX_OPTS];                 /* foir's own value (its config) before suxty set anything */
static char l_name[256];

typedef struct {
    const struct foir_suxty_interface *found;
    int                                wrong_abi;
    char                               path[256];
} scan_t;

static int scan_cb(struct dl_phdr_info *info, size_t size, void *data)
{
    scan_t *s = (scan_t *) data;
    void *h;
    const struct foir_suxty_interface *(*get)(void);

    (void) size;
    if (info->dlpi_name == NULL || info->dlpi_name[0] == 0)
        return 0;
    h = dlopen(info->dlpi_name, RTLD_NOW | RTLD_NOLOAD);       /* reference to an already loaded object */
    if (h == NULL)
        return 0;
    *(void **) (&get) = dlsym(h, "FoirSuxtyGetInterface");
    if (get != NULL)
    {
        const struct foir_suxty_interface *i = get();
        if (i != NULL && i->abi == FOIR_SUXTY_ABI && i->size >= sizeof(*i) && i->option_count <= MAX_OPTS)
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

int fif_discover(void)
{
    scan_t s;
    unsigned i;

    if (l_if != NULL)
        return 1;
    memset(&s, 0, sizeof(s));
    dl_iterate_phdr(scan_cb, &s);
    if (s.found == NULL)
    {
        if (s.wrong_abi)
            DebugMessage(M64MSG_WARNING, "foir offers suxty interface ABI %d, this suxty needs %d: no Video page", s.wrong_abi, FOIR_SUXTY_ABI);
        else
            DebugMessage(M64MSG_VERBOSE, "no foir video plugin loaded: no Video page");
        return 0;
    }
    l_if = s.found;
    snprintf(l_name, sizeof(l_name), "%s", s.path);
    for (i = 0; i < l_if->option_count; i++)
        if (l_if->get(i, &l_base[i]) != 0)
            l_base[i] = l_if->options[i].def;
    DebugMessage(M64MSG_INFO, "foir found (%s): %u options for the Video page", l_name, l_if->option_count);
    return 1;
}

const struct foir_suxty_interface *fif_get(void) { return l_if; }

void fif_release(void)
{
    l_if = NULL;
}

unsigned fif_count(void) { return l_if != NULL ? l_if->option_count : 0; }
const struct foir_suxty_option *fif_option(unsigned i) { return l_if != NULL && i < l_if->option_count ? &l_if->options[i] : NULL; }

void fif_value_text(unsigned i, int v, char *out, unsigned size)
{
    const struct foir_suxty_option *o = fif_option(i);
    if (o == NULL)
        snprintf(out, size, "%d", v);
    else if (o->type == FOIR_OPT_BOOL)
        snprintf(out, size, "%s", v ? "true" : "false");
    else if (o->type == FOIR_OPT_ENUM && v >= o->min && v <= o->max)
        snprintf(out, size, "%s", o->enum_labels[v]);
    else
        snprintf(out, size, "%d", v);
}

static int parse_value(const struct foir_suxty_option *o, const char *text, int *v)
{
    char *end;
    long n;
    int k;

    if (text == NULL || text[0] == 0)
        return 0;
    if (o->type == FOIR_OPT_BOOL)
    {
        if (!strcasecmp(text, "true") || !strcasecmp(text, "yes") || !strcasecmp(text, "on") || !strcmp(text, "1")) { *v = 1; return 1; }
        if (!strcasecmp(text, "false") || !strcasecmp(text, "no") || !strcasecmp(text, "off") || !strcmp(text, "0")) { *v = 0; return 1; }
        return 0;
    }
    if (o->type == FOIR_OPT_ENUM)
        for (k = o->min; k <= o->max; k++)
            if (!strcasecmp(text, o->enum_labels[k]))
            {
                *v = k;
                return 1;
            }
    n = strtol(text, &end, 10);
    if (*end != 0 || n < o->min || n > o->max)
        return 0;
    *v = (int) n;
    return 1;
}

/* unparsable values are skipped so a hand-edit mistake in the game file doesn't hide a good global one */
static int layer_value(const ini_t *ini, const struct foir_suxty_option *o, int *v)
{
    return ini != NULL && ini_has(ini, "foir", o->key) && parse_value(o, ini_get(ini, "foir", o->key, ""), v);
}

static int stored(unsigned i, int scoped, int *v)
{
    const struct foir_suxty_option *o = fif_option(i);
    if (o == NULL)
        return 0;
    if ((!scoped || g_cfg.scope == CFG_SCOPE_GAME) && layer_value(g_cfg.game, o, v))
        return 1;
    return layer_value(g_cfg.ini, o, v);
}

static int effective(unsigned i)
{
    int v;
    return stored(i, 0, &v) ? v : l_base[i];
}

void fif_apply_config(void)
{
    unsigned i;
    int n = 0;
    if (l_if == NULL)
        return;
    for (i = 0; i < l_if->option_count; i++)
    {
        int v;
        const struct foir_suxty_option *o = &l_if->options[i];
        if (cfg_get("foir", o->key, "")[0] && !stored(i, 0, &v))
            DebugMessage(M64MSG_WARNING, "[foir] %s = '%s' is not a valid value; ignored", o->key, cfg_get("foir", o->key, ""));
        if (stored(i, 0, &v) && l_if->set(i, v) >= 0)
            n++;
    }
    if (n > 0)
        DebugMessage(M64MSG_INFO, "foir: %d option(s) set from the suxty configuration", n);
}

int fif_shown(unsigned i)
{
    int v;
    if (stored(i, 1, &v))
        return v;
    /* unset in this scope: GAME shows what the game plays with, global shows foir's own value */
    if (g_cfg.scope == CFG_SCOPE_GAME)
        return effective(i);
    return l_base[i];
}

int fif_overridden(unsigned i)
{
    const struct foir_suxty_option *o = fif_option(i);
    return o != NULL && cfg_overridden("foir", o->key);
}

int fif_pending(unsigned i)
{
    int running = 0;
    const struct foir_suxty_option *o = fif_option(i);
    if (o == NULL || (o->flags & FOIR_OPTF_LIVE) || l_if->get(i, &running) != 0)
        return 0;
    return effective(i) != running;
}

int fif_set(unsigned i, int v)
{
    const struct foir_suxty_option *o = fif_option(i);
    char text[48];
    int r;
    if (o == NULL)
        return FOIR_SUXTY_E_INDEX;
    fif_value_text(i, v, text, sizeof(text));
    cfg_set("foir", o->key, text);
    r = l_if->set(i, effective(i));
    DebugMessage(M64MSG_INFO, "foir: %s = %s (%s)", o->key, text,
                 r == FOIR_SUXTY_APPLIED ? "applied" : r == FOIR_SUXTY_NEXT_LAUNCH ? "next launch" : "refused");
    return r;
}

void fif_reset(unsigned i)
{
    const struct foir_suxty_option *o = fif_option(i);
    if (o == NULL)
        return;
    cfg_reset("foir", o->key);
    l_if->set(i, effective(i));
}

void fif_reapply_all(void)
{
    unsigned i;
    for (i = 0; l_if != NULL && i < l_if->option_count; i++)
        l_if->set(i, effective(i));
}

int fif_pin_game(void)
{
    unsigned i;
    int n = 0;
    if (l_if == NULL || g_cfg.game == NULL)
        return 0;
    for (i = 0; i < l_if->option_count; i++)
    {
        char text[48];
        const struct foir_suxty_option *o = &l_if->options[i];
        if (ini_has(g_cfg.game, "foir", o->key))
            continue;
        fif_value_text(i, effective(i), text, sizeof(text));
        ini_set(g_cfg.game, "foir", o->key, text);
        n++;
    }
    return n;
}

int fif_save_defaults(void)
{
    unsigned i;
    int rc = 0;
    if (l_if == NULL)
        return -1;
    for (i = 0; i < l_if->option_count; i++)
    {
        int v = fif_shown(i);
        if (l_if->save_default(i, v) != 0)
            rc = -1;
        else if (g_cfg.scope != CFG_SCOPE_GAME || !fif_overridden(i))
            l_base[i] = v;
    }
    return rc;
}
