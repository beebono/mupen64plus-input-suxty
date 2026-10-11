/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - session.c
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


#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <SDL.h>

#include "audio.h"
#include "autosave.h"
#include "cfg.h"
#include "core_interface.h"
#include "debug.h"
#include "filewatch.h"
#include "foir_if.h"
#include "gl.h"
#include "inputfilter.h"
#include "m64p_types.h"
#include "main.h"
#include "menu.h"
#include "pad.h"
#include "quitarm.h"
#include "remap.h"
#include "session.h"
#include "signals.h"
#include "states.h"
#include "toast.h"
#include "video.h"

static SDL_atomic_t  l_menu_open;
static SDL_threadID  l_menu_thread;
static int           l_stop_sent;
static uint32_t      l_prev_held;
static unsigned      l_frames;

#define FRAME_MS 16

/* the core runs state commands from its interrupt handler (only while the emulator runs) and saves on a worker
 * thread:
 * fire and forget; a save counts as written when the inotify watch sees the file closed, else "not written" after
 * SAVE_WAIT_MS */
#define SAVE_WAIT_MS 5000u

static struct {
    int             active;
    int             slot;
    int             quit_after;
    int             rotated;
    char            path[PATH_MAX];
    filewatch       fw;
    uint32_t        t0;
} l_save;

/* the toast waits for the first frames, when it can be drawn */
static char         l_startup_toast[96];
static int          l_startup_toast_kind;

static int  l_quitting;
static int  l_sig_quit;                     /* the quit was asked for by SIGUSR1: it ends even if the save fails */
static int  l_load_wait;                    /* swaps left until a load we issued has surely run (the core keeps one job) */
static int  l_ff_on;
static int  l_ff_saved_factor = 100;

static quitarm_t l_quit_arm;

/* M64P_AUTOLOAD: resume from the auto-save at the first frame */
static int          l_autoload_want;
static int          l_autoload_ok;
static char         l_autoload_path[PATH_MAX];

void session_request_autoload(int on)
{
    l_autoload_want = on;
}

int session_menu_blocks_this_thread(void)
{
    return SDL_AtomicGet(&l_menu_open) && SDL_ThreadID() != l_menu_thread;
}

/* the launcher or M64P_AUTOLOAD (first frame, see autoload_run) loads the auto-save, never the menu: check it here and
 * replace a damaged file with the previous one */
static void autosave_startup_check(void)
{
    char path[PATH_MAX], ss[PATH_MAX];
    autosave_report rep;
    int launcher;
    uint32_t t0 = SDL_GetTicks();

    l_startup_toast[0] = 0;
    l_autoload_ok = 0;
    if (states_slot_path(STATES_AUTO_SLOT, path, sizeof(path)) != 0)
        return;
    autosave_reconcile(path, &rep);
    DebugMessage(M64MSG_VERBOSE, "auto-save check took %u ms", (unsigned) (SDL_GetTicks() - t0));
    ss[0] = 0;
    launcher = autosave_cmdline_savestate(ss, sizeof(ss));
    l_autoload_ok = rep.status == AUTOSAVE_GOOD || rep.status == AUTOSAVE_RESTORED;
    snprintf(l_autoload_path, sizeof(l_autoload_path), "%s", path);
    if (launcher && l_autoload_want)
    {
        DebugMessage(M64MSG_INFO, "M64P_AUTOLOAD ignored: the launcher passes --savestate");
        l_autoload_want = 0;
    }
    if (launcher)
        DebugMessage(M64MSG_INFO, "auto-save: the launcher loads '%s' (--savestate)%s", ss, strcmp(ss, path) == 0 ? ", the auto-save" : "");
    switch (rep.status)
    {
        case AUTOSAVE_GOOD:
            DebugMessage(M64MSG_INFO, "auto-save '%s' is valid; %d backup(s) removed", path, rep.removed);
            break;
        case AUTOSAVE_RESTORED:
            DebugMessage(M64MSG_WARNING, "auto-save '%s' was %s (%s); restored the previous one from '%s'", path,
                         rep.was_missing ? "missing" : "damaged", rep.why, rep.from);
            snprintf(l_startup_toast, sizeof(l_startup_toast), rep.was_missing ? "Auto-save was missing;\nprevious one restored"
                                                                               : "Auto-save was damaged;\nprevious one restored");
            l_startup_toast_kind = TOAST_ERROR;
            break;
        case AUTOSAVE_BAD:
            DebugMessage(M64MSG_ERROR, "auto-save '%s' is %s (%s) and no backup is usable", path, rep.was_missing ? "missing" : "damaged", rep.why);
            if (!rep.was_missing)
            {
                snprintf(l_startup_toast, sizeof(l_startup_toast), "Auto-save is damaged");
                l_startup_toast_kind = TOAST_ERROR;
            }
            break;
        case AUTOSAVE_ABSENT:
            break;
    }
}

