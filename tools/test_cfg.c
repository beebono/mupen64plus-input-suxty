/* Unit tests for the config layers (src/cfg.c + src/inifile.c): lookups through the game layer, what a game cannot
 * override, writes by scope, "reset to global" (deleting the game entry), saving and re-loading the game file, and an
 * emptied game file being deleted.
 *
 *   make -C projects/unix check          GPLv2+, like the rest of the project.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cfg.h"
#include "osal_files.h"

static int l_fail, l_checks;

#define CHECK(cond) do { l_checks++; if (!(cond)) { l_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define STREQ(a, b) (strcmp((a), (b)) == 0)

/* the pieces of the front-end cfg.c calls into */
void DebugMessage(int level, const char *message, ...) { (void) level; (void) message; }
void toast_set_enabled(int e) { (void) e; }
void toast_set_duration_ms(int ms) { (void) ms; }
void inputfilter_bypass(int on) { (void) on; }
void inputfilter_after_update(void) { }
void inputfilter_configure(int block, uint32_t hot, uint32_t all) { (void) block; (void) hot; (void) all; }

static void ini_roundtrip_tests(void)
{
    ini_t *i = ini_new();
    ini_set(i, "a", "x", "1");
    ini_set(i, "a", "y", "2");
    ini_set(i, "b", "x", "3");
    CHECK(ini_count(i) == 3);
    CHECK(ini_has_section(i, "a") && !ini_has_section(i, "c"));
    CHECK(ini_count_prefix(i, "a") == 2);
    CHECK(ini_remove(i, "a", "x") == 1);
    CHECK(ini_remove(i, "a", "x") == 0);
    CHECK(ini_count(i) == 2 && STREQ(ini_get(i, "a", "y", ""), "2") && STREQ(ini_get(i, "b", "x", ""), "3"));
    {
        const char *s, *k, *v;
        CHECK(ini_entry(i, 0, &s, &k, &v) && STREQ(s, "a") && STREQ(k, "y") && STREQ(v, "2"));
        CHECK(!ini_entry(i, 2, &s, &k, &v));
    }
    CHECK(ini_remove_section(i, "a") == 1 && ini_count(i) == 1);
    ini_free(i);
}

