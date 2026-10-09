/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-ui-suxty - session.c
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


#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <SDL.h>

#include "cfg.h"
#include "core_interface.h"
#include "debug.h"
#include "gl.h"
#include "inputfilter.h"
#include "m64p_types.h"
#include "main.h"
#include "menu.h"
#include "pad.h"
#include "profile.h"
#include "remap.h"
#include "session.h"
#include "states.h"
#include "toast.h"
#include "video.h"

/* See the threading notes in session.h. */

static volatile sig_atomic_t l_stop_requested;  /* number of SIGINT/SIGTERM received */
static SDL_atomic_t  l_menu_open;               /* 1 while the nested menu loop runs */
static SDL_threadID  l_menu_thread;
static volatile int  l_emu_state = M64EMU_STOPPED;
static int           l_stop_sent;
static uint32_t      l_prev_held;
static unsigned      l_frames;

#define FRAME_MS 16

/* ---- save/load jobs --------------------------------------------------------------------------- */

/* The core executes state jobs from its interrupt handler, i.e. only while the emulator runs, and a save
 * finishes later on the core's worker thread.  A job therefore outlives the call that queued it: it is
 * tracked here and completed from the frame hook when the core reports M64CORE_STATE_SAVECOMPLETE /
 * LOADCOMPLETE (session_core_state(), possibly on another thread; it only stores the result). */
typedef enum { JOB_NONE = 0, JOB_SAVE, JOB_LOAD } job_kind;

#define SAVE_TIMEOUT_MS 30000u
#define LOAD_TIMEOUT_MS 10000u
#define MENU_REOPEN_FRAMES 3        /* let the core take its snapshot before the menu pauses it again */

static struct {
    volatile int kind;              /* job_kind; written by the emulation thread only */
    int          slot;              /* 0-9, or STATES_AUTO_SLOT */
    int          from_menu;         /* re-open the menu (busy page) when the save is done */
    int          quit_after;        /* stop the emulator when done (auto-save on quit) */
    int          resume;            /* a load issued by --resume */
    int          pj64;              /* a load of a Project64 state */
    unsigned     frames;            /* frames presented since the job was issued */
    uint32_t     started;           /* SDL ticks of the first poll (0 = not yet) */
    uint32_t     cb_seen;           /* SDL ticks when the core first said "save complete" */
    char         tmp[PATH_MAX];     /* auto-save: file the core writes, renamed over `dest` on success */
    char         dest[PATH_MAX];
} l_job;
static SDL_atomic_t l_job_result;   /* 0 pending, 1 success, 2 failure (from the state callback) */

/* "Is the file on disk yet?"  The state callback is not enough for a save: depending on the core version,
 * M64CORE_STATE_SAVECOMPLETE is sent as soon as the save has been *queued* (the Mupen64Plus 2.5.9 core that
 * ROCKNIX ships) or when the worker thread has finished writing (current core).  Both versions have that
 * worker thread announce the outcome with a status message, which reaches us through the debug callback
 * (session_core_message()): "Saved state to: <file name>", or "Could not open/write ... state file: <path>".
 * A save job is therefore done when its own file name shows up in one of those messages. */
static SDL_mutex   *l_msg_lock;
static char         l_expect_name[300];          /* file name (no directory) the running save job waits for */
static SDL_atomic_t l_msg_result;                /* 0 pending, 1 written, 2 write failed */

static int  l_quitting;             /* quit sequence in progress: hotkeys are off */
static int  l_quit_pending;         /* quit as soon as the running job is done */
static int  l_restart;              /* the quit in progress is a restart (auto-save is then forced) */
static int  l_ff_on;
static int  l_ff_saved_factor = 100;

/* fast-forward + hotkey bookkeeping */
static uint32_t l_quit_since;

void session_request_stop(void)
{
    l_stop_requested++;
}

