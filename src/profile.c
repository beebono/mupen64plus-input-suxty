/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-ui-suxty - profile.c
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
#include <strings.h>

#include "cfg.h"
#include "core_interface.h"
#include "main.h"
#include "m64p_types.h"
#include "plugin.h"
#include "profile.h"

/* See profile.h for the design. */

static int l_save_core = 1;

typedef struct {
    char      sec[48];
    char      key[48];
    m64p_type type;
    char      global[256];      /* the global value (what mupen64plus.cfg holds) */
    char      game[256];        /* the value this game runs with (what is in the core's memory) */
} applied_t;

#define MAX_APPLIED 128
static applied_t l_app[MAX_APPLIED];
static int       l_napp;

typedef struct {
    char sec[48];
    char key[48];
    char val[256];
} snap_t;

#define MAX_SNAP 640
static snap_t l_snap[MAX_SNAP];
static int    l_nsnap;

#define MAX_TOUCHED 64
static struct { char sec[48]; char key[48]; } l_touched[MAX_TOUCHED];
static int l_ntouched;

void profile_set_save_core(int save) { l_save_core = save; }

/* ---- which parameters a game may override -------------------------------------------------------------- */

static int starts_with_ci(const char *s, const char *prefix)
{
    return strncasecmp(s, prefix, strlen(prefix)) == 0;
}

/* Core parameters that make sense per game; everything under the plugin prefixes is allowed.  Input-* is managed by the
 * remap layer and UI-Suxty / CoreEvents / Core paths are not game settings. */
static int param_allowed(const char *sec, const char *key)
{
    static const char *const core_keys[] = {
        "R4300Emulator", "DisableExtraMem", "CountPerOp", "CountPerOpDenomPot", "NoCompiledJump", "RandomizeInterrupt",
        "SiDmaDuration", "SaveDiskFormat"
    };
    size_t i;
    if (strcasecmp(sec, "Core") == 0)
    {
        for (i = 0; i < sizeof(core_keys) / sizeof(core_keys[0]); i++)
            if (strcasecmp(key, core_keys[i]) == 0)
                return 1;
        return 0;
    }
    return starts_with_ci(sec, "Video-") || starts_with_ci(sec, "Audio-") || starts_with_ci(sec, "Rsp-");
}

/* ---- reading and writing the core's in-memory config ---------------------------------------------------- */

typedef struct {
    const char *want;
    int         found;
} sec_probe;

static void probe_section_cb(void *ctx, const char *name)
{
    sec_probe *p = (sec_probe *) ctx;
    if (strcasecmp(name, p->want) == 0)
        p->found = 1;
}

static int section_exists(const char *name)
{
    sec_probe p;
    p.want = name;
    p.found = 0;
    if (ConfigListSections != NULL)
        (*ConfigListSections)(&p, probe_section_cb);
    return p.found;
}

static int mem_type(const char *sec, const char *key, m64p_handle *h, m64p_type *type)
{
    if (!section_exists(sec))
        return 0;
    if ((*ConfigOpenSection)(sec, h) != M64ERR_SUCCESS)
        return 0;
    return (*ConfigGetParameterType)(*h, key, type) == M64ERR_SUCCESS;
}

static int get_mem(m64p_handle h, const char *key, m64p_type type, char *out, size_t size)
{
    switch (type)
    {
        case M64TYPE_INT:
        {
            int v = 0;
            if ((*ConfigGetParameter)(h, key, M64TYPE_INT, &v, sizeof(v)) != M64ERR_SUCCESS)
                return 0;
            snprintf(out, size, "%d", v);
            return 1;
        }
        case M64TYPE_BOOL:
        {
            int v = 0;
            if ((*ConfigGetParameter)(h, key, M64TYPE_BOOL, &v, sizeof(v)) != M64ERR_SUCCESS)
                return 0;
            snprintf(out, size, "%s", v ? "True" : "False");
            return 1;
        }
        case M64TYPE_FLOAT:
        {
            float v = 0;
            if ((*ConfigGetParameter)(h, key, M64TYPE_FLOAT, &v, sizeof(v)) != M64ERR_SUCCESS)
                return 0;
            snprintf(out, size, "%g", (double) v);
            return 1;
        }
        case M64TYPE_STRING:
        {
            const char *s = (*ConfigGetParamString)(h, key);
            snprintf(out, size, "%s", s ? s : "");
            return 1;
        }
        default:
            return 0;
    }
}

