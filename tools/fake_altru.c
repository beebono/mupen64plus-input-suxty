/* stand-in for altru's suxty interface (src/altru_suxty.h) for test_optif. GPLv2+, like the rest of the project.
 * gcc -shared -fPIC -I../src -o fake_altru.so fake_altru.c */
#include <string.h>

#include "altru_suxty.h"

static const char *const l_labels[] = { "Normal Pitch", "Muted" };

static const struct altru_suxty_option l_opts[2] = {
    { "VOLUME_DEFAULT", "Volume", "int, live, step 5", ALTRU_OPT_INT, ALTRU_OPTF_LIVE, 0, 100, 5, NULL, 80 },
    { "FF_MODE", "Fast-Forward Audio", "enum, next launch", ALTRU_OPT_ENUM, 0, 0, 1, 1, l_labels, 0 },
};

static int l_val[2] = { 80, 0 }, l_staged[2] = { 80, 0 }, l_defaults[2] = { -1, -1 };

static int a_get(unsigned i, int *v)
{
    if (i >= 2) return ALTRU_SUXTY_E_INDEX;
    *v = l_val[i];
    return 0;
}

static int a_set(unsigned i, int v)
{
    if (i >= 2) return ALTRU_SUXTY_E_INDEX;
    if (v < l_opts[i].min || v > l_opts[i].max || (i == 0 && v % 5)) return ALTRU_SUXTY_E_RANGE;
    l_staged[i] = v;
    if (l_opts[i].flags & ALTRU_OPTF_LIVE) { l_val[i] = v; return ALTRU_SUXTY_APPLIED; }
    return ALTRU_SUXTY_NEXT_LAUNCH;
}

static int a_save_default(unsigned i, int v)
{
    if (i >= 2) return ALTRU_SUXTY_E_INDEX;
    l_defaults[i] = v;
    return 0;
}

static const struct altru_suxty_interface l_if = {
    ALTRU_SUXTY_ABI, sizeof(struct altru_suxty_interface), 0x000100, 2, l_opts, a_get, a_set, a_save_default
};

const struct altru_suxty_interface *AltruSuxtyGetInterface(void) { return &l_if; }

void fake_reset(int v, int m) { l_val[0] = l_staged[0] = v; l_val[1] = l_staged[1] = m; }
int  fake_staged(int i) { return l_staged[i]; }
int  fake_default(int i) { return l_defaults[i]; }