int session_init(void)
{
    if (pad_init() != 0)
        return -1;
    remap_init();
    autosave_startup_check();
    return 0;
}

static int emu_state(void)
{
    int v = 0;
    if ((*CoreDoCommand)(M64CMD_CORE_STATE_QUERY, M64CORE_EMU_STATE, &v) != M64ERR_SUCCESS)
        return M64EMU_STOPPED;
    return v;
}

static void emu_stop(void)
{
    if (!l_stop_sent)
    {
        l_stop_sent = 1;
        DebugMessage(M64MSG_INFO, "stopping emulation (t=%u ms)", (unsigned) SDL_GetTicks());
        (*CoreDoCommand)(M64CMD_STOP, 0, NULL);
    }
}

static int cur_slot(void)
{
    int v = 0;
    (*CoreDoCommand)(M64CMD_CORE_STATE_QUERY, M64CORE_SAVESTATE_SLOT, &v);
    return v;
}

static void set_slot(int slot)
{
    if (slot >= 0 && slot < STATES_NUM_SLOTS && slot != cur_slot())
        (*CoreDoCommand)(M64CMD_STATE_SET_SLOT, slot, NULL);
}

static void ff_set(int on)
{
    int v;
    if (on == l_ff_on)
        return;
    if (on)
    {
        l_ff_saved_factor = 100;
        if ((*CoreDoCommand)(M64CMD_CORE_STATE_QUERY, M64CORE_SPEED_FACTOR, &v) == M64ERR_SUCCESS && v >= 1)
            l_ff_saved_factor = v;
        if (g_cfg.ff_speed == 0)
        {
            v = 0;
            (*CoreDoCommand)(M64CMD_CORE_STATE_SET, M64CORE_SPEED_LIMITER, &v);
            toast_set_badge(">> MAX");
        }
        else
        {
            char b[24];
            v = g_cfg.ff_speed;
            (*CoreDoCommand)(M64CMD_CORE_STATE_SET, M64CORE_SPEED_FACTOR, &v);
            snprintf(b, sizeof(b), ">> %d%%", g_cfg.ff_speed);
            toast_set_badge(b);
        }
    }
    else
    {
        v = 1;
        (*CoreDoCommand)(M64CMD_CORE_STATE_SET, M64CORE_SPEED_LIMITER, &v);
        v = l_ff_saved_factor;
        (*CoreDoCommand)(M64CMD_CORE_STATE_SET, M64CORE_SPEED_FACTOR, &v);
        toast_set_badge(NULL);
    }
    l_ff_on = on;
    video_set_unthrottled(on);
    /* render only the frames the speed leaves room for: 1 in 2 up to 200 %, 1 in 3 at 300 %, else foir's maximum 1 in 4 */
    fif_ff_frameskip(!on ? 0 : g_cfg.ff_speed == 0 || g_cfg.ff_speed > 300 ? 3 : g_cfg.ff_speed > 200 ? 2 : 1);
    DebugMessage(M64MSG_INFO, "fast-forward %s (frame %u, t=%u ms)", on ? "on" : "off", l_frames, (unsigned) SDL_GetTicks());
}

void session_shutdown(void)
{
    ff_set(0);
    remap_shutdown();
    pad_shutdown();
    if (l_save.active)
        filewatch_stop(&l_save.fw);
    memset(&l_save, 0, sizeof(l_save));
    l_startup_toast[0] = 0;
    l_quitting = 0;
    l_sig_quit = 0;
    l_load_wait = 0;
    l_autoload_want = 0;
    l_stop_sent = 0;
    l_frames = 0;
    l_prev_held = 0;
}

