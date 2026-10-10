/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - plugin_api.c
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
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/* Relies on the ui-console order: PluginStartup() runs before the video plugin calls VidExt_*, so suxty's VidExt
 * override is in
 * place in time. CoreAttachPlugin(INPUT) needs an open ROM, so everything game-dependent (name, MD5, settings, pads)
 * is set up
 * in InitiateControllers(). */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "cfg.h"
#include "core_interface.h"
#include "debug.h"
#include "foir_if.h"
#include "m64p_plugin.h"
#include "m64p_types.h"
#include "main.h"
#include "menu.h"
#include "pad.h"
#include "padmap.h"
#include "remap.h"
#include "session.h"
#include "states.h"
#include "version.h"
#include "video.h"

#define PLUGIN_NAME     "Mupen64Plus Suxty Input Plugin (in-game menu)"
#define INPUT_PLUGIN_API_VERSION 0x020100

int g_Verbose = 0;

static void (*l_debug_cb)(void *, int, const char *);
static void *l_debug_ctx;
static int   l_started;
static int   l_session_up;
static int   l_log_keys;

void DebugMessage(int level, const char *message, ...)
{
    char buf[1024];
    va_list args;

    va_start(args, message);
    vsnprintf(buf, sizeof(buf), message, args);
    va_end(args);
    DebugCallback(l_debug_ctx, level, buf);
}

void DebugCallback(void *Context, int level, const char *message)
{
    char buf[1100];
    if (level == M64MSG_VERBOSE && !g_Verbose)
        return;
    if (l_debug_cb != NULL)
    {
        snprintf(buf, sizeof(buf), "suxty: %s", message);
        l_debug_cb(Context, level == M64MSG_VERBOSE ? M64MSG_INFO : level, buf);
    }
    else
        fprintf(stderr, "suxty: %s\n", message);
}

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context, void (*Callback)(void *, int, const char *))
{
    m64p_error rval;
    const char *cfgdir;

    if (l_started)
        return M64ERR_ALREADY_INIT;
    l_debug_cb = Callback;
    l_debug_ctx = Context;
    g_Verbose = SDL_getenv("SUXTY_VERBOSE") != NULL;
    l_log_keys = SDL_getenv("SUXTY_LOG_KEYS") != NULL;

    rval = AttachCoreHandle(CoreLibHandle);
    if (rval != M64ERR_SUCCESS)
        return rval;

    cfgdir = (*ConfigGetUserConfigPath)();
    if (cfgdir == NULL || cfg_init(cfgdir) != 0)
    {
        DebugMessage(M64MSG_ERROR, "couldn't set up suxty configuration");
        DetachCoreLib();
        return M64ERR_FILES;
    }

    /* take over the window, GL context and buffer swap before the video plugin's InitiateGFX / RomOpen call VidExt_* */
    rval = video_install();
    if (rval != M64ERR_SUCCESS)
    {
        DebugMessage(M64MSG_ERROR, "couldn't install the VidExt override (core error %i)", (int) rval);
        cfg_shutdown();
        DetachCoreLib();
        return rval;
    }
    DebugMessage(M64MSG_INFO, "version %i.%i.%i: VidExt override installed", VERSION_PRINTF_SPLIT(SUXTY_VERSION));
    l_started = 1;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void)
{
    if (!l_started)
        return M64ERR_NOT_INIT;
    if (l_session_up)
    {
        session_shutdown();
        l_session_up = 0;
    }
    fif_release();
    video_uninstall();               /* before the core can call a function of a library that is about to be dlclose()d */
    cfg_shutdown();
    DetachCoreLib();
    l_debug_cb = NULL;
    l_started = 0;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion, int *APIVersion, const char **PluginNamePtr, int *Capabilities)
{
    if (PluginType != NULL)
        *PluginType = M64PLUGIN_INPUT;
    if (PluginVersion != NULL)
        *PluginVersion = SUXTY_VERSION;
    if (APIVersion != NULL)
        *APIVersion = INPUT_PLUGIN_API_VERSION;
    if (PluginNamePtr != NULL)
        *PluginNamePtr = PLUGIN_NAME;
    if (Capabilities != NULL)
        *Capabilities = 0;
    return M64ERR_SUCCESS;
}