static int parse_bool(const char *s)
{
    return strcasecmp(s, "true") == 0 || strcasecmp(s, "yes") == 0 || strcasecmp(s, "on") == 0 || strcmp(s, "1") == 0;
}

static void set_mem(m64p_handle h, const char *key, m64p_type type, const char *val)
{
    switch (type)
    {
        case M64TYPE_INT:
        {
            int v = atoi(val);
            (*ConfigSetParameter)(h, key, M64TYPE_INT, &v);
            break;
        }
        case M64TYPE_BOOL:
        {
            int v = parse_bool(val);
            (*ConfigSetParameter)(h, key, M64TYPE_BOOL, &v);
            break;
        }
        case M64TYPE_FLOAT:
        {
            float v = (float) atof(val);
            (*ConfigSetParameter)(h, key, M64TYPE_FLOAT, &v);
            break;
        }
        case M64TYPE_STRING:
            (*ConfigSetParameter)(h, key, M64TYPE_STRING, val);
            break;
        default:
            break;
    }
}

int profile_normalize(m64p_type type, const char *in, char *out, size_t size)
{
    switch (type)
    {
        case M64TYPE_INT:    snprintf(out, size, "%d", atoi(in)); return 1;
        case M64TYPE_BOOL:   snprintf(out, size, "%s", parse_bool(in) ? "True" : "False"); return 1;
        case M64TYPE_FLOAT:  snprintf(out, size, "%g", (double) (float) atof(in)); return 1;
        case M64TYPE_STRING: snprintf(out, size, "%s", in); return 1;
        default:             return 0;
    }
}

static applied_t *find_applied(const char *sec, const char *key)
{
    int i;
    for (i = 0; i < l_napp; i++)
        if (strcasecmp(l_app[i].sec, sec) == 0 && strcasecmp(l_app[i].key, key) == 0)
            return &l_app[i];
    return NULL;
}

/* ---- launch --------------------------------------------------------------------------------------------- */

void profile_apply(void)
{
    int i;
    const char *sec, *key, *val;

    l_napp = 0;
    if (g_cfg.game == NULL)
        return;
    for (i = 0; ini_entry(g_cfg.game, i, &sec, &key, &val); i++)
    {
        m64p_handle h;
        m64p_type type;
        applied_t *a;
        char norm[256], cur[256];

        if (strncmp(sec, "cfg:", 4) != 0)
            continue;
        sec += 4;
        if (!param_allowed(sec, key))
        {
            DebugMessage(M64MSG_WARNING, "game profile: [cfg:%s] %s may not be set per game; ignored", sec, key);
            continue;
        }
        if (!mem_type(sec, key, &h, &type))
        {
            DebugMessage(M64MSG_VERBOSE, "game profile: [%s] %s does not exist (another plugin?); ignored", sec, key);
            continue;
        }
        if (!get_mem(h, key, type, cur, sizeof(cur)) || !profile_normalize(type, val, norm, sizeof(norm)))
            continue;
        if (strcmp(cur, norm) == 0 || l_napp >= MAX_APPLIED)
            continue;                    /* nothing to change (or no room: ignore) */
        a = &l_app[l_napp++];
        snprintf(a->sec, sizeof(a->sec), "%s", sec);
        snprintf(a->key, sizeof(a->key), "%s", key);
        a->type = type;
        snprintf(a->global, sizeof(a->global), "%s", cur);
        snprintf(a->game, sizeof(a->game), "%s", norm);
        set_mem(h, key, type, norm);
        DebugMessage(M64MSG_INFO, "game profile: [%s] %s = %s (global: %s)", sec, key, norm, cur);
    }
}

void profile_note_external_changes(void)
{
    int i = 0;
    while (i < l_napp)
    {
        m64p_handle h;
        m64p_type type;
        char cur[256];
        if (mem_type(l_app[i].sec, l_app[i].key, &h, &type) && get_mem(h, l_app[i].key, type, cur, sizeof(cur)) &&
            strcmp(cur, l_app[i].game) != 0)
        {
            DebugMessage(M64MSG_INFO, "game profile: [%s] %s was set on the command line (%s); the command line wins",
                         l_app[i].sec, l_app[i].key, cur);
            l_app[i] = l_app[--l_napp];
            continue;
        }
        i++;
    }
}