void session_core_state(m64p_core_param param, int value)
{
    /* Called synchronously from whichever thread changed the state.  Only record things here; never call
     * back into the core or the menu from this callback. */
    if (param == M64CORE_EMU_STATE)
        l_emu_state = value;
    else if (param == M64CORE_STATE_SAVECOMPLETE && l_job.kind == JOB_SAVE)
        SDL_AtomicSet(&l_job_result, value ? 1 : 2);
    else if (param == M64CORE_STATE_LOADCOMPLETE && l_job.kind == JOB_LOAD)
        SDL_AtomicSet(&l_job_result, value ? 1 : 2);
}

/* Debug/status messages from the core (any thread). */
void session_core_message(int level, const char *message)
{
    int res = 0;
    if (level != M64MSG_STATUS || message == NULL || l_msg_lock == NULL)
        return;
    if (strncmp(message, "Saved state to: ", 16) == 0)
        res = 1;
    else if (strncmp(message, "Could not open state file: ", 27) == 0 ||
             strncmp(message, "Could not write data to state file: ", 36) == 0)
        res = 2;
    if (res == 0)
        return;
    SDL_LockMutex(l_msg_lock);
    if (l_expect_name[0] != 0 && strstr(message, l_expect_name) != NULL)
        SDL_AtomicSet(&l_msg_result, res);
    SDL_UnlockMutex(l_msg_lock);
}

static void expect_file(const char *path)
{
    const char *base = path != NULL ? strrchr(path, '/') : NULL;
    base = base != NULL ? base + 1 : path;
    SDL_LockMutex(l_msg_lock);
    snprintf(l_expect_name, sizeof(l_expect_name), "%.*s", (int) sizeof(l_expect_name) - 1, base ? base : "");
    SDL_AtomicSet(&l_msg_result, 0);
    SDL_UnlockMutex(l_msg_lock);
}

int session_restart_requested(void)
{
    return l_restart;
}

int session_menu_blocks_this_thread(void)
{
    return SDL_AtomicGet(&l_menu_open) && SDL_ThreadID() != l_menu_thread;
}

int session_init(void)
{
    l_msg_lock = SDL_CreateMutex();
    if (pad_init() != 0)
        return -1;
    remap_init();
    return 0;
}

/* ---- core control helpers ------------------------------------------------------------------ */

static int emu_state(void)
{
    int v = 0;
    if ((*CoreDoCommand)(M64CMD_CORE_STATE_QUERY, M64CORE_EMU_STATE, &v) != M64ERR_SUCCESS)
        return l_emu_state;
    return v;
}

