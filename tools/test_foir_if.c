/* Unit test for src/foir_if.c against tools/fake_foir.c: discovery in a plugin loaded RTLD_LOCAL under an arbitrary
 * file name,
 * the ABI check, option layering ([foir] in the game file over suxty.cfg over foir's own value).
 * make -C projects/unix check          GPLv2+, like the rest of the project. */
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cfg.h"
#include "foir_if.h"
#include "osal_files.h"

static int l_fail, l_checks;

#define CHECK(cond) do { l_checks++; if (!(cond)) { l_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define STREQ(a, b) (strcmp((a), (b)) == 0)

void DebugMessage(int level, const char *message, ...) { (void) level; (void) message; }
void toast_set_enabled(int e) { (void) e; }
void toast_set_duration_ms(int ms) { (void) ms; }
void inputfilter_bypass(int on) { (void) on; }
void inputfilter_after_update(void) { }
void inputfilter_configure(int block, uint32_t hot, uint32_t all) { (void) block; (void) hot; (void) all; }

static int copy_file(const char *from, const char *to)
{
    char cmd[1200];
    snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", from, to);
    return system(cmd);
}

int main(int argc, char **argv)
{
    char dir[] = "/tmp/suxty-test-foir-XXXXXX", odd[600];
    void *h;
    void (*fake_reset)(int, int, int);
    int (*fake_sets)(void), (*fake_staged)(int), (*fake_default)(int);
    void (*fake_set_abi)(int);
    char text[48];

    if (argc < 2 || mkdtemp(dir) == NULL)
        return 2;
    CHECK(cfg_init(dir) == 0 && cfg_load_game("00112233445566778899aabbccddeeff") == 0);

    CHECK(fif_discover() == 0 && fif_get() == NULL && fif_count() == 0);

    /* loaded RTLD_LOCAL (so dlsym(RTLD_DEFAULT) cannot see it), under a file name that says nothing */
    snprintf(odd, sizeof(odd), "%s/libwhatever-%d.so", dir, (int) getpid());
    CHECK(copy_file(argv[1], odd) == 0);
    h = dlopen(odd, RTLD_NOW | RTLD_LOCAL);
    CHECK(h != NULL);
    if (h == NULL)
        return 1;
    CHECK(dlsym(RTLD_DEFAULT, "FoirSuxtyGetInterface") == NULL);
    *(void **) (&fake_reset) = dlsym(h, "fake_reset");
    *(void **) (&fake_sets) = dlsym(h, "fake_sets");
    *(void **) (&fake_staged) = dlsym(h, "fake_staged");
    *(void **) (&fake_default) = dlsym(h, "fake_default");
    *(void **) (&fake_set_abi) = dlsym(h, "fake_set_abi");

    /* another ABI version counts as absent */
    fake_set_abi(FOIR_SUXTY_ABI + 1);
    CHECK(fif_discover() == 0 && fif_get() == NULL);
    fake_set_abi(FOIR_SUXTY_ABI);
    CHECK(fif_discover() == 1 && fif_get() != NULL && fif_count() == 3);
    CHECK(fif_discover() == 1);
    CHECK(STREQ(fif_option(0)->key, "Widescreen") && (fif_option(0)->flags & FOIR_OPTF_LIVE) && !(fif_option(1)->flags & FOIR_OPTF_LIVE));
    CHECK(fif_option(3) == NULL);

    /* foir's own values: Widescreen off, Mode "Fast" (1), Level 4 */
    fake_reset(0, 1, 4);
    CHECK(fif_shown(0) == 0 && fif_shown(1) == 1 && fif_shown(2) == 4);
    fif_apply_config(); CHECK(fake_sets() == 0);

    CHECK(fif_set(0, 1) == FOIR_SUXTY_APPLIED && fif_shown(0) == 1 && fake_staged(0) == 1);
    CHECK(STREQ(cfg_get("foir", "Widescreen", ""), "true") && !fif_overridden(0));
    CHECK(fif_set(1, 2) == FOIR_SUXTY_NEXT_LAUNCH && STREQ(cfg_get("foir", "Mode", ""), "Faster") && fif_pending(1));
    CHECK(fif_set(2, 7) == FOIR_SUXTY_APPLIED && STREQ(cfg_get("foir", "Level", ""), "7"));
    CHECK(!ini_has(g_cfg.game, "foir", "Widescreen"));
    fif_value_text(1, 0, text, sizeof(text));
    CHECK(STREQ(text, "Off"));

    /* its own value wins; a later global edit does not change the running game */
    cfg_set_scope(CFG_SCOPE_GAME);
    CHECK(fif_set(0, 0) == FOIR_SUXTY_APPLIED && fif_overridden(0) && fake_staged(0) == 0);
    cfg_set_scope(CFG_SCOPE_GLOBAL);
    CHECK(fif_shown(0) == 1);
    CHECK(fif_set(2, 3) == FOIR_SUXTY_APPLIED && fif_set(0, 1) == FOIR_SUXTY_APPLIED && fake_staged(0) == 0);
    cfg_set_scope(CFG_SCOPE_GAME);
    CHECK(fif_shown(0) == 0);
    fif_reset(0);
    CHECK(!fif_overridden(0) && fake_staged(0) == 1);

    /* every stored value goes to foir before it opens the ROM (also the next-launch ones) */
    fake_reset(0, 1, 4);
    fif_apply_config();
    CHECK(fake_staged(0) == 1 && fake_staged(1) == 2 && fake_staged(2) == 3);

    ini_set(g_cfg.game, "foir", "Mode", "fast");
    ini_set(g_cfg.game, "foir", "Level", "99");                          /* out of range: ignored */
    ini_set(g_cfg.game, "foir", "Widescreen", "banana");
    fake_reset(0, 0, 4);
    fif_apply_config();
    CHECK(fake_staged(1) == 1 && fake_staged(2) == 3 && fake_staged(0) == 1);

    cfg_set_scope(CFG_SCOPE_GLOBAL);
    CHECK(fif_save_defaults() == 0 && fake_default(0) == 1 && fake_default(1) == 2 && fake_default(2) == 3);

    fif_release();
    CHECK(fif_get() == NULL && fif_count() == 0);
    dlclose(h);
    unlink(odd);
    cfg_shutdown();
    printf("%d checks, %d failed\n", l_checks, l_fail);
    return l_fail ? 1 : 0;
}