typedef struct {
    const char *sec;
    m64p_handle h;
    snap_t     *list;
    int        *n;
} snap_ctx;

static void snap_param_cb(void *ctx, const char *name, m64p_type type)
{
    snap_ctx *c = (snap_ctx *) ctx;
    snap_t *s;
    if (*c->n >= MAX_SNAP || !param_allowed(c->sec, name))
        return;
    s = &c->list[*c->n];
    if (!get_mem(c->h, name, type, s->val, sizeof(s->val)))
        return;
    snprintf(s->sec, sizeof(s->sec), "%s", c->sec);
    snprintf(s->key, sizeof(s->key), "%s", name);
    (*c->n)++;
}

typedef struct {
    char names[64][48];
    int  n;
} sec_list;

static void collect_section_cb(void *ctx, const char *name)
{
    sec_list *l = (sec_list *) ctx;
    if (l->n < 64)
        snprintf(l->names[l->n++], sizeof(l->names[0]), "%s", name);
}

void profile_snapshot(void)
{
    sec_list sl;
    int i;
    l_nsnap = 0;
    sl.n = 0;
    (*ConfigListSections)(&sl, collect_section_cb);
    for (i = 0; i < sl.n; i++)
    {
        snap_ctx c;
        c.sec = sl.names[i];
        c.list = l_snap;
        c.n = &l_nsnap;
        if (strcasecmp(c.sec, "Core") != 0 && !starts_with_ci(c.sec, "Video-") && !starts_with_ci(c.sec, "Audio-") &&
            !starts_with_ci(c.sec, "Rsp-"))
            continue;
        if ((*ConfigOpenSection)(c.sec, &c.h) == M64ERR_SUCCESS)
            (*ConfigListParameters)(c.h, &c, snap_param_cb);
    }
    l_ntouched = 0;
}

/* ---- saving ---------------------------------------------------------------------------------------------- */

static void set_values(const char *only_sec, int to_global)
{
    int i;
    for (i = 0; i < l_napp; i++)
    {
        m64p_handle h;
        if (only_sec != NULL && strcasecmp(l_app[i].sec, only_sec) != 0)
            continue;
        if ((*ConfigOpenSection)(l_app[i].sec, &h) == M64ERR_SUCCESS)
            set_mem(h, l_app[i].key, l_app[i].type, to_global ? l_app[i].global : l_app[i].game);
    }
}

void profile_save_file(void)
{
    if (!l_save_core)
        return;
    set_values(NULL, 1);
    (*ConfigSaveFile)();
    set_values(NULL, 0);
}

void profile_save_section(const char *section)
{
    if (!l_save_core)
        return;
    set_values(section, 1);
    (*ConfigSaveSection)(section);
    set_values(section, 0);
}

void profile_shutdown(void)
{
    l_napp = 0;
    l_nsnap = 0;
    l_ntouched = 0;
}

/* ---- the plugins' sections ----------------------------------------------------------------------------- */

const char *profile_plugin_choice(const char *kind)
{
    const char *v;
    if (g_cfg.game == NULL)
        return NULL;
    v = ini_get(g_cfg.game, "plugins", kind, NULL);
    return (v != NULL && v[0] != 0) ? v : NULL;
}

int profile_plugin_section(m64p_plugin_type type, char *out, size_t size)
{
    const plugin_map_node *node = NULL;
    const char *prefix, *base, *dash;
    char stem[64];
    sec_list sl;
    size_t n, i;
    int k, best = -1;

    for (k = 0; k < 4; k++)
        if (g_PluginMap[k].type == type)
            node = &g_PluginMap[k];
    if (node == NULL || node->handle == NULL)
        return 0;
    prefix = type == M64PLUGIN_GFX ? "Video-" : type == M64PLUGIN_AUDIO ? "Audio-" : type == M64PLUGIN_RSP ? "Rsp-" : "Input-";
    base = strrchr(node->filename, '/');
    base = base ? base + 1 : node->filename;
    if (starts_with_ci(base, "mupen64plus-"))
        base += 12;
    dash = strchr(base, '-');
    if (dash == NULL)
        return 0;
    dash++;
    for (n = 0; dash[n] && dash[n] != '.' && n + 1 < sizeof(stem); n++)
        stem[n] = dash[n];
    stem[n] = 0;
    if (n == 0)
        return 0;

    sl.n = 0;
    (*ConfigListSections)(&sl, collect_section_cb);
    for (i = 0; i < (size_t) sl.n; i++)
    {
        const char *name = sl.names[i];
        if (!starts_with_ci(name, prefix) || strcasecmp(name, "Video-General") == 0)
            continue;
        if (strcasecmp(name + strlen(prefix), stem) == 0)
        {
            best = (int) i;
            break;
        }
        if (best < 0 && (strcasestr(name + strlen(prefix), stem) != NULL || strcasestr(stem, name + strlen(prefix)) != NULL))
            best = (int) i;
    }
    if (best < 0)
        return 0;
    snprintf(out, size, "%s", sl.names[best]);
    return 1;
}