static void emu_stop(void)
{
    if (!l_stop_sent)
    {
        l_stop_sent = 1;
        DebugMessage(M64MSG_INFO, "stopping emulation");
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

/* ---- fast-forward ---------------------------------------------------------------------------- */

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
    DebugMessage(M64MSG_INFO, "fast-forward %s (frame %u, t=%u ms)", on ? "on" : "off", l_frames, (unsigned) SDL_GetTicks());
}

void session_shutdown(void)
{
    ff_set(0);
    remap_shutdown();
    pad_shutdown();
    if (l_msg_lock != NULL)
    {
        SDL_mutex *m = l_msg_lock;
        l_msg_lock = NULL;
        SDL_DestroyMutex(m);
    }
}

/* ---- screenshots (test hook) ---------------------------------------------------------------- */

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

/* Toasts and the fast-forward badge are drawn over the finished game frame, in suxty's private GL context,
 * right before the swap.  If a debug screenshot was asked for, it is read back from there so it shows them. */
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

/* ---- jobs -------------------------------------------------------------------------------------- */

static int job_busy(void)
{
    return l_job.kind != JOB_NONE;
}

static void job_clear(void)
{
    l_job.kind = JOB_NONE;
}

static int start_save(int slot, int from_menu, int quit_after)
{
    char why[64];
    m64p_error r;

    if (job_busy())
        return -1;
    if (states_check_writable(why, sizeof(why)) != 0)
    {
        DebugMessage(M64MSG_ERROR, "can't save a state: %s", why);
        toast_show(TOAST_ERROR, "Can't save: %s", why);
        return -1;
    }
    memset((void *) &l_job, 0, sizeof(l_job));
    l_job.slot = slot;
    l_job.from_menu = from_menu;
    l_job.quit_after = quit_after;
    SDL_AtomicSet(&l_job_result, 0);
    l_job.kind = JOB_SAVE;
    if (slot == STATES_AUTO_SLOT)
    {
        if (states_slot_path(slot, l_job.dest, sizeof(l_job.dest)) != 0 ||
            (size_t) snprintf(l_job.tmp, sizeof(l_job.tmp), "%s.tmp", l_job.dest) >= sizeof(l_job.tmp))
        {
            job_clear();
            return -1;
        }
        unlink(l_job.tmp);
        expect_file(l_job.tmp);
        r = (*CoreDoCommand)(M64CMD_STATE_SAVE, 1, l_job.tmp);        /* 1 = Mupen64Plus format */
    }
    else
    {
        char path[PATH_MAX];
        set_slot(slot);
        if (states_slot_path(slot, path, sizeof(path)) == 0)
            expect_file(path);
        r = (*CoreDoCommand)(M64CMD_STATE_SAVE, 1, NULL);
    }
    if (r != M64ERR_SUCCESS)
    {
        DebugMessage(M64MSG_ERROR, "M64CMD_STATE_SAVE failed (%d)", (int) r);
        job_clear();
        return -1;
    }
    DebugMessage(M64MSG_INFO, "saving state to %s", slot == STATES_AUTO_SLOT ? "the auto slot" : "slot");
    return 0;
}

static int start_load(int slot, int resume)
{
    char path[PATH_MAX];
    m64p_error r;

    if (job_busy())
        return -1;
    memset((void *) &l_job, 0, sizeof(l_job));
    l_job.slot = slot;
    l_job.resume = resume;
    SDL_AtomicSet(&l_job_result, 0);
    l_job.kind = JOB_LOAD;
    if (slot == STATES_AUTO_SLOT)
    {
        if (states_slot_path(slot, path, sizeof(path)) != 0)
        {
            job_clear();
            return -1;
        }
        r = (*CoreDoCommand)(M64CMD_STATE_LOAD, 0, path);
    }
    else
    {
        state_info si;
        states_stat(slot, &si);
        set_slot(slot);
        if (si.pj64 && states_pj_path(slot, path, sizeof(path)) == 0)
        {
            /* the slot's newest state is a Project64 one: the core's slot load would prefer an existing .st, so ask
             * for the file itself (it detects the format from the file) */
            l_job.pj64 = 1;
            DebugMessage(M64MSG_INFO, "loading the Project64 state '%s'", path);
            r = (*CoreDoCommand)(M64CMD_STATE_LOAD, 0, path);
        }
        else
            r = (*CoreDoCommand)(M64CMD_STATE_LOAD, 0, NULL);
    }
    if (r != M64ERR_SUCCESS)
    {
        DebugMessage(M64MSG_ERROR, "M64CMD_STATE_LOAD failed (%d)", (int) r);
        job_clear();
        return -1;
    }
    return 0;
}

static void begin_quit(void);

/* Polls the running job.  Returns 1 when it finished (or timed out) and fills msg/kind (kind > 0 good,
 * < 0 error).  The caller decides how to show the message. */
static int job_poll(char *msg, size_t msgsize, int *kind)
{
    int res;
    uint32_t now = SDL_GetTicks();

    if (l_job.kind == JOB_NONE)
        return 0;
    if (l_job.started == 0)
        l_job.started = now ? now : 1;
    res = SDL_AtomicGet(&l_job_result);
    if (l_job.kind == JOB_SAVE)
    {
        int m = SDL_AtomicGet(&l_msg_result);
        if (m != 0)
            res = m;                                 /* the writer announced the outcome */
        else if (res == 1)
        {
            /* "queued" (or "written", depending on the core version) but no word from the writer yet.  If the
             * announcement never comes (a core that words it differently), settle for a quiet, non-empty file. */
            res = 0;
            if (l_job.cb_seen == 0)
                l_job.cb_seen = now ? now : 1;
            else if (now - l_job.cb_seen > 8000u)
            {
                char path[PATH_MAX];
                struct stat st;
                if ((l_job.slot == STATES_AUTO_SLOT ? snprintf(path, sizeof(path), "%s", l_job.tmp) > 0
                                                    : states_slot_path(l_job.slot, path, sizeof(path)) == 0) &&
                    stat(path, &st) == 0 && st.st_size > 0 && time(NULL) - st.st_mtime >= 2)
                {
                    DebugMessage(M64MSG_WARNING, "the core never reported the finished state file; assuming it is complete");
                    res = 1;
                }
            }
        }
    }
    if (res != 0 && now - l_job.started < (uint32_t) dbg_job_delay())
        return 0;                                    /* test hook: make the "Saving..." page observable */
    if (res == 0)
    {
        uint32_t limit = l_job.kind == JOB_SAVE ? SAVE_TIMEOUT_MS : LOAD_TIMEOUT_MS;
        if (now - l_job.started < limit)
            return 0;
        res = 2;
        DebugMessage(M64MSG_ERROR, "state %s timed out", l_job.kind == JOB_SAVE ? "save" : "load");
    }

    if (l_job.kind == JOB_SAVE)
    {
        const char *what = l_job.slot == STATES_AUTO_SLOT ? "auto-save" : NULL;
        if (l_job.slot == STATES_AUTO_SLOT)
        {
            if (res == 1 && rename(l_job.tmp, l_job.dest) != 0)
            {
                DebugMessage(M64MSG_ERROR, "couldn't move the auto-save into place");
                res = 2;
            }
            if (res != 1)
                unlink(l_job.tmp);
        }
        if (res == 1)
        {
            if (what)
                snprintf(msg, msgsize, "Auto-saved");
            else
                snprintf(msg, msgsize, "Saved slot %d", l_job.slot);
            *kind = 1;
        }
        else
        {
            if (what)
                snprintf(msg, msgsize, "Auto-save failed");
            else
                snprintf(msg, msgsize, "Saving slot %d failed", l_job.slot);
            *kind = -1;
        }
    }
    else
    {
        if (res == 1)
        {
            if (l_job.resume)
                snprintf(msg, msgsize, "Resumed from auto-save");
            else if (l_job.slot == STATES_AUTO_SLOT)
                snprintf(msg, msgsize, "Loaded auto-save");
            else
                snprintf(msg, msgsize, l_job.pj64 ? "Loaded slot %d (PJ64)" : "Loaded slot %d", l_job.slot);
            *kind = 1;
        }
        else
        {
            snprintf(msg, msgsize, l_job.slot == STATES_AUTO_SLOT ? "Loading the auto-save failed" : "Loading slot %d failed", l_job.slot);
            *kind = -1;
        }
    }
    DebugMessage(res == 1 ? M64MSG_INFO : M64MSG_ERROR, "%s", msg);

    if (l_job.quit_after)
    {
        if (l_restart && *kind <= 0)
        {
            /* no auto-save, nothing to come back to: stay in the game instead of restarting into a fresh boot */
            l_restart = 0;
            l_quitting = 0;
            toast_show(TOAST_ERROR, "Restart cancelled: couldn't save");
        }
        else
            emu_stop();
    }
    job_clear();
    if (l_quit_pending)
    {
        l_quit_pending = 0;
        begin_quit();
    }
    return 1;
}

/* Quit, saving the auto slot first if that is enabled.  The game keeps running until the save is done. */
static void begin_quit(void)
{
    if (l_quitting)
        return;
    if (job_busy())
    {
        l_quit_pending = 1;           /* a save/load is in flight; quit right after it */
        return;
    }
    l_quitting = 1;
    ff_set(0);
    if ((g_cfg.autosave_on_quit || l_restart) && emu_state() == M64EMU_RUNNING)
    {
        if (start_save(STATES_AUTO_SLOT, 0, 1) == 0)
        {
            toast_show(TOAST_INFO, l_restart ? "Restarting..." : "Saving...");
            return;
        }
        if (l_restart)
        {
            l_restart = 0;
            l_quitting = 0;
            toast_show(TOAST_ERROR, "Restart cancelled: couldn't save");
            return;
        }
        DebugMessage(M64MSG_WARNING, "auto-save failed to start; quitting without it");
    }
    emu_stop();
}

int session_resume(void)
{
    state_info si;
    states_stat(STATES_AUTO_SLOT, &si);
    if (!si.exists)
    {
        DebugMessage(M64MSG_INFO, "--resume: no auto-save yet; starting normally");
        return 0;
    }
    if (start_load(STATES_AUTO_SLOT, 1) != 0)
    {
        DebugMessage(M64MSG_WARNING, "--resume: couldn't queue the auto-save; starting normally");
        return -1;
    }
    DebugMessage(M64MSG_INFO, "--resume: loading the auto-save");
    return 1;
}

/* ---- hotkeys --------------------------------------------------------------------------------- */

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
        toast_show(TOAST_INFO, "Slot %d  (empty)", slot);
}