static void save_shot(const uint8_t *rgba, int w, int h)
{
    char path[1100];
    if (rgba != NULL && dbg_next_shot_path(path, sizeof(path)) != NULL)
    {
        if (dbg_save_png(path, rgba, w, h) == 0)
            DebugMessage(M64MSG_INFO, "debug screenshot written: %s (%dx%d)", path, w, h);
        else
            DebugMessage(M64MSG_ERROR, "couldn't write debug screenshot %s", path);
    }
}

/* drawn in suxty's private GL context right before the swap; a debug screenshot is read back from there so it shows
 * them */
static void draw_overlay(int want_shot)
{
    int w, h;
    const uint8_t *shot = NULL;

    if (!video_ui_available())
        return;
    video_drawable_size(&w, &h);
    if (w <= 0 || h <= 0 || video_enter_ui() != 0)
        return;
    ugl_begin_overlay(w, h);
    toast_draw(w, h);
    if (want_shot)
        shot = ugl_readback(w, h);
    ugl_end();
    video_leave_ui();
    if (shot != NULL)
        save_shot(shot, w, h);
}

static int start_save(int slot, int quit_after)
{
    char why[64];
    m64p_error r;

    if (l_save.active)
    {
        toast_show(TOAST_ERROR, "Busy, try again");
        return -1;
    }
    if (states_check_writable(why, sizeof(why)) != 0)
    {
        DebugMessage(M64MSG_ERROR, "can't save a state: %s", why);
        toast_show(TOAST_ERROR, "Can't save: %s", why);
        return -1;
    }
    memset(&l_save, 0, sizeof(l_save));
    l_save.slot = slot;
    l_save.quit_after = quit_after;
    if (states_slot_path(slot, l_save.path, sizeof(l_save.path)) != 0)
        return -1;
    if (slot == STATES_AUTO_SLOT)
    {
        /* the core rewrites the file in place: keep the previous auto-save aside until the next game start has seen
         * that the new one is good */
        int rot = autosave_rotate(l_save.path);
        if (rot > 0)
            l_save.rotated = 1;
        else if (rot < 0)
            DebugMessage(M64MSG_WARNING, "couldn't move the old auto-save aside (%s); writing it in place", strerror(errno));
    }
    filewatch_start(&l_save.fw, l_save.path, 0);                 /* before the command: the worker may be quick */
    if (!filewatch_uses_inotify(&l_save.fw))
        DebugMessage(M64MSG_WARNING, "no inotify here; judging the save by the file's time and size");
    set_slot(slot);
    r = (*CoreDoCommand)(M64CMD_STATE_SAVE, 1, l_save.path);        /* 1 = Mupen64Plus format, to this file */
    if (r != M64ERR_SUCCESS)
    {
        DebugMessage(M64MSG_ERROR, "M64CMD_STATE_SAVE failed (%d)", (int) r);
        toast_show(TOAST_ERROR, "Save failed");
        filewatch_stop(&l_save.fw);
        if (l_save.rotated)
            autosave_unrotate(l_save.path);
        l_save.rotated = 0;
        return -1;
    }
    l_save.active = 1;
    l_save.t0 = SDL_GetTicks();
    if (slot == STATES_AUTO_SLOT)
        toast_show(TOAST_INFO, "Saving auto slot...");
    else
        toast_show(TOAST_INFO, "Saving slot %d...", slot);
    DebugMessage(M64MSG_INFO, "saving state to '%s'", l_save.path);
    return 0;
}

static void save_failed(const char *what)
{
    int quit = l_save.quit_after;
    l_save.active = 0;
    filewatch_stop(&l_save.fw);
    toast_show(TOAST_ERROR, "Save not written");
    DebugMessage(M64MSG_ERROR, "the state file '%s' %s", l_save.path, what);
    if (l_save.rotated && autosave_unrotate(l_save.path))
        DebugMessage(M64MSG_INFO, "the previous auto-save was put back");
    l_save.rotated = 0;
    if (quit && l_sig_quit)
        emu_stop();                    /* spruce's SIGTERM follows in 2 s anyway; a clean stop flushes the game's own saves */
    else if (quit)
        l_quitting = 0;                /* stay in the game rather than lose progress: Quit again to retry */
}