typedef struct {
    profile_param *out;
    int            max, n;
} list_ctx;

static void list_param_cb(void *ctx, const char *name, m64p_type type)
{
    list_ctx *c = (list_ctx *) ctx;
    if (c->n >= c->max || strcasestr(name, "version") != NULL)      /* "Version", "configVersion": the plugin's own bookkeeping */
        return;
    snprintf(c->out[c->n].name, sizeof(c->out[c->n].name), "%s", name);
    c->out[c->n].type = type;
    c->n++;
}

int profile_list_params(const char *section, profile_param *out, int max)
{
    list_ctx c;
    m64p_handle h;
    if (!section_exists(section) || (*ConfigOpenSection)(section, &h) != M64ERR_SUCCESS)
        return 0;
    c.out = out;
    c.max = max;
    c.n = 0;
    (*ConfigListParameters)(h, &c, list_param_cb);
    return c.n;
}

const char *profile_param_help(const char *section, const char *name)
{
    m64p_handle h;
    if (!section_exists(section) || (*ConfigOpenSection)(section, &h) != M64ERR_SUCCESS)
        return NULL;
    return (*ConfigGetParameterHelp)(h, name);
}

/* ---- values ---------------------------------------------------------------------------------------------- */

static void game_section(const char *sec, char *out, size_t size)
{
    snprintf(out, size, "cfg:%.52s", sec);
}

int profile_get_global(const char *sec, const char *name, char *out, size_t size)
{
    m64p_handle h;
    m64p_type type;
    applied_t *a = find_applied(sec, name);
    if (a != NULL)
    {
        snprintf(out, size, "%s", a->global);
        return 1;
    }
    if (!mem_type(sec, name, &h, &type))
        return 0;
    return get_mem(h, name, type, out, size);
}

int profile_get_game(const char *sec, const char *name, char *out, size_t size)
{
    char gs[64];
    m64p_handle h;
    m64p_type type;
    if (g_cfg.game == NULL)
        return 0;
    game_section(sec, gs, sizeof(gs));
    if (!ini_has(g_cfg.game, gs, name))
        return 0;
    if (mem_type(sec, name, &h, &type))
        return profile_normalize(type, ini_get(g_cfg.game, gs, name, ""), out, size);
    snprintf(out, size, "%s", ini_get(g_cfg.game, gs, name, ""));
    return 1;
}

int profile_overridden(const char *sec, const char *name)
{
    char gs[64];
    if (g_cfg.game == NULL)
        return 0;
    game_section(sec, gs, sizeof(gs));
    return ini_has(g_cfg.game, gs, name);
}

int profile_get_scoped(const char *sec, const char *name, char *out, size_t size)
{
    if (g_cfg.scope == CFG_SCOPE_GAME && profile_get_game(sec, name, out, size))
        return 1;
    return profile_get_global(sec, name, out, size);
}

static void touch(const char *sec, const char *key)
{
    int i;
    for (i = 0; i < l_ntouched; i++)
        if (strcasecmp(l_touched[i].sec, sec) == 0 && strcasecmp(l_touched[i].key, key) == 0)
            return;
    if (l_ntouched < MAX_TOUCHED)
    {
        snprintf(l_touched[l_ntouched].sec, sizeof(l_touched[0].sec), "%s", sec);
        snprintf(l_touched[l_ntouched].key, sizeof(l_touched[0].key), "%s", key);
        l_ntouched++;
    }
}

/* sections whose global values were edited; written by profile_flush() (a slider is not a reason to write a file per step) */
static char l_dirty[8][48];
static int  l_ndirty;