static void do_quick_save(void)
{
    int slot = cur_slot();
    if (job_busy())
    {
        toast_show(TOAST_ERROR, "Busy, try again");
        return;
    }
    if (start_save(slot, 0, 0) == 0)
        toast_show(TOAST_INFO, "Saving slot %d...", slot);
}

static void do_quick_load(void)
{
    int slot = cur_slot();
    state_info si;
    if (job_busy())
    {
        toast_show(TOAST_ERROR, "Busy, try again");
        return;
    }
    states_stat(slot, &si);
    if (!si.exists)
    {
        toast_show(TOAST_ERROR, "Slot %d is empty", slot);
        return;
    }
    if (start_load(slot, 0) != 0)
        toast_show(TOAST_ERROR, "Loading slot %d failed", slot);
}

/* Evaluates the combos for one frame.  Returns 1 if the menu should open. */
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
                toast_show(TOAST_INFO, l_ff_on ? "Fast-forward on" : "Fast-forward off");
            }
        }
        else
            ff_set(complete[HK_FF]);
    }
    if (g_cfg.hk_quit != 0)
    {
        if (complete[HK_QUIT])
        {
            if (fire[HK_QUIT] || l_quit_since == 0)
            {
                l_quit_since = now ? now : 1;
                if (g_cfg.quit_hold_ms > 0)
                    toast_show(TOAST_INFO, "Hold to quit...");
            }
            if (now - l_quit_since >= (uint32_t) g_cfg.quit_hold_ms)
            {
                l_quit_since = 0;
                begin_quit();
            }
        }
        else
            l_quit_since = 0;
    }
    return open;
}