static void save_poll(void)
{
    struct stat st;
    if (!l_save.active)
        return;
    if (l_sig_quit && !l_stop_sent && filewatch_started(&l_save.fw))
    {
        /* The core's worker has the state (serialised before it was queued) and is compressing it. The core finishes the
         * write while it shuts down, as it does for spruce's own SIGUSR1: waiting for the file to close would put the whole
         * shutdown (0.6 s on the RG35XX SP) after the write, too close to spruce's SIGTERM 2 s after the signal. A write
         * cut short is what the start-up check and the .prev backup are for. */
        DebugMessage(M64MSG_INFO, "SIGUSR1: the write has started (%u ms after the command); stopping, the core finishes it",
                     (unsigned) (SDL_GetTicks() - l_save.t0));
        emu_stop();
    }
    if (filewatch_poll(&l_save.fw))
    {
        /* closed by the writer. The core's own status message follows within microseconds; the emulator is only
         * stopped on a later frame, long before the core tears its configuration down */
        int quit = l_save.quit_after;
        if (stat(l_save.path, &st) != 0 || st.st_size <= 0)
        {
            save_failed("is empty (the write failed)");
            return;
        }
        int ino = filewatch_uses_inotify(&l_save.fw);
        l_save.active = 0;
        filewatch_stop(&l_save.fw);
        toast_show(TOAST_OK, "Saved");
        DebugMessage(M64MSG_INFO, "state file written: %s (%s, %u ms after the command, t=%u ms)", l_save.path, ino ? "inotify" : "polled",
                     (unsigned) (SDL_GetTicks() - l_save.t0), (unsigned) SDL_GetTicks());
        if (quit)
            emu_stop();
        return;
    }
    if (SDL_GetTicks() - l_save.t0 >= SAVE_WAIT_MS)
    {
        char what[64];
        snprintf(what, sizeof(what), "was not written within %u ms", SAVE_WAIT_MS);
        save_failed(what);
    }
}

static int start_load(int slot)
{
    state_info si;
    char path[PATH_MAX];
    m64p_error r;

    if (slot < 0 || slot >= STATES_NUM_SLOTS)        /* the auto slot is loaded by the launcher (--savestate), never here */
        return -1;
    states_stat(slot, &si);
    if (!si.exists)
    {
        toast_show(TOAST_ERROR, "Slot %d is empty", slot);
        return -1;
    }
    set_slot(slot);
    if (si.pj64 && states_pj_path(slot, path, sizeof(path)) == 0)
    {
        /* the slot's newest state is a Project64 one, but the core's slot load would prefer an existing .st: ask for
         * the file itself (the format is detected from the file) */
        DebugMessage(M64MSG_INFO, "loading the Project64 state '%s'", path);
        r = (*CoreDoCommand)(M64CMD_STATE_LOAD, 0, path);
    }
    else
        r = (*CoreDoCommand)(M64CMD_STATE_LOAD, 0, NULL);
    if (r != M64ERR_SUCCESS)
    {
        DebugMessage(M64MSG_ERROR, "M64CMD_STATE_LOAD failed (%d)", (int) r);
        toast_show(TOAST_ERROR, "Load failed");
        return -1;
    }
    l_load_wait = 2;
    toast_show(TOAST_INFO, "Loading slot %d...", slot);
    return 0;
}

static void begin_quit(void)
{
    if (l_quitting)
        return;
    if (l_save.active)
    {
        toast_show(TOAST_ERROR, "Busy, try again");
        return;
    }
    l_quitting = 1;
    ff_set(0);
    if (g_cfg.autosave_on_quit && emu_state() == M64EMU_RUNNING)
    {
        if (start_save(STATES_AUTO_SLOT, 1) == 0)
            return;
        l_quitting = 0;                    /* couldn't even start: stay in the game, the toast says why */
        return;
    }
    emu_stop();
}