static void mark_dirty(const char *sec)
{
    int i;
    for (i = 0; i < l_ndirty; i++)
        if (strcasecmp(l_dirty[i], sec) == 0)
            return;
    if (l_ndirty < 8)
        snprintf(l_dirty[l_ndirty++], sizeof(l_dirty[0]), "%s", sec);
}

void profile_flush(void)
{
    int i;
    for (i = 0; i < l_ndirty; i++)
        profile_save_section(l_dirty[i]);
    l_ndirty = 0;
}

void profile_set(const char *sec, const char *name, const char *value)
{
    m64p_handle h;
    m64p_type type;
    char norm[256], gs[64];

    if (!param_allowed(sec, name) || !mem_type(sec, name, &h, &type) || !profile_normalize(type, value, norm, sizeof(norm)))
        return;
    touch(sec, name);
    if (g_cfg.scope == CFG_SCOPE_GAME && g_cfg.game != NULL)
    {
        game_section(sec, gs, sizeof(gs));
        ini_set(g_cfg.game, gs, name, norm);
        return;
    }
    {
        applied_t *a = find_applied(sec, name);
        if (a != NULL)
            snprintf(a->global, sizeof(a->global), "%s", norm);        /* the live value stays the game's */
        else
            set_mem(h, name, type, norm);
    }
    mark_dirty(sec);
}

void profile_reset(const char *sec, const char *name)
{
    char gs[64];
    if (g_cfg.game == NULL)
        return;
    game_section(sec, gs, sizeof(gs));
    if (ini_remove(g_cfg.game, gs, name))
        touch(sec, name);
}

void profile_reset_all(void)
{
    int i;
    struct { char sec[48]; char key[48]; } list[MAX_APPLIED + 64];
    int n = 0;
    const char *sec, *key;

    if (g_cfg.game == NULL)
        return;
    for (i = 0; ini_entry(g_cfg.game, i, &sec, &key, NULL) && n < (int) (sizeof(list) / sizeof(list[0])); i++)
        if (strncmp(sec, "cfg:", 4) == 0)
        {
            snprintf(list[n].sec, sizeof(list[n].sec), "%s", sec + 4);
            snprintf(list[n].key, sizeof(list[n].key), "%s", key);
            n++;
        }
    for (i = 0; i < n; i++)
        profile_reset(list[i].sec, list[i].key);
}

int profile_pending(void)
{
    int i, j, n = 0;
    for (i = 0; i < l_ntouched; i++)
    {
        char after[256];
        if (!profile_get_game(l_touched[i].sec, l_touched[i].key, after, sizeof(after)) &&
            !profile_get_global(l_touched[i].sec, l_touched[i].key, after, sizeof(after)))
            continue;
        for (j = 0; j < l_nsnap; j++)
            if (strcasecmp(l_snap[j].sec, l_touched[i].sec) == 0 && strcasecmp(l_snap[j].key, l_touched[i].key) == 0)
            {
                if (strcmp(l_snap[j].val, after) != 0)
                    n++;
                break;
            }
    }
    return n;
}

int profile_entry_count(void)
{
    return g_cfg.game != NULL ? ini_count_prefix(g_cfg.game, "cfg:") : 0;
}

/* ---- audio volume ------------------------------------------------------------------------------------------ */

int profile_volume_now(void)
{
    int v = -1;
    if ((*CoreDoCommand)(M64CMD_CORE_STATE_QUERY, M64CORE_AUDIO_VOLUME, &v) != M64ERR_SUCCESS)
        return -1;
    return v;
}

static int l_vol_initial = -2;       /* the volume the audio plugin started with (-2: not asked yet) */

void profile_apply_volume(void)
{
    int v = cfg_get_int("audio", "volume", -1);
    if (l_vol_initial == -2)
        l_vol_initial = profile_volume_now();
    if (v < 0)
    {
        /* "plugin default": go back to what the plugin started with (after a reset in the menu) */
        v = l_vol_initial;
        if (v < 0 || v == profile_volume_now())
            return;
    }
    if (v > 100)
        v = 100;
    if ((*CoreDoCommand)(M64CMD_CORE_STATE_SET, M64CORE_AUDIO_VOLUME, &v) == M64ERR_SUCCESS)
        DebugMessage(M64MSG_INFO, "audio volume set to %d%% (profile); the audio plugin reports %d%%", v, profile_volume_now());
}