/* ---- the menu loop -------------------------------------------------------------------------- */

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

typedef struct {
    int  busy;            /* a save is in flight: show "Saving slot N..." */
    char msg[64];         /* status line */
    int  kind;
    int  states;          /* open on the save-state carousel */
} menu_entry;

/* returns 1 if the menu ran (and the frame was presented here), 0 if it couldn't open */
static int run_menu(const menu_entry *entry)
{
    int w = 0, h = 0;
    menu_result result = MENU_NONE;
    int paused_by_us = 0, quit = 0;
    pad_tracker tr;
    uint32_t t_frame, loop_prev, t_capture;
    int entered_ui = 0;
    dbg_tick_t dt;

    video_drawable_size(&w, &h);
    if (w <= 0 || h <= 0)
        return 0;

    /* 1. freeze: copy the frame that is about to be presented (game context is current) */
    t_capture = SDL_GetTicks();
    if (ugl_capture_frame(w, h) != 0)
    {
        DebugMessage(M64MSG_WARNING, "couldn't capture the game frame; not opening the menu");
        return 0;
    }
    t_capture = SDL_GetTicks() - t_capture;

    /* 2. pause the core (state bookkeeping + any threaded plugin), then become the menu thread */
    ff_set(0);
    toast_clear();
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
    ugl_background_upload();
    menu_set_quit_saves(g_cfg.autosave_on_quit);
    if (entry != NULL && entry->states)
        menu_open_states(cur_slot(), entry->busy, entry->msg, entry->kind);
    else
        menu_open(cur_slot());
    inputfilter_force(1);
    DebugMessage(M64MSG_VERBOSE, "menu opened at frame %u (%dx%d, frame capture %u ms)", l_frames, w, h, (unsigned) t_capture);

    /* buttons held right now (the combo that opened us) count as already pressed */
    loop_prev = pad_poll();
    pad_tracker_reset(&tr, loop_prev);

    /* 3. the menu loop: we own the window and the (private) GL context until it ends */
    t_frame = SDL_GetTicks();
    while (!quit && !l_stop_requested && emu_state() != M64EMU_STOPPED)
    {
        uint32_t held, now;
        float brightness = 1.0f - (float) g_cfg.dim_percent / 100.0f;
        const uint8_t *shot = NULL;
        int consumed = 0;

        drain_sdl_events(&quit);
        dbg_tick(&dt);
        pad_inject(dt.buttons);
        if (dt.cap)
            pad_capture_inject(&dt.capsrc);
        held = pad_poll();
        now = SDL_GetTicks();
        pad_tracker_update(&tr, held, now);

        {
            char msg[64];
            int kind = 0;
            if (job_poll(msg, sizeof(msg), &kind) && menu_busy())
                menu_busy_done(msg, kind);
        }

        if (dt.kill)
        {
            quit = 1;
        }
        else if (dt.restart)
        {
            result = MENU_RESTART;
        }
        else if (dt.quit)
        {
            result = MENU_QUIT;
        }
        else if (dt.toggle_menu || (!menu_busy() && combo_edge(held, loop_prev) && !(consumed = menu_combo_pressed(held))))
        {
            result = MENU_RESUME;
        }
        else
        {
            video_drawable_size(&w, &h);
            ugl_begin(w, h);
            ugl_background_draw(w, h, brightness);
            /* the buttons that confirmed a new menu combo are not also presses on the page underneath */
            result = menu_frame(w, h, (menu_busy() || consumed) ? 0 : tr.pressed, (menu_busy() || consumed) ? 0 : held);
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
    menu_closing();                                  /* reverts unconfirmed combo changes, writes pending config */

    /* 4. leaving: show one clean (undimmed) frame so the game doesn't resume over a menu.  The buttons that
     *    closed the menu are hidden from the game by the input filter until they are released; if the filter
     *    is off (or the input plugin bypasses it), keep the frame up until they are, as before. */
    {
        uint32_t t0 = SDL_GetTicks();
        int wait_release = !(g_cfg.block_game_input);
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

    /* 5. act on the choice.  Everything below is issued while the core is still paused; the core runs it from
     *    its own interrupt handler once it executes again (so the job outlives this call; see job_poll()). */
    if (result == MENU_NONE || result == MENU_RESUME || result == MENU_RESET)
    {
        int sel = menu_selected_slot();
        if (sel >= 0)
            set_slot(sel);               /* the carousel's slot becomes the quick-save slot */
    }
    if (quit || l_stop_requested > 1)
    {
        l_quitting = 1;
        emu_stop();                                  /* clears the pause itself */
        paused_by_us = 0;
    }
    else if (result == MENU_QUIT || result == MENU_RESTART || l_stop_requested)
    {
        if (result == MENU_RESTART)
            l_restart = 1;
        /* with auto-save on, the game must run a moment to take the snapshot: resume first */
        if (paused_by_us && emu_state() == M64EMU_PAUSED)
            (*CoreDoCommand)(M64CMD_RESUME, 0, NULL);
        paused_by_us = 0;
        begin_quit();
    }
    else if (result == MENU_RESET)
    {
        DebugMessage(M64MSG_INFO, "resetting the console");
        (*CoreDoCommand)(M64CMD_RESET, 1, NULL);     /* 1 = hard reset (power cycle) */
    }
    else if (result == MENU_SAVE_STATE)
    {
        if (job_busy())
            toast_show(TOAST_ERROR, "Busy, try again");
        else if (start_save(menu_action_slot(), 1, 0) != 0)
            toast_show(TOAST_ERROR, "Saving slot %d failed", menu_action_slot());
    }
    else if (result == MENU_LOAD_STATE)
    {
        if (job_busy())
            toast_show(TOAST_ERROR, "Busy, try again");
        else if (start_load(menu_action_slot(), 0) != 0)
            toast_show(TOAST_ERROR, "Loading slot %d failed", menu_action_slot());
    }
    if (paused_by_us && emu_state() == M64EMU_PAUSED)
        (*CoreDoCommand)(M64CMD_RESUME, 0, NULL);
    DebugMessage(M64MSG_VERBOSE, "menu closed");
    l_prev_held = pad_poll();
    return 1;
}

/* ---- per-frame hook -------------------------------------------------------------------------- */

/* test hook (debug script "pjsave:N" / "pjzip:N"): the core writes a Project64 state to slot N's .pjN file */
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
    menu_entry entry;

    if (SDL_AtomicGet(&l_menu_open))
        return 0;                                    /* re-entrancy guard */
    l_frames++;
    if (l_frames == 3)
        profile_apply_volume();                      /* the audio plugin is up: this game's volume */
    if (l_frames == 300 && remap_health_check() < 0)
    {
        DebugMessage(M64MSG_WARNING, "the input plugin never read the controller through SDL_JoystickGet*(): "
                     "the controller remap has no effect with this plugin");
        toast_show(TOAST_ERROR, "Remap inactive: input plugin bypasses it");
    }
    now = SDL_GetTicks();

    if (l_stop_requested)
    {
        if (l_stop_requested > 1 || !g_cfg.autosave_on_quit)
            emu_stop();
        else
            begin_quit();
        if (l_stop_sent)
            return 0;
    }

    dbg_tick(&dt);
    pad_inject(dt.buttons);
    held = pad_poll();

    /* a save/load issued earlier */
    memset(&entry, 0, sizeof(entry));
    if (job_busy())
    {
        char msg[64];
        int kind = 0;
        int from_menu = l_job.from_menu;
        l_job.frames++;
        if (job_poll(msg, sizeof(msg), &kind))
        {
            if (from_menu)
            {
                entry.states = 1;
                snprintf(entry.msg, sizeof(entry.msg), "%s", msg);
                entry.kind = kind;
                open = 2;
            }
            else
                toast_show(kind > 0 ? TOAST_OK : TOAST_ERROR, "%s", msg);
        }
        else if (from_menu && l_job.frames >= MENU_REOPEN_FRAMES && !l_quitting)
        {
            entry.states = 1;
            entry.busy = 1;
            open = 2;
        }
    }

    if (dt.stat)
        DebugMessage(M64MSG_INFO, "stat: frame %u t=%u ms", l_frames, (unsigned) now);
    if (dt.kill)
        emu_stop();
    if (dt.restart)
    {
        l_restart = 1;
        begin_quit();
    }
    if (dt.quit)
        begin_quit();
    if (dt.pj_slot >= 0)
        debug_save_pj(dt.pj_slot, dt.pj_zip);
    if (dt.shot)
        want_shot = 1;
    if (open == 0 && !l_quitting)
    {
        if (dt.toggle_menu || combo_edge(held, l_prev_held))
            open = 1;
        else if (hotkeys_update(held, l_prev_held, now))
            open = 1;
    }
    else if (open == 0)
        hotkeys_update(0, 0, now);                   /* keeps fast-forward off while quitting */
    l_prev_held = held;

    if (open)
    {
        if (open == 2 || open == 1)
        {
            if (run_menu(open == 2 ? &entry : NULL))
                return 1;
        }
    }
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