/* SIGUSR1: an explicit save-and-quit request, so it does not depend on autosave_on_quit */
static void signal_save_quit(void)
{
    DebugMessage(M64MSG_INFO, "SIGUSR1: writing the auto-save, then quitting (frame %u, t=%u ms)", l_frames, (unsigned) SDL_GetTicks());
    l_quitting = 1;
    l_sig_quit = 1;
    ff_set(0);
    if (emu_state() == M64EMU_RUNNING && start_save(STATES_AUTO_SLOT, 1) == 0)
        return;
    DebugMessage(M64MSG_WARNING, "SIGUSR1: no auto-save could be started; quitting without one");
    emu_stop();
}

/* from the first swap, where the emulator is surely running and the core takes the load like any menu load */
static void autoload_run(void)
{
    m64p_error r;

    if (!l_autoload_want)
        return;
    l_autoload_want = 0;
    if (!l_autoload_ok)
    {
        DebugMessage(M64MSG_INFO, "M64P_AUTOLOAD: no valid auto-save '%s'; starting fresh", l_autoload_path);
        return;
    }
    r = (*CoreDoCommand)(M64CMD_STATE_LOAD, 0, l_autoload_path);
    if (r != M64ERR_SUCCESS)
    {
        DebugMessage(M64MSG_ERROR, "M64P_AUTOLOAD: M64CMD_STATE_LOAD failed (%d); starting fresh", (int) r);
        return;
    }
    l_load_wait = 2;
    DebugMessage(M64MSG_INFO, "M64P_AUTOLOAD: resuming from the auto-save '%s'", l_autoload_path);
    if (l_startup_toast[0] == 0)
    {
        snprintf(l_startup_toast, sizeof(l_startup_toast), "Auto-save loaded");
        l_startup_toast_kind = TOAST_OK;
    }
}

typedef enum { HK_MENU, HK_SAVE, HK_LOAD, HK_NEXT, HK_PREV, HK_FF, HK_QUIT, HK_RECOVERY, HK_NUM } hk_id;

static uint32_t hk_combo(int id)
{
    switch (id)
    {
        case HK_MENU: return g_cfg.menu_combo;
        case HK_SAVE: return g_cfg.hk_quick_save;
        case HK_LOAD: return g_cfg.hk_quick_load;
        case HK_NEXT: return g_cfg.hk_next_slot;
        case HK_PREV: return g_cfg.hk_prev_slot;
        case HK_FF:   return g_cfg.hk_fast_forward;
        case HK_RECOVERY: return g_cfg.recovery_combo;
        default:      return g_cfg.hk_quit;
    }
}

static void announce_slot(int slot)
{
    state_info si;
    states_stat(slot, &si);
    if (si.exists)
    {
        char d[16], c[16];
        states_format_time(si.mtime, d, sizeof(d), c, sizeof(c));
        toast_show(TOAST_INFO, "Slot %d  %s %s%s", slot, d, c, si.pj64 ? "  PJ64" : "");
    }
    else
        toast_show(TOAST_INFO, "Slot %d  (Empty)", slot);
}

static void do_quick_save(void)
{
    start_save(cur_slot(), 0);
}

static void do_quick_load(void)
{
    start_load(cur_slot());
}

