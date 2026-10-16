/* Unit test for src/optif.c against tools/fake_foir.c and tools/fake_altru.c: discovery in a plugin loaded RTLD_LOCAL under an arbitrary
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
#include "optif.h"
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
    char dir[] = "/tmp/suxty-test-optif-XXXXXX", odd[600], odda[600];
    void *h;
    void (*fake_reset)(int, int, int);
    int (*fake_sets)(void), (*fake_staged)(int), (*fake_default)(int);
    void (*fake_set_abi)(int);
    char text[48];

    if (argc < 3 || mkdtemp(dir) == NULL)
        return 2;
    CHECK(cfg_init(dir) == 0 && cfg_load_game("00112233445566778899aabbccddeeff") == 0);

    CHECK(optif_discover(&g_foir) == 0 && g_foir.iface == NULL && optif_count(&g_foir) == 0);

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
    fake_set_abi(g_foir.abi + 1);
    CHECK(optif_discover(&g_foir) == 0 && g_foir.iface == NULL);
    fake_set_abi(g_foir.abi);
    CHECK(optif_discover(&g_foir) == 1 && g_foir.iface != NULL && optif_count(&g_foir) == 3);
    CHECK(optif_discover(&g_foir) == 1);
    CHECK(STREQ(optif_option(&g_foir, 0)->key, "Widescreen") && (optif_option(&g_foir, 0)->flags & OPTF_LIVE) && !(optif_option(&g_foir, 1)->flags & OPTF_LIVE));
    CHECK(optif_option(&g_foir, 3) == NULL);

    /* foir's own values: Widescreen off, Mode "Fast" (1), Level 4 */
    fake_reset(0, 1, 4);
    CHECK(optif_shown(&g_foir, 0) == 0 && optif_shown(&g_foir, 1) == 1 && optif_shown(&g_foir, 2) == 4);
    optif_apply_config(&g_foir); CHECK(fake_sets() == 0);

    CHECK(optif_set(&g_foir, 0, 1) == OPT_APPLIED && optif_shown(&g_foir, 0) == 1 && fake_staged(0) == 1);
    CHECK(STREQ(cfg_get("foir", "Widescreen", ""), "true") && !optif_overridden(&g_foir, 0));
    CHECK(optif_set(&g_foir, 1, 2) == OPT_NEXT_LAUNCH && STREQ(cfg_get("foir", "Mode", ""), "Faster") && optif_pending(&g_foir, 1));
    CHECK(optif_set(&g_foir, 2, 7) == OPT_APPLIED && STREQ(cfg_get("foir", "Level", ""), "7"));
    CHECK(!ini_has(g_cfg.game, "foir", "Widescreen"));
    optif_value_text(&g_foir, 1, 0, text, sizeof(text));
    CHECK(STREQ(text, "Off"));

    /* its own value wins; a later global edit does not change the running game */
    cfg_set_scope(CFG_SCOPE_GAME);
    CHECK(optif_set(&g_foir, 0, 0) == OPT_APPLIED && optif_overridden(&g_foir, 0) && fake_staged(0) == 0);
    cfg_set_scope(CFG_SCOPE_GLOBAL);
    CHECK(optif_shown(&g_foir, 0) == 1);
    CHECK(optif_set(&g_foir, 2, 3) == OPT_APPLIED && optif_set(&g_foir, 0, 1) == OPT_APPLIED && fake_staged(0) == 0);
    cfg_set_scope(CFG_SCOPE_GAME);
    CHECK(optif_shown(&g_foir, 0) == 0);
    optif_reset(&g_foir, 0);
    CHECK(!optif_overridden(&g_foir, 0) && fake_staged(0) == 1);

    /* every stored value goes to foir before it opens the ROM (also the next-launch ones) */
    fake_reset(0, 1, 4);
    optif_apply_config(&g_foir);
    CHECK(fake_staged(0) == 1 && fake_staged(1) == 2 && fake_staged(2) == 3);

    ini_set(g_cfg.game, "foir", "Mode", "fast");
    ini_set(g_cfg.game, "foir", "Level", "99");                          /* out of range: ignored */
    ini_set(g_cfg.game, "foir", "Widescreen", "banana");
    fake_reset(0, 0, 4);
    optif_apply_config(&g_foir);
    CHECK(fake_staged(1) == 1 && fake_staged(2) == 3 && fake_staged(0) == 1);

    cfg_set_scope(CFG_SCOPE_GLOBAL);
    CHECK(optif_save_defaults(&g_foir) == 0 && fake_default(0) == 1 && fake_default(1) == 2 && fake_default(2) == 3);

    optif_release(&g_foir);
    CHECK(g_foir.iface == NULL && optif_count(&g_foir) == 0);

    /* altru: Volume is stored as [audio] volume, steps of 5 */
    {
        void *ha;
        void (*a_reset)(int, int);
        int (*a_staged)(int), (*a_default)(int);

        snprintf(odda, sizeof(odda), "%s/libaudiothing-%d.so", dir, (int) getpid());
        CHECK(copy_file(argv[2], odda) == 0);
        ha = dlopen(odda, RTLD_NOW | RTLD_LOCAL);
        CHECK(ha != NULL);
        if (ha == NULL)
            return 1;
        *(void **) (&a_reset) = dlsym(ha, "fake_reset");
        *(void **) (&a_staged) = dlsym(ha, "fake_staged");
        *(void **) (&a_default) = dlsym(ha, "fake_default");
        cfg_set_scope(CFG_SCOPE_GLOBAL);
        CHECK(optif_discover(&g_altru) == 1 && optif_count(&g_altru) == 2 && g_altru.iface != NULL);
        CHECK(STREQ(optif_key(&g_altru, 0), "volume") && STREQ(optif_key(&g_altru, 1), "FF_MODE"));
        CHECK(optif_shown(&g_altru, 0) == 80 && optif_shown(&g_altru, 1) == 0);

        ini_set(g_cfg.game, "audio", "volume", "63");                    /* off the step: snapped */
        ini_set(g_cfg.game, "audio", "FF_MODE", "muted");
        optif_apply_config(&g_altru);
        CHECK(a_staged(0) == 65 && a_staged(1) == 1);
        cfg_set_scope(CFG_SCOPE_GAME);
        CHECK(optif_shown(&g_altru, 0) == 65 && optif_overridden(&g_altru, 0));
        CHECK(optif_set(&g_altru, 0, 40) == OPT_APPLIED && a_staged(0) == 40 && STREQ(ini_get(g_cfg.game, "audio", "volume", ""), "40"));
        optif_reset(&g_altru, 0);
        CHECK(!optif_overridden(&g_altru, 0) && a_staged(0) == 80);
        optif_reset(&g_altru, 1);
        CHECK(a_staged(1) == 0);

        ini_remove(g_cfg.game, "audio", "FF_MODE");
        CHECK(optif_pin_game(&g_altru) == 2 && STREQ(ini_get(g_cfg.game, "audio", "volume", ""), "80")
              && STREQ(ini_get(g_cfg.game, "audio", "FF_MODE", ""), "Normal Pitch"));
        cfg_set_scope(CFG_SCOPE_GLOBAL);
        CHECK(optif_save_defaults(&g_altru) == 0 && a_default(0) == 80 && a_default(1) == 0);
        a_reset(80, 0);
        optif_release(&g_altru);
        dlclose(ha);
        unlink(odda);
    }
    dlclose(h);
    unlink(odd);
    cfg_shutdown();
    printf("%d checks, %d failed\n", l_checks, l_fail);
    return l_fail ? 1 : 0;
}
