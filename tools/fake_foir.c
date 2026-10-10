/* stand-in for foir's suxty interface (src/foir_suxty.h) for test_foir_if. GPLv2+, like the rest of the project.
 * gcc -shared -fPIC -I../src -o fake_foir.so fake_foir.c */
#include <string.h>

#include "foir_suxty.h"

static const char *const l_labels[] = { "Off", "Fast", "Faster" };

static const struct foir_suxty_option l_opts[3] = {
    { "Widescreen", "Widescreen", "bool, live", FOIR_OPT_BOOL, FOIR_OPTF_LIVE, 0, 1, 1, NULL, 0 },
    { "Mode", "Mode", "enum, next launch", FOIR_OPT_ENUM, 0, 0, 2, 1, l_labels, 1 },
    { "Level", "Level", "int, live", FOIR_OPT_INT, FOIR_OPTF_LIVE, -2, 10, 2, NULL, 4 },
};

static int l_val[3] = { 0, 1, 4 };        /* what get() returns; Widescreen / Level change at once, Mode does not */
static int l_staged[3] = { 0, 1, 4 };
static int l_sets, l_abi = FOIR_SUXTY_ABI, l_defaults[3] = { -1, -1, -1 };

static int f_get(unsigned i, int *v)
{
    if (i >= 3) return FOIR_SUXTY_E_INDEX;
    *v = l_val[i];
    return 0;
}

static int f_set(unsigned i, int v)
{
    if (i >= 3) return FOIR_SUXTY_E_INDEX;
    if (v < l_opts[i].min || v > l_opts[i].max) return FOIR_SUXTY_E_RANGE;
    l_sets++;
    l_staged[i] = v;
    if (l_opts[i].flags & FOIR_OPTF_LIVE) { l_val[i] = v; return FOIR_SUXTY_APPLIED; }
    return FOIR_SUXTY_NEXT_LAUNCH;
}

static int f_save_default(unsigned i, int v)
{
    if (i >= 3) return FOIR_SUXTY_E_INDEX;
    l_defaults[i] = v;
    return 0;
}

static struct foir_suxty_interface l_if = {
    FOIR_SUXTY_ABI, sizeof(struct foir_suxty_interface), 0x000100, 3, l_opts, f_get, f_set, f_save_default
};

const struct foir_suxty_interface *FoirSuxtyGetInterface(void)
{
    l_if.abi = (uint32_t) l_abi;
    return &l_if;
}

void fake_reset(int w, int m, int l) { l_val[0] = l_staged[0] = w; l_val[1] = l_staged[1] = m; l_val[2] = l_staged[2] = l; l_sets = 0; }
int  fake_sets(void) { return l_sets; }
int  fake_staged(int i) { return l_staged[i]; }
int  fake_default(int i) { return l_defaults[i]; }
void fake_set_abi(int abi) { l_abi = abi; }