static int hotkeys_update(uint32_t held, uint32_t prev, uint32_t now)
{
    int complete[HK_NUM], was[HK_NUM], fire[HK_NUM], i, j, open = 0;

    for (i = 0; i < HK_NUM; i++)
    {
        uint32_t c = hk_combo(i);
        complete[i] = c != 0 && (held & c) == c;
        was[i] = c != 0 && (prev & c) == c;
    }
    /* the most specific combo wins: select+start+b must not also fire select+start */
    for (i = 0; i < HK_NUM; i++)
    {
        fire[i] = complete[i] && !was[i];
        for (j = 0; j < HK_NUM && fire[i]; j++)
        {
            uint32_t ci = hk_combo(i), cj = hk_combo(j);
            if (j != i && complete[j] && cj != ci && (cj & ci) == ci)
                fire[i] = 0;
            if (j < i && complete[j] && cj == ci)       /* identical combos: first one wins */
                fire[i] = 0;
        }
    }
    for (i = 0; i < HK_NUM; i++)
    {
        uint32_t c = hk_combo(i);
        if (c == 0 || !complete[i])
            continue;
        for (j = 0; j < HK_NUM; j++)
            if (j != i && complete[j] && hk_combo(j) != c && (hk_combo(j) & c) == c)
                complete[i] = 0;                        /* superseded by a longer combo while held */
    }

    if (l_quitting)
        return 0;

    if (fire[HK_MENU] || fire[HK_RECOVERY])
        open = 1;
    if (fire[HK_SAVE] && !open)
        do_quick_save();
    if (fire[HK_LOAD] && !open)
        do_quick_load();
    if (fire[HK_NEXT] && !open)
    {
        int s = (cur_slot() + 1) % STATES_NUM_SLOTS;
        set_slot(s);
        announce_slot(s);
    }
    if (fire[HK_PREV] && !open)
    {
        int s = (cur_slot() + STATES_NUM_SLOTS - 1) % STATES_NUM_SLOTS;
        set_slot(s);
        announce_slot(s);
    }
    if (g_cfg.hk_fast_forward != 0)
    {
        if (g_cfg.ff_toggle)
        {
            if (fire[HK_FF])
            {
                ff_set(!l_ff_on);
                toast_show(TOAST_INFO, l_ff_on ? "Fast-Forward On" : "Fast-Forward Off");
            }
        }
        else
            ff_set(complete[HK_FF]);
    }
    if (g_cfg.hk_quit != 0 && fire[HK_QUIT] && !open)
    {
        switch (quitarm_press(&l_quit_arm, g_cfg.confirm_quit, now))
        {
            case QUITARM_ARMED: toast_show(TOAST_INFO, "Press again to quit"); break;
            case QUITARM_QUIT:  begin_quit(); break;
            default: break;
        }
    }
    return open;
}

/* the menu combo, or the recovery combo that always works whatever the menu combo was set to */
static int combo_edge(uint32_t held, uint32_t prev)
{
    uint32_t c = g_cfg.menu_combo, r = g_cfg.recovery_combo;
    return (c != 0 && (held & c) == c && (prev & c) != c) || (r != 0 && (held & r) == r && (prev & r) != r);
}

static void drain_sdl_events(int *quit)
{
    SDL_Event ev;
    SDL_PumpEvents();      /* the core's event filter runs here (window resize, SDL_QUIT, keyboard hotkeys) */
    while (SDL_PollEvent(&ev))
        if (ev.type == SDL_QUIT)
            *quit = 1;
}