int main(void)
{
    char dir[] = "/tmp/suxty-test-cfg-XXXXXX";
    char path[512];
    const char *md5 = "753437D0D8ADA1D12F3F9CF0F0A5171F";

    ini_roundtrip_tests();

    if (mkdtemp(dir) == NULL)
    {
        perror("mkdtemp");
        return 2;
    }
    CHECK(cfg_init(dir) == 0);
    CHECK(!cfg_game_available());
    CHECK(g_cfg.ff_speed == 300 && g_cfg.autosave_on_quit == 0);        /* auto-save on quit is opt-in */
    CHECK(g_cfg.confirm_quit == 1 && g_cfg.soft_tilt_pct == 50);        /* Confirm Quit is on, Soft Tilt halves the stick */
    cfg_set("input", "soft_tilt_scale", "0.25");  CHECK(g_cfg.soft_tilt_pct == 25);
    cfg_set("input", "soft_tilt_scale", "1.5");   CHECK(g_cfg.soft_tilt_pct == 100);
    cfg_set("input", "soft_tilt_scale", "-1");    CHECK(g_cfg.soft_tilt_pct == 0);
    cfg_set("input", "soft_tilt_scale", "half");  CHECK(g_cfg.soft_tilt_pct == 50);          /* not a number: the default */
    cfg_set("input", "soft_tilt_scale", "0.5");
    cfg_set_bool("states", "autosave_on_quit", 1);
    CHECK(g_cfg.autosave_on_quit == 1);

    /* no game layer yet: writes in GAME scope fall back to the global layer */
    cfg_set_scope(CFG_SCOPE_GAME);
    CHECK(g_cfg.scope == CFG_SCOPE_GLOBAL);

    CHECK(cfg_load_game(md5) == 0 && cfg_game_available());
    CHECK(STREQ(g_cfg.game_md5, "753437d0d8ada1d12f3f9cf0f0a5171f"));      /* lower case, as the file name */
    snprintf(path, sizeof(path), "%s/suxty/games/753437d0d8ada1d12f3f9cf0f0a5171f.cfg", dir);
    CHECK(strcmp(g_cfg.game_path, path) == 0);
    CHECK(cfg_game_override_count() == 0);

    /* global write, then a game override on top */
    CHECK(!cfg_key_overridable("menu", "recovery_combo") && !cfg_key_overridable("menu", "dim_percent") &&
          cfg_key_overridable("hotkeys", "quick_save") && cfg_key_overridable("states", "autosave_on_quit"));
    cfg_set_int("hotkeys", "fast_forward_speed", 400);
    CHECK(g_cfg.ff_speed == 400 && !cfg_overridden("hotkeys", "fast_forward_speed"));
    cfg_set_scope(CFG_SCOPE_GAME);
    CHECK(g_cfg.scope == CFG_SCOPE_GAME);
    cfg_set_int("hotkeys", "fast_forward_speed", 150);
    cfg_set_bool("states", "autosave_on_quit", 0);
    CHECK(g_cfg.ff_speed == 150 && g_cfg.autosave_on_quit == 0);
    CHECK(cfg_overridden("hotkeys", "fast_forward_speed") && cfg_game_override_count() == 2);
    /* the global layer is untouched */
    CHECK(ini_get_int(g_cfg.ini, "hotkeys", "fast_forward_speed", 0) == 400 && ini_get_bool(g_cfg.ini, "states", "autosave_on_quit", 0) == 1);
    /* scoped views */
    CHECK(cfg_get_scoped_int("hotkeys", "fast_forward_speed", 0) == 150);
    cfg_set_scope(CFG_SCOPE_GLOBAL);
    CHECK(cfg_get_scoped_int("hotkeys", "fast_forward_speed", 0) == 400);
    CHECK(cfg_get_int("hotkeys", "fast_forward_speed", 0) == 150);        /* what the game plays with */
    /* a global edit of an overridden key does not change the game */
    cfg_set_int("hotkeys", "fast_forward_speed", 500);
    CHECK(g_cfg.ff_speed == 150 && ini_get_int(g_cfg.ini, "hotkeys", "fast_forward_speed", 0) == 500);

    /* things a game cannot override go to the global layer even in GAME scope; a hand-edited game file is ignored too */
    cfg_set_scope(CFG_SCOPE_GAME);
    cfg_set_int("menu", "dim_percent", 20);
    CHECK(ini_get_int(g_cfg.ini, "menu", "dim_percent", 0) == 20 && !ini_has(g_cfg.game, "menu", "dim_percent"));
    ini_set(g_cfg.game, "menu", "recovery_combo", "none");
    cfg_apply();
    CHECK(g_cfg.recovery_combo != 0);
    ini_remove(g_cfg.game, "menu", "recovery_combo");

    /* combos: a game hotkey */
    cfg_set("hotkeys", "quick_save", "hotkey+x");
    CHECK(cfg_overridden("hotkeys", "quick_save") && g_cfg.hk_quick_save != 0 &&
          g_cfg.hk_quick_save == cfg_combo_mask("select+x", g_cfg.hotkey_button));

    /* save, reload, and reset to global */
    CHECK(cfg_save() == 0 && osal_file_exists(g_cfg.game_path));
    CHECK(cfg_load_game(md5) == 0 && cfg_game_override_count() == 3);
    CHECK(g_cfg.ff_speed == 150 && g_cfg.autosave_on_quit == 0);
    cfg_set_scope(CFG_SCOPE_GAME);
    cfg_reset("hotkeys", "fast_forward_speed");
    CHECK(g_cfg.ff_speed == 500 && !cfg_overridden("hotkeys", "fast_forward_speed"));
    CHECK(cfg_reset_all_game() == 2 && g_cfg.autosave_on_quit == 1);
    CHECK(cfg_save() == 0 && !osal_file_exists(g_cfg.game_path));       /* nothing left: the file goes away */
    CHECK(osal_file_exists(g_cfg.cfg_path));

    /* a different ROM has its own, separate layer */
    CHECK(cfg_load_game("0123456789abcdef0123456789abcdef") == 0 && cfg_game_override_count() == 0);
    CHECK(cfg_load_game("xyz") != 0 && !cfg_game_available());

    cfg_shutdown();

    /* an suxty.cfg and a game file from the front-end build: removed keys are dropped, the rest is kept, no error */
    {
        char dir2[] = "/tmp/suxty-test-cfg2-XXXXXX", gpath[600];
        FILE *f;
        if (mkdtemp(dir2) == NULL)
            return 2;
        snprintf(path, sizeof(path), "%s/suxty", dir2);
        osal_mkdir_p(path);
        snprintf(path, sizeof(path), "%s/suxty/suxty.cfg", dir2);
        f = fopen(path, "w");
        fputs("[general]\nversion = 1\n[input]\nremap_input = false\nswap_ab = true\n[states]\nautosave_on_quit = true\n"
              "[hotkeys]\nfast_forward_speed = 250\nsome_future_key = 1\n[UI-Suxty]\nx = 1\n", f);
        fclose(f);
        snprintf(gpath, sizeof(gpath), "%s/suxty/games", dir2);
        osal_mkdir_p(gpath);
        snprintf(gpath, sizeof(gpath), "%s/suxty/games/753437d0d8ada1d12f3f9cf0f0a5171f.cfg", dir2);
        f = fopen(gpath, "w");
        fputs("[plugins]\nvideo = mupen64plus-video-GLideN64.so\n[cfg:Video-Foir]\nThreads = 3\n[cfg:Core]\nR4300Emulator = 1\n"
              "[hotkeys]\nquick_save = hotkey+x\n[audio]\nvolume = 60\n", f);
        fclose(f);
        CHECK(cfg_init(dir2) == 0);
        CHECK(!ini_has(g_cfg.ini, "input", "remap_input") && !ini_has_section(g_cfg.ini, "UI-Suxty"));
        CHECK(g_cfg.swap_ab == 1 && g_cfg.ff_speed == 250 && ini_get_int(g_cfg.ini, "general", "version", 0) == CFG_VERSION);
        CHECK(g_cfg.autosave_on_quit == 0);                              /* an old "true" meant something else: back to the new default */
        CHECK(ini_has(g_cfg.ini, "hotkeys", "some_future_key"));       /* unknown keys are kept */
        CHECK(cfg_load_game(md5) == 0);
        CHECK(!ini_has_section(g_cfg.game, "plugins") && ini_count_prefix(g_cfg.game, "cfg:") == 0);
        CHECK(cfg_game_override_count() == 2 && cfg_get_int("audio", "volume", 0) == 60 && g_cfg.hk_quick_save != 0);
        CHECK(cfg_save() == 0);
        cfg_shutdown();
        /* what was written no longer has them */
        CHECK(cfg_init(dir2) == 0 && cfg_load_game(md5) == 0 && cfg_game_override_count() == 2 && !ini_has(g_cfg.ini, "input", "remap_input"));
        cfg_shutdown();
    }

    /* the menu refinement pass: hints are always on (show_hints is gone), "Quit hold" became "Confirm Quit" (a hold of 0 = off,
     * any other = on, an explicit confirm_quit wins), and the pad sections' modifier layer became the C-Face shift - in the
     * global file and in a game file alike, written back clean */
    {
        char dir3[] = "/tmp/suxty-test-cfg3-XXXXXX", gpath[600];
        FILE *f;
        if (mkdtemp(dir3) == NULL)
            return 2;
        snprintf(path, sizeof(path), "%s/suxty", dir3);
        osal_mkdir_p(path);
        snprintf(path, sizeof(path), "%s/suxty/suxty.cfg", dir3);
        f = fopen(path, "w");
        fputs("[general]\nversion = 2\n[menu]\nshow_hints = false\ndim_percent = 30\n[hotkeys]\nquit_hold_ms = 0\n"
              "[pad:one]\npreset = Face buttons = C\nmodifier = button:7\nmod_c_up = button:3\nmod_c_down = button:0\na = button:0\n"
              "[pad:two]\nmodifier = button:6\nmod_a = button:4\n", f);
        fclose(f);
        snprintf(gpath, sizeof(gpath), "%s/suxty/games", dir3);
        osal_mkdir_p(gpath);
        snprintf(gpath, sizeof(gpath), "%s/suxty/games/753437d0d8ada1d12f3f9cf0f0a5171f.cfg", dir3);
        f = fopen(gpath, "w");
        fputs("[hotkeys]\nquit_hold_ms = 2000\n[pad:one]\nmodifier = button:6\nmod_c_up = button:2\n[pad:two]\nmodifier = button:6\nmod_a = button:4\n", f);
        fclose(f);
        CHECK(cfg_init(dir3) == 0);
        CHECK(!ini_has(g_cfg.ini, "menu", "show_hints") && ini_get_int(g_cfg.ini, "menu", "dim_percent", 0) == 30);
        CHECK(!ini_has(g_cfg.ini, "hotkeys", "quit_hold_ms") && ini_has(g_cfg.ini, "hotkeys", "confirm_quit") && g_cfg.confirm_quit == 0);
        CHECK(STREQ(ini_get(g_cfg.ini, "pad:one", "cface", ""), "button:7") && STREQ(ini_get(g_cfg.ini, "pad:one", "cface_c_up", ""), "button:3"));
        CHECK(!ini_has(g_cfg.ini, "pad:one", "modifier") && !ini_has(g_cfg.ini, "pad:one", "preset") && !ini_has(g_cfg.ini, "pad:one", "mod_c_up"));
        CHECK(!ini_has(g_cfg.ini, "pad:two", "modifier") && !ini_has(g_cfg.ini, "pad:two", "mod_a") && !ini_has(g_cfg.ini, "pad:two", "cface"));
        CHECK(STREQ(ini_get(g_cfg.ini, "pad:one", "a", ""), "button:0"));
        CHECK(cfg_load_game(md5) == 0);
        CHECK(!ini_has(g_cfg.game, "hotkeys", "quit_hold_ms") && ini_get_bool(g_cfg.game, "hotkeys", "confirm_quit", 0) == 1 && g_cfg.confirm_quit == 1);
        CHECK(STREQ(ini_get(g_cfg.game, "pad:one", "cface", ""), "button:6") && STREQ(ini_get(g_cfg.game, "pad:one", "cface_c_up", ""), "button:2"));
        CHECK(!ini_has_section(g_cfg.game, "pad:two"));
        CHECK(cfg_save() == 0);
        cfg_shutdown();
        CHECK(cfg_init(dir3) == 0 && cfg_load_game(md5) == 0);          /* what was written is clean and stable */
        CHECK(g_cfg.confirm_quit == 1 && !ini_has(g_cfg.game, "pad:one", "modifier") && !ini_has(g_cfg.ini, "hotkeys", "quit_hold_ms"));
        /* an explicit Confirm Quit wins over a leftover hold time */
        ini_set(g_cfg.ini, "hotkeys", "confirm_quit", "false");
        ini_set(g_cfg.ini, "hotkeys", "quit_hold_ms", "1000");
        cfg_shutdown();
        cfg_init(dir3);
        CHECK(cfg_save() == 0);
        cfg_shutdown();
        {
            ini_t *i = ini_new();
            CHECK(ini_load(i, g_cfg.cfg_path) == 0);
            CHECK(ini_get_bool(i, "hotkeys", "confirm_quit", 1) == 0);
            ini_free(i);
        }
        /* "Save Game Profile": settings counted as the user sees them, and the global values pinned (not the hotkey button / menu combo) */
        CHECK(cfg_init(dir3) == 0 && cfg_load_game(md5) == 0);
        ini_set(g_cfg.game, "pad:x", "a", "button:1");
        ini_set(g_cfg.game, "pad:x", "b", "button:2");
        ini_set(g_cfg.game, "pad:x", "cface", "none");
        ini_set(g_cfg.game, "players", "p1", "auto");
        ini_set(g_cfg.game, "players", "p2", "none");
        ini_set(g_cfg.game, "foir", "Widescreen", "true");
        {
            int before = cfg_game_setting_count(), n;
            /* confirm_quit, pad:one (2 keys), pad:x (3 keys), the players (2 keys), Widescreen: 9 keys, 5 settings */
            CHECK(cfg_game_override_count() == 9 && before == 5);
            n = cfg_pin_profile();
            CHECK(n > 0 && ini_has(g_cfg.game, "hotkeys", "quick_save") && ini_has(g_cfg.game, "hotkeys", "fast_forward_speed") &&
                  ini_has(g_cfg.game, "hotkeys", "confirm_quit") && ini_has(g_cfg.game, "input", "soft_tilt_scale") &&
                  ini_has(g_cfg.game, "states", "autosave_on_quit"));
            CHECK(!ini_has(g_cfg.game, "hotkeys", "hotkey_button") && !ini_has(g_cfg.game, "menu", "menu_combo") && !ini_has(g_cfg.game, "menu", "dim_percent"));
            CHECK(cfg_pin_profile() == 0);                                   /* nothing more to pin */
            CHECK(cfg_game_setting_count() == before + n);
            CHECK(g_cfg.confirm_quit == 1);                                  /* the pinned value is what is in force */
        }
        cfg_shutdown();
    }
    printf("%d checks, %d failed\n", l_checks, l_fail);
    return l_fail ? 1 : 0;
}