/* the ROM is open; the video and audio plugins are attached but have not opened it */
EXPORT void CALL InitiateControllers(CONTROL_INFO ControlInfo)
{
    int i;
    m64p_rom_header header;
    m64p_rom_settings settings;
    char name[21];

    if (!l_started || l_session_up)
        return;

    memset(&header, 0, sizeof(header));
    memset(&settings, 0, sizeof(settings));
    name[0] = 0;
    if ((*CoreDoCommand)(M64CMD_ROM_GET_HEADER, sizeof(header), &header) == M64ERR_SUCCESS)
    {
        memcpy(name, header.Name, 20);
        name[20] = 0;
        menu_set_game_name(name);
    }
    if ((*CoreDoCommand)(M64CMD_ROM_GET_SETTINGS, sizeof(settings), &settings) == M64ERR_SUCCESS)
        DebugMessage(M64MSG_VERBOSE, "ROM md5 %s  (%s)", settings.MD5, settings.goodname);
    states_init(name, settings.goodname, settings.MD5);
    cfg_load_game(settings.MD5);

    if (session_init() != 0)
        DebugMessage(M64MSG_WARNING, "session init failed; the in-game menu will not work");
    pad_enable_keyboard(SDL_getenv("SUXTY_KEYBOARD") != NULL);
    {
        const char *oa = SDL_getenv("SUXTY_DEBUG_OPEN_AFTER");
        dbg_configure(SDL_getenv("SUXTY_DEBUG_SCRIPT"), SDL_getenv("SUXTY_DEBUG_SHOT_PREFIX"), oa != NULL ? atoi(oa) : 0);
    }
    {
        char combo[64];
        pad_format_combo(g_cfg.menu_combo, combo, sizeof(combo));
        DebugMessage(M64MSG_INFO, "menu combo: %s", combo);
    }

    /* hand foir this game's stored options before it opens the ROM */
    if (fif_discover())
        fif_apply_config();

    remap_prepare_launch();
    for (i = 0; i < 4; i++)
    {
        ControlInfo.Controls[i].Present = (i == 0 || remap_player_pad(i) >= 0) ? 1 : 0;
        ControlInfo.Controls[i].RawData = 0;
        ControlInfo.Controls[i].Plugin = PLUGIN_MEMPAK;       /* the core's own Controller Pak (no rumble) */
        ControlInfo.Controls[i].Type = CONT_TYPE_STANDARD;
    }
    l_session_up = 1;
    DebugMessage(M64MSG_INFO, "controllers initiated");
}

EXPORT void CALL ControllerCommand(int Control, unsigned char *Command)
{
    (void) Control; (void) Command;
}

EXPORT void CALL ReadController(int Control, unsigned char *Command)
{
    (void) Control; (void) Command;
}

static int axis80(int v)
{
    v = v * 80 / 32767;
    return v > 80 ? 80 : v < -80 ? -80 : v;
}

/* runs on the emulation thread (the one that called M64CMD_EXECUTE), as does the swap that runs the menu */
EXPORT void CALL GetKeys(int Control, BUTTONS *Keys)
{
    static unsigned last[4] = { ~0u, ~0u, ~0u, ~0u };
    n64_state st;
    BUTTONS k;

    k.Value = 0;
    if (l_session_up && Control >= 0 && Control < 4)
    {
        if (Control == 0)
            pad_refresh();
        if (remap_player_state(Control, &st))
        {
            uint32_t b = st.buttons;
            k.A_BUTTON = (b >> N64_A) & 1;       k.B_BUTTON = (b >> N64_B) & 1;       k.Z_TRIG = (b >> N64_Z) & 1;
            k.START_BUTTON = (b >> N64_START) & 1;
            k.L_TRIG = (b >> N64_L) & 1;         k.R_TRIG = (b >> N64_R) & 1;
            k.U_DPAD = (b >> N64_DUP) & 1;       k.D_DPAD = (b >> N64_DDOWN) & 1;
            k.L_DPAD = (b >> N64_DLEFT) & 1;     k.R_DPAD = (b >> N64_DRIGHT) & 1;
            k.U_CBUTTON = (b >> N64_CUP) & 1;    k.D_CBUTTON = (b >> N64_CDOWN) & 1;
            k.L_CBUTTON = (b >> N64_CLEFT) & 1;  k.R_CBUTTON = (b >> N64_CRIGHT) & 1;
            k.X_AXIS = axis80(st.sx);
            k.Y_AXIS = axis80(-st.sy);           /* SDL: +y is down; N64: +y is up */
        }
        if (l_log_keys && k.Value != last[Control])
        {
            last[Control] = k.Value;
            DebugMessage(M64MSG_INFO, "GetKeys(%d): %08x  buttons=%04x stick=(%d,%d)", Control, k.Value, k.Value & 0xffff,
                         (int) k.X_AXIS, (int) k.Y_AXIS);
        }
    }
    Keys->Value = k.Value;
}

EXPORT int CALL RomOpen(void)
{
    DebugMessage(M64MSG_VERBOSE, "RomOpen");
    return 1;
}

EXPORT void CALL RomClosed(void)
{
    /* the video plugin's RomClosed (VidExt_Quit -> our window teardown) is independent of this one; do not touch GL
     * here.
     * foir may already be gone from here on: never call its interface again */
    DebugMessage(M64MSG_VERBOSE, "RomClosed");
    fif_release();
    if (l_session_up)
        remap_finish();
}

/* the core handles its own keyboard hotkeys */
EXPORT void CALL SDL_KeyDown(int keymod, int keysym) { (void) keymod; (void) keysym; }
EXPORT void CALL SDL_KeyUp(int keymod, int keysym)   { (void) keymod; (void) keysym; }

/* the menu is drawn from the buffer swap instead */
EXPORT void CALL RenderCallback(void) { }