static int run_menu(void)
{
    int w = 0, h = 0;
    menu_result result = MENU_NONE;
    int paused_by_us = 0, quit = 0;
    pad_tracker tr;
    uint32_t t_frame, loop_prev, t_capture;
    int entered_ui = 0, sig_close = 0;
    dbg_tick_t dt;

    video_drawable_size(&w, &h);
    if (w <= 0 || h <= 0)
        return 0;

    /* freeze: copy the frame about to be presented (game context is current) */
    t_capture = SDL_GetTicks();
    if (ugl_capture_frame(w, h) != 0)
    {
        DebugMessage(M64MSG_WARNING, "couldn't capture the game frame; not opening the menu");
        return 0;
    }
    t_capture = SDL_GetTicks() - t_capture;

    /* pause the core (state bookkeeping + any threaded plugin), then become the menu thread */
    ff_set(0);
    toast_clear();
    quitarm_reset(&l_quit_arm);
    if (emu_state() == M64EMU_RUNNING && (*CoreDoCommand)(M64CMD_PAUSE, 0, NULL) == M64ERR_SUCCESS)
        paused_by_us = 1;
    l_menu_thread = SDL_ThreadID();
    SDL_AtomicSet(&l_menu_open, 1);

    if (video_enter_ui() != 0)
    {
        SDL_AtomicSet(&l_menu_open, 0);
        if (paused_by_us)
            (*CoreDoCommand)(M64CMD_RESUME, 0, NULL);
        return 0;
    }
    entered_ui = 1;
    toast_set_menu_open(1);                          /* toasts made while the menu is up go to its status line */
    ugl_background_upload();
    menu_set_quit_saves(g_cfg.autosave_on_quit);
    fif_discover();                                  /* foir loaded after we looked? (no-op once found) */
    menu_open(cur_slot());
    inputfilter_force(1);
    DebugMessage(M64MSG_VERBOSE, "menu opened at frame %u (%dx%d, frame capture %u ms)", l_frames, w, h, (unsigned) t_capture);

    /* buttons held right now (the combo that opened us) count as already pressed */
    loop_prev = pad_poll();
    pad_tracker_reset(&tr, loop_prev);

    t_frame = SDL_GetTicks();
    while (!quit && emu_state() != M64EMU_STOPPED)
    {
        uint32_t held, now;
        float brightness = 1.0f - (float) g_cfg.dim_percent / 100.0f;
        const uint8_t *shot = NULL;
        int consumed = 0;

        drain_sdl_events(&quit);
        dbg_tick(&dt);
        {
            sigs_state ss = { 1, l_quitting, 0 };
            if (sigs_poll(&ss) != SIGS_NONE)
                sig_close = 1;
        }
        pad_inject(dt.buttons);
        if (dt.cap)
            pad_capture_inject(&dt.capsrc);
        if (dt.probe_set)
            remap_set_probe_override(dt.probe_on, &dt.probe);
        held = pad_poll();
        now = SDL_GetTicks();
        pad_tracker_update(&tr, held, now);

        if (dt.kill)
        {
            quit = 1;
        }
        else if (dt.quit)
        {
            result = MENU_QUIT;
        }
        else if (dt.toggle_menu || sig_close || (combo_edge(held, loop_prev) && !(consumed = menu_combo_pressed(held))))
        {
            result = MENU_RESUME;
        }
        else
        {
            video_drawable_size(&w, &h);
            ugl_begin(w, h);
            ugl_background_draw(w, h, brightness);
            /* the buttons that confirmed a new menu combo are not also presses on the page underneath */
            result = menu_frame(w, h, consumed ? 0 : tr.pressed, consumed ? 0 : held);
            if (dt.shot)
                shot = ugl_readback(w, h);        /* composited back buffer, before it is presented */
            ugl_end();
            if (shot != NULL)
                save_shot(shot, w, h);
            video_present();
        }
        loop_prev = held;
        if (result != MENU_NONE)
            break;

        /* pace to ~60 Hz even when vsync is unavailable or the compositor throttles us */
        {
            uint32_t elapsed = SDL_GetTicks() - t_frame;
            if (elapsed < FRAME_MS)
                SDL_Delay(FRAME_MS - elapsed);
            t_frame = SDL_GetTicks();
        }
    }
    pad_inject(0);
    toast_set_menu_open(0);                          /* what menu_closing() has to say floats over the game again */
    menu_closing();

    /* show one clean (undimmed) frame so the game doesn't resume over a menu; the input filter hides the closing
     * buttons until released; if it is off, keep the frame up until they are */
    {
        uint32_t t0 = SDL_GetTicks();
        int wait_release = !(g_cfg.block_game_input) && !sig_close;
        if (result != MENU_QUIT && !quit)
        {
            for (;;)
            {
                video_drawable_size(&w, &h);
                ugl_begin(w, h);
                ugl_background_draw(w, h, 1.0f);
                ugl_end();
                video_present();
                drain_sdl_events(&quit);
                if (!wait_release || pad_poll() == 0 || SDL_GetTicks() - t0 > 800 || quit)
                    break;
                SDL_Delay(FRAME_MS);
            }
        }
    }
    inputfilter_force(0);

    if (entered_ui)
        video_leave_ui();
    SDL_AtomicSet(&l_menu_open, 0);

    /* issued while the core is still paused; it runs them from its interrupt handler once it executes again */
    if (result == MENU_NONE || result == MENU_RESUME || result == MENU_RESET || result == MENU_RESET_SOFT ||
        result == MENU_SAVE_STATE || result == MENU_LOAD_STATE)
    {
        int sel = menu_selected_slot();
        if (sel >= 0)
            set_slot(sel);
    }
    if (quit)
    {
        l_quitting = 1;
        emu_stop();                                  /* clears the pause itself */
        paused_by_us = 0;
    }
    else if (result == MENU_QUIT)
    {
        /* with auto-save on, the game must run a moment to take the snapshot: resume first */
        if (paused_by_us && emu_state() == M64EMU_PAUSED)
            (*CoreDoCommand)(M64CMD_RESUME, 0, NULL);
        paused_by_us = 0;
        begin_quit();
    }
    else if (result == MENU_RESET || result == MENU_RESET_SOFT)
    {
        DebugMessage(M64MSG_INFO, "%s reset of the console", result == MENU_RESET ? "hard" : "soft");
        (*CoreDoCommand)(M64CMD_RESET, result == MENU_RESET ? 1 : 0, NULL);     /* 1 = hard reset (power cycle), 0 = soft */
    }
    else if (result == MENU_SAVE_STATE)
    {
        start_save(menu_action_slot(), 0);
    }
    else if (result == MENU_LOAD_STATE)
    {
        start_load(menu_action_slot());
    }
    if (paused_by_us && emu_state() == M64EMU_PAUSED)
        (*CoreDoCommand)(M64CMD_RESUME, 0, NULL);
    if (l_save.active)
        l_save.t0 = SDL_GetTicks();       /* the core ran no frames while the menu was up: the save's clock starts over */
    DebugMessage(M64MSG_VERBOSE, "menu closed");
    l_prev_held = pad_poll();
    return 1;
}

static void debug_save_pj(int slot, int zip)
{
    char path[PATH_MAX];
    m64p_error r;
    if (states_pj_new_path(slot, zip, path, sizeof(path)) != 0)
        return;
    r = (*CoreDoCommand)(M64CMD_STATE_SAVE, zip ? 2 : 3, path);
    DebugMessage(M64MSG_INFO, "debug: saving a Project64 state to '%s' (core: %d)", path, (int) r);
}

int session_before_swap(void)
{
    uint32_t held, now;
    dbg_tick_t dt;
    int open = 0, want_shot = 0;

    if (SDL_AtomicGet(&l_menu_open))
        return 0;                                    /* re-entrancy guard */
    l_frames++;
    sigs_arm();
    if (l_frames == 1)
        autoload_run();
    if (l_load_wait > 0)
        l_load_wait--;
    if (l_frames == 3)
    {
        audio_apply_volume();                        /* the audio plugin is up: this game's volume */
        if (l_startup_toast[0] != 0)
            toast_show((toast_kind) l_startup_toast_kind, "%s", l_startup_toast);
        l_startup_toast[0] = 0;
    }
    now = SDL_GetTicks();

    dbg_tick(&dt);
    pad_inject(dt.buttons);
    held = pad_poll();

    save_poll();

    {
        sigs_state ss = { 0, l_quitting, l_save.active || l_load_wait > 0 };
        sigs_action sa = sigs_poll(&ss);
        if (sa == SIGS_SAVE_QUIT)
            signal_save_quit();
        else if (sa == SIGS_MENU)
        {
            DebugMessage(M64MSG_VERBOSE, "SIGUSR2: opening the menu (frame %u)", l_frames);
            open = 1;
        }
    }

    if (dt.stat)
        DebugMessage(M64MSG_INFO, "stat: frame %u t=%u ms", l_frames, (unsigned) now);
    if (dt.kill)
        emu_stop();
    if (dt.quit)
        begin_quit();
    if (dt.pj_slot >= 0)
        debug_save_pj(dt.pj_slot, dt.pj_zip);
    if (dt.shot)
        want_shot = 1;
    if (!l_quitting)
    {
        if (dt.toggle_menu || combo_edge(held, l_prev_held))
            open = 1;
        else if (hotkeys_update(held, l_prev_held, now))
            open = 1;
    }
    else
        hotkeys_update(0, 0, now);                   /* keeps fast-forward off while quitting */
    l_prev_held = held;

    if (open && run_menu())
        return 1;
    /* fast-forward: frames arriving faster than the display refreshes are dropped instead of blocking in the swap;
     * once it ends every frame is swapped again, so the newest one shows at the next vblank */
    if (l_ff_on && !want_shot && !video_present_due())
        return 1;
    if (toast_visible())
    {
        draw_overlay(want_shot);
        want_shot = 0;
    }
    if (want_shot)
    {
        int w, h;
        video_drawable_size(&w, &h);
        if (ugl_capture_frame(w, h) == 0)
        {
            const uint8_t *d = ugl_capture_data(&w, &h);
            save_shot(d, w, h);
        }
    }
    return 0;
}
