/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-ui-suxty - main.c
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


/* Application entry point for the suxty gamepad front-end.  Structured after
 * mupen64plus-ui-console: load the core, start it, load the plugins, run the ROM.
 * What is different: suxty installs a VidExt override (video.c) and runs a
 * pause-menu session (session.c) inside the frame presentation path.
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <SDL.h>

#include "cfg.h"
#include "core_interface.h"
#include "debug.h"
#include "m64p_types.h"
#include "main.h"
#include "menu.h"
#include "osal_files.h"
#include "osal_preproc.h"
#include "pad.h"
#include "plugin.h"
#include "profile.h"
#include "remap.h"
#include "session.h"
#include "states.h"
#include "version.h"
#include "video.h"

/* Version number for the UI-Suxty section in mupen64plus.cfg */
#define CONFIG_PARAM_VERSION     1.00

/** global variables **/
int g_Verbose = 0;

/** static (local) variables **/
static m64p_handle l_ConfigCore = NULL;
static m64p_handle l_ConfigVideo = NULL;
static m64p_handle l_ConfigUI = NULL;

static const char *l_CoreLibPath = NULL;
static const char *l_ConfigDirPath = NULL;
static const char *l_ROMFilepath = NULL;
static const char *l_GamepadDb = NULL;
static int         l_SaveOptions = 1;
static int         l_Resume = 0;
static int         l_Keyboard = 0;

#if defined(SHAREDIR)
  static const char *l_DataDirPath = SHAREDIR;
#else
  static const char *l_DataDirPath = NULL;
#endif

/* debug / test hooks */
static const char *l_DbgScript = NULL;
static const char *l_DbgScriptRestart = NULL;
static const char *l_DbgShotPrefix = NULL;
static int         l_DbgOpenAfter = 0;

/*********************************************************************************************************
 *  Callback functions from the core
 */

void DebugMessage(int level, const char *message, ...)
{
    char msgbuf[1024];
    va_list args;

    va_start(args, message);
    vsnprintf(msgbuf, sizeof(msgbuf), message, args);
    va_end(args);

    DebugCallback("suxty", level, msgbuf);
}

void DebugCallback(void *Context, int level, const char *message)
{
    session_core_message(level, message);
    if (level == M64MSG_ERROR)
        printf("%s Error: %s\n", (const char *) Context, message);
    else if (level == M64MSG_WARNING)
        printf("%s Warning: %s\n", (const char *) Context, message);
    else if (level == M64MSG_INFO)
        printf("%s: %s\n", (const char *) Context, message);
    else if (level == M64MSG_STATUS)
        printf("%s Status: %s\n", (const char *) Context, message);
    else if (level == M64MSG_VERBOSE)
    {
        if (g_Verbose)
            printf("%s: %s\n", (const char *) Context, message);
    }
    else
        printf("%s Unknown: %s\n", (const char *) Context, message);
    fflush(stdout);
}

static void StateCallback(void *Context, m64p_core_param ParamChanged, int NewValue)
{
    (void) Context;
    session_core_state(ParamChanged, NewValue);
}

static void on_signal(int sig)
{
    (void) sig;
    session_request_stop();
}

/*********************************************************************************************************
 *  Configuration handling
 */

static m64p_error OpenConfigurationHandles(void)
{
    float fConfigParamsVersion;
    m64p_error rval;

    rval = (*ConfigOpenSection)("Core", &l_ConfigCore);
    if (rval != M64ERR_SUCCESS)
    {
        DebugMessage(M64MSG_ERROR, "failed to open 'Core' configuration section");
        return rval;
    }
    rval = (*ConfigOpenSection)("Video-General", &l_ConfigVideo);
    if (rval != M64ERR_SUCCESS)
    {
        DebugMessage(M64MSG_ERROR, "failed to open 'Video-General' configuration section");
        return rval;
    }
    rval = (*ConfigOpenSection)("UI-Suxty", &l_ConfigUI);
    if (rval != M64ERR_SUCCESS)
    {
        DebugMessage(M64MSG_ERROR, "failed to open 'UI-Suxty' configuration section");
        return rval;
    }

    if ((*ConfigGetParameter)(l_ConfigUI, "Version", M64TYPE_FLOAT, &fConfigParamsVersion, sizeof(float)) != M64ERR_SUCCESS)
    {
        DebugMessage(M64MSG_INFO, "No version number in 'UI-Suxty' config section. Setting defaults.");
        (*ConfigDeleteSection)("UI-Suxty");
        (*ConfigOpenSection)("UI-Suxty", &l_ConfigUI);
    }
    else if (((int) fConfigParamsVersion) != ((int) CONFIG_PARAM_VERSION))
    {
        DebugMessage(M64MSG_WARNING, "Incompatible version %.2f in 'UI-Suxty' config section: current is %.2f. Setting defaults.",
                     fConfigParamsVersion, (float) CONFIG_PARAM_VERSION);
        (*ConfigDeleteSection)("UI-Suxty");
        (*ConfigOpenSection)("UI-Suxty", &l_ConfigUI);
    }

    (*ConfigSetDefaultFloat)(l_ConfigUI, "Version", CONFIG_PARAM_VERSION, "mupen64plus-ui-suxty config parameter set version number.  Please don't change this version number.");
    (*ConfigSetDefaultString)(l_ConfigUI, "PluginDir", OSAL_CURRENT_DIR, "Directory in which to search for plugins");
    (*ConfigSetDefaultString)(l_ConfigUI, "VideoPlugin", "mupen64plus-video-rice" OSAL_DLL_EXTENSION, "Filename of video plugin");
    (*ConfigSetDefaultString)(l_ConfigUI, "AudioPlugin", "mupen64plus-audio-sdl" OSAL_DLL_EXTENSION, "Filename of audio plugin");
    (*ConfigSetDefaultString)(l_ConfigUI, "InputPlugin", "mupen64plus-input-sdl" OSAL_DLL_EXTENSION, "Filename of input plugin");
    (*ConfigSetDefaultString)(l_ConfigUI, "RspPlugin", "mupen64plus-rsp-hle" OSAL_DLL_EXTENSION, "Filename of RSP plugin");
    return M64ERR_SUCCESS;
}

static void SaveConfigurationOptions(void)
{
    /* if any plugins were specified on the command line, remember them in the config file */
    if (g_PluginDir != NULL)
        (*ConfigSetParameter)(l_ConfigUI, "PluginDir", M64TYPE_STRING, g_PluginDir);
    if (g_GfxPlugin != NULL)
        (*ConfigSetParameter)(l_ConfigUI, "VideoPlugin", M64TYPE_STRING, g_GfxPlugin);
    if (g_AudioPlugin != NULL)
        (*ConfigSetParameter)(l_ConfigUI, "AudioPlugin", M64TYPE_STRING, g_AudioPlugin);
    if (g_InputPlugin != NULL)
        (*ConfigSetParameter)(l_ConfigUI, "InputPlugin", M64TYPE_STRING, g_InputPlugin);
    if (g_RspPlugin != NULL)
        (*ConfigSetParameter)(l_ConfigUI, "RspPlugin", M64TYPE_STRING, g_RspPlugin);

    profile_save_file();
}

/*********************************************************************************************************
 *  Command-line handling
 */

static void printUsage(const char *progname)
{
    printf("Usage: %s [parameters] [--resume] romfile\n"
           "\n"
           "Parameters:\n"
           "    --corelib (filepath)   : use core library (filepath) (can be only filename or full path)\n"
           "    --configdir (dirpath)  : force configation directory; suxty.cfg lives in (dirpath)/suxty/\n"
           "    --datadir (dirpath)    : search for shared data files (.ini files, etc) in (dirpath)\n"
           "    --plugindir (dirpath)  : search for plugins in (dirpath)\n"
           "    --gfx (plugin-spec)    : use gfx plugin given by (plugin-spec)\n"
           "    --audio (plugin-spec)  : use audio plugin given by (plugin-spec)\n"
           "    --input (plugin-spec)  : use input plugin given by (plugin-spec)\n"
           "    --rsp (plugin-spec)    : use rsp plugin given by (plugin-spec)\n"
           "    --emumode (mode)       : set emu mode to: 0=Pure Interpreter 1=Interpreter 2=DynaRec\n"
           "    --fullscreen           : use fullscreen display mode\n"
           "    --windowed             : use windowed display mode\n"
           "    --resolution (res)     : display resolution (ex: 640x480)\n"
           "    --osd / --noosd        : enable / disable the core's on-screen display\n"
           "    --set (param-spec)     : set a configuration variable, format: ParamSection[ParamName]=Value\n"
           "    --resume               : load the auto-save state (saved when you quit) if one exists\n"
           "    --gamecontrollerdb (f) : add SDL game controller mappings from file (f)\n"
           "    --keyboard             : also accept keyboard input in the menu (desktop development aid)\n"
           "    --nosaveoptions        : do not save the given command-line options in configuration file\n"
           "    --verbose              : print lots of information\n"
           "    --help                 : see this help message\n"
           "\n"
           "Test hooks (no controller needed):\n"
           "    --debug-script (s)         : e.g. \"wait300,menu,wait30,shot,down,shot,b,wait20,quit\"\n"
           "                                 (tokens: waitN menu shot quit kill, buttons, a+b combos, holdN:buttons,\n"
           "                                  cap:button:3 / cap:hat:0:up / cap:axis:2+ answer the remap capture prompt)\n"
           "    --debug-shot-prefix (path) : where 'shot' writes PNGs (path-1.png, path-2.png, ...)\n"
           "    --debug-open-menu-after N  : open the menu after N presented frames\n"
           "    --debug-job-delay MS       : make save/load jobs look MS milliseconds slower (shows the 'Saving...' page)\n"
           "    --debug-script-restart (s) : the script the game runs after a menu 'Restart game' (it starts with --resume)\n"
           "\n"
           "(plugin-spec):\n"
           "    (pluginname)           : filename (without path) of plugin to find in plugin directory\n"
           "    (pluginpath)           : full path and filename of plugin\n"
           "    'dummy'                : use dummy plugin\n"
           "\n"
           "In the game, the menu opens with the configured combo (default: Select+Start, see suxty.cfg).\n"
           "Hotkeys (default, hold Select): R quick-save, L quick-load, Right/Left next/previous slot,\n"
           "Y fast-forward, B (hold) quit.  All configurable in suxty.cfg [hotkeys].\n"
           "Per-game settings (menu: 'Apply to: This game') live in (configdir)/suxty/games/(rom md5).cfg and win over the\n"
           "global ones; command-line options win over both.\n"
           "\n", progname);
}

static int SetConfigParameter(const char *ParamSpec)
{
    char *ParsedString, *VarName, *VarValue = NULL;
    m64p_handle ConfigSection;
    m64p_type VarType;
    m64p_error rval;

    if (ParamSpec == NULL)
    {
        DebugMessage(M64MSG_ERROR, "ParamSpec is NULL in SetConfigParameter()");
        return 1;
    }
    ParsedString = strdup(ParamSpec);
    if (ParsedString == NULL)
    {
        DebugMessage(M64MSG_ERROR, "SetConfigParameter() couldn't allocate memory for temporary string.");
        return 2;
    }

    /* parse it for the simple section[name]=value format */
    VarName = strchr(ParsedString, '[');
    if (VarName != NULL)
    {
        *VarName++ = 0;
        VarValue = strchr(VarName, ']');
        if (VarValue != NULL)
            *VarValue++ = 0;
    }
    if (VarName == NULL || VarValue == NULL || *VarValue != '=')
    {
        DebugMessage(M64MSG_ERROR, "invalid (param-spec) '%s'", ParamSpec);
        free(ParsedString);
        return 3;
    }
    VarValue++;

    rval = (*ConfigOpenSection)(ParsedString, &ConfigSection);
    if (rval != M64ERR_SUCCESS)
    {
        DebugMessage(M64MSG_ERROR, "SetConfigParameter failed to open config section '%s'", ParsedString);
        free(ParsedString);
        return 4;
    }
    if ((*ConfigGetParameterType)(ConfigSection, VarName, &VarType) == M64ERR_SUCCESS)
    {
        int ValueInt;
        float ValueFloat;
        switch (VarType)
        {
            case M64TYPE_INT:
                ValueInt = atoi(VarValue);
                (*ConfigSetParameter)(ConfigSection, VarName, M64TYPE_INT, &ValueInt);
                break;
            case M64TYPE_FLOAT:
                ValueFloat = (float) atof(VarValue);
                (*ConfigSetParameter)(ConfigSection, VarName, M64TYPE_FLOAT, &ValueFloat);
                break;
            case M64TYPE_BOOL:
                ValueInt = (int) (osal_insensitive_strcmp(VarValue, "true") == 0);
                (*ConfigSetParameter)(ConfigSection, VarName, M64TYPE_BOOL, &ValueInt);
                break;
            case M64TYPE_STRING:
                (*ConfigSetParameter)(ConfigSection, VarName, M64TYPE_STRING, VarValue);
                break;
            default:
                DebugMessage(M64MSG_ERROR, "invalid VarType in SetConfigParameter()");
                free(ParsedString);
                return 5;
        }
    }
    else
    {
        (*ConfigSetParameter)(ConfigSection, VarName, M64TYPE_STRING, VarValue);
    }

    free(ParsedString);
    return 0;
}

/* Phase 1: everything that must be known before the core starts. */
static int ParseCommandLineInitial(int argc, const char **argv)
{
    int i;
    for (i = 1; i < argc; i++)
    {
        int ArgsLeft = argc - i - 1;

        if (strcmp(argv[i], "--corelib") == 0 && ArgsLeft >= 1)
            l_CoreLibPath = argv[++i];
        else if (strcmp(argv[i], "--configdir") == 0 && ArgsLeft >= 1)
            l_ConfigDirPath = argv[++i];
        else if (strcmp(argv[i], "--datadir") == 0 && ArgsLeft >= 1)
            l_DataDirPath = argv[++i];
        else if (strcmp(argv[i], "--plugindir") == 0 && ArgsLeft >= 1)
            g_PluginDir = argv[++i];
        else if (strcmp(argv[i], "--gamecontrollerdb") == 0 && ArgsLeft >= 1)
            l_GamepadDb = argv[++i];
        else if (strcmp(argv[i], "--debug-script") == 0 && ArgsLeft >= 1)
            l_DbgScript = argv[++i];
        else if (strcmp(argv[i], "--debug-shot-prefix") == 0 && ArgsLeft >= 1)
            l_DbgShotPrefix = argv[++i];
        else if (strcmp(argv[i], "--debug-script-restart") == 0 && ArgsLeft >= 1)
            l_DbgScriptRestart = argv[++i];
        else if (strcmp(argv[i], "--debug-open-menu-after") == 0 && ArgsLeft >= 1)
            l_DbgOpenAfter = atoi(argv[++i]);
        else if (strcmp(argv[i], "--debug-job-delay") == 0 && ArgsLeft >= 1)
            dbg_set_job_delay(atoi(argv[++i]));
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
        {
            printUsage(argv[0]);
            return 1;
        }
        else if (strcmp(argv[i], "--verbose") == 0)
            g_Verbose = 1;
        else if (strcmp(argv[i], "--nosaveoptions") == 0)
            l_SaveOptions = 0;
        else if (strcmp(argv[i], "--resume") == 0)
            l_Resume = 1;
        else if (strcmp(argv[i], "--keyboard") == 0)
            l_Keyboard = 1;
    }
    return 0;
}

/* Phase 2: after the core is up and config sections are open (not --set; that waits for the plugins). */
static m64p_error ParseCommandLineMain(int argc, const char **argv)
{
    int i;

    for (i = 1; i < argc; i++)
    {
        int ArgsLeft = argc - i - 1;

        if (strcmp(argv[i], "--noosd") == 0)
        {
            int v = 0;
            (*ConfigSetParameter)(l_ConfigCore, "OnScreenDisplay", M64TYPE_BOOL, &v);
        }
        else if (strcmp(argv[i], "--osd") == 0)
        {
            int v = 1;
            (*ConfigSetParameter)(l_ConfigCore, "OnScreenDisplay", M64TYPE_BOOL, &v);
        }
        else if (strcmp(argv[i], "--fullscreen") == 0)
        {
            int v = 1;
            (*ConfigSetParameter)(l_ConfigVideo, "Fullscreen", M64TYPE_BOOL, &v);
        }
        else if (strcmp(argv[i], "--windowed") == 0)
        {
            int v = 0;
            (*ConfigSetParameter)(l_ConfigVideo, "Fullscreen", M64TYPE_BOOL, &v);
        }
        else if (strcmp(argv[i], "--emumode") == 0 && ArgsLeft >= 1)
        {
            int emumode = atoi(argv[++i]);
            if (emumode < 0 || emumode > 2)
                DebugMessage(M64MSG_WARNING, "invalid --emumode value '%i'", emumode);
            else
                (*ConfigSetParameter)(l_ConfigCore, "R4300Emulator", M64TYPE_INT, &emumode);
        }
        else if (strcmp(argv[i], "--resolution") == 0 && ArgsLeft >= 1)
        {
            const char *res = argv[++i];
            int xres, yres;
            if (sscanf(res, "%ix%i", &xres, &yres) != 2)
                DebugMessage(M64MSG_WARNING, "couldn't parse resolution '%s'", res);
            else
            {
                (*ConfigSetParameter)(l_ConfigVideo, "ScreenWidth", M64TYPE_INT, &xres);
                (*ConfigSetParameter)(l_ConfigVideo, "ScreenHeight", M64TYPE_INT, &yres);
            }
        }
        else if (strcmp(argv[i], "--gfx") == 0 && ArgsLeft >= 1)
            g_GfxPlugin = argv[++i];
        else if (strcmp(argv[i], "--audio") == 0 && ArgsLeft >= 1)
            g_AudioPlugin = argv[++i];
        else if (strcmp(argv[i], "--input") == 0 && ArgsLeft >= 1)
            g_InputPlugin = argv[++i];
        else if (strcmp(argv[i], "--rsp") == 0 && ArgsLeft >= 1)
            g_RspPlugin = argv[++i];
        else if (strcmp(argv[i], "--set") == 0 && ArgsLeft >= 1)
            i++;       /* applied once the plugins are loaded */
        else if ((strcmp(argv[i], "--corelib") == 0 || strcmp(argv[i], "--configdir") == 0 || strcmp(argv[i], "--datadir") == 0 ||
                  strcmp(argv[i], "--plugindir") == 0 || strcmp(argv[i], "--gamecontrollerdb") == 0 ||
                  strcmp(argv[i], "--debug-script") == 0 || strcmp(argv[i], "--debug-shot-prefix") == 0 ||
                  strcmp(argv[i], "--debug-script-restart") == 0 ||
                  strcmp(argv[i], "--debug-open-menu-after") == 0 || strcmp(argv[i], "--debug-job-delay") == 0) && ArgsLeft >= 1)
            i++;       /* handled in phase 1 */
        else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "--nosaveoptions") == 0 ||
                 strcmp(argv[i], "--resume") == 0 || strcmp(argv[i], "--keyboard") == 0)
            ;          /* handled in phase 1 */
        else if (ArgsLeft == 0 && argv[i][0] != '-')
            l_ROMFilepath = argv[i];
        else
        {
            DebugMessage(M64MSG_ERROR, "unrecognized command-line parameter '%s'", argv[i]);
            return M64ERR_INPUT_INVALID;
        }
    }

    if (l_ROMFilepath == NULL)
    {
        DebugMessage(M64MSG_ERROR, "no ROM filepath given");
        return M64ERR_INPUT_INVALID;
    }
    return M64ERR_SUCCESS;
}

/* Phase 3: plugin-related parameters (--set), applied after plugins have created their defaults. */
static m64p_error ParseCommandLinePlugin(int argc, const char **argv)
{
    int i;
    for (i = 1; i < argc; i++)
    {
        int ArgsLeft = argc - i - 1;
        if (strcmp(argv[i], "--set") == 0 && ArgsLeft >= 1)
        {
            if (SetConfigParameter(argv[i + 1]) != 0)
                return M64ERR_INPUT_INVALID;
            i++;
        }
    }
    return M64ERR_SUCCESS;
}

/*********************************************************************************************************
 *  main
 */

/* "Restart game": everything is shut down; start again with --resume, so the auto-save written on the way out
 * brings the player back to where they were, with the launch-time options (plugin settings) read afresh. */
static void restart_self(int argc, char **argv)
{
    char **nv = (char **) calloc((size_t) argc + 6, sizeof(char *));
    char shot[1100];
    int n = 0, i, have_resume = 0;

    if (nv == NULL)
        return;
    for (i = 0; i < argc; i++)
    {
        int has_val = i + 1 < argc;
        if (i > 0 && has_val && (strcmp(argv[i], "--debug-script") == 0 || strcmp(argv[i], "--debug-script-restart") == 0 ||
                                 strcmp(argv[i], "--debug-open-menu-after") == 0))
        {
            i++;                                  /* the first run's script is over; the restart script replaces it */
            continue;
        }
        if (i > 0 && has_val && strcmp(argv[i], "--debug-shot-prefix") == 0)
        {
            snprintf(shot, sizeof(shot), "%s-restarted", argv[i + 1]);   /* don't overwrite the first run's shots */
            nv[n++] = argv[i];
            nv[n++] = shot;
            i++;
            continue;
        }
        if (strcmp(argv[i], "--resume") == 0)
            have_resume = 1;
        if (i == argc - 1)
        {
            /* the ROM path is last: the new options go before it */
            if (!have_resume)
                nv[n++] = (char *) "--resume";
            if (l_DbgScriptRestart != NULL)
            {
                nv[n++] = (char *) "--debug-script";
                nv[n++] = (char *) l_DbgScriptRestart;
            }
        }
        nv[n++] = argv[i];
    }
    nv[n] = NULL;
    /* what survives an exec: the signal mask and ignored signals.  Start the new run with a clean slate. */
    {
        sigset_t cur, none;
        int sig;
        pthread_sigmask(SIG_BLOCK, NULL, &cur);
        for (sig = 1; sig < NSIG; sig++)
            if (sigismember(&cur, sig) == 1)
                printf("%s: signal %d was blocked at restart\n", SUXTY_NAME, sig);
        sigemptyset(&none);
        pthread_sigmask(SIG_SETMASK, &none, NULL);
        for (sig = 1; sig < NSIG; sig++)
            if (sig != SIGKILL && sig != SIGSTOP)
                signal(sig, SIG_DFL);
    }
    /* nothing the old run opened (SDL's input devices, the display connection ...) should leak into the new one */
    {
        int fd;
        long maxfd = sysconf(_SC_OPEN_MAX);
        if (maxfd < 0 || maxfd > 4096)
            maxfd = 4096;
        for (fd = 3; fd < maxfd; fd++)
        {
            int fl = fcntl(fd, F_GETFD);
            if (fl >= 0)
                fcntl(fd, F_SETFD, fl | FD_CLOEXEC);
        }
    }
    fflush(stdout);
    printf("%s: restarting\n", SUXTY_NAME);
    fflush(stdout);
    execv("/proc/self/exe", nv);
    execvp(argv[0], nv);
    fprintf(stderr, "suxty: couldn't restart (%s)\n", strerror(errno));
    free(nv);
}

static void shutdown_all(void)
{
    (*CoreShutdown)();
    DetachCoreLib();
}

int main(int argc, char *argv[])
{
    int i;
    unsigned char *ROM_buffer;
    long romlength;
    FILE *fPtr;
    m64p_error rval;
    const char *cfgdir;

    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("%s version %i.%i.%i  (gamepad front-end for Mupen64Plus)\n\n", SUXTY_NAME, VERSION_PRINTF_SPLIT(SUXTY_VERSION));

    if (ParseCommandLineInitial(argc, (const char **) argv) != 0)
        return 1;

    /* SDL must not hijack SIGINT/SIGTERM: we turn them into a clean M64CMD_STOP ourselves */
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    /* load the Mupen64Plus core library */
    if (AttachCoreLib(l_CoreLibPath) != M64ERR_SUCCESS)
        return 2;

    /* start the core; it loads mupen64plus.cfg from the config dir */
    rval = (*CoreStartup)(CORE_API_VERSION, l_ConfigDirPath, l_DataDirPath, "Core", DebugCallback, NULL, StateCallback);
    if (rval != M64ERR_SUCCESS)
    {
        DebugMessage(M64MSG_ERROR, "couldn't start Mupen64Plus core library.");
        DetachCoreLib();
        return 3;
    }

    /* suxty's own config: <configdir>/suxty/suxty.cfg, wherever the core put its config dir */
    cfgdir = (*ConfigGetUserConfigPath)();
    if (cfgdir == NULL || cfg_init(cfgdir) != 0)
    {
        DebugMessage(M64MSG_ERROR, "couldn't set up suxty configuration");
        shutdown_all();
        return 3;
    }
    DebugMessage(M64MSG_INFO, "config dir: %s", g_cfg.config_dir);

    /* take over the window, the GL context and the buffer swap */
    rval = video_install();
    if (rval != M64ERR_SUCCESS)
    {
        DebugMessage(M64MSG_ERROR, "couldn't install the VidExt override (core error %i)", (int) rval);
        cfg_shutdown();
        shutdown_all();
        return 14;
    }

    rval = OpenConfigurationHandles();
    if (rval != M64ERR_SUCCESS)
    {
        cfg_shutdown();
        shutdown_all();
        return 4;
    }

    rval = ParseCommandLineMain(argc, (const char **) argv);
    if (rval != M64ERR_SUCCESS)
    {
        printUsage(argv[0]);
        cfg_shutdown();
        shutdown_all();
        return 5;
    }

    if (l_SaveOptions)
        SaveConfigurationOptions();

    /* load ROM image */
    fPtr = fopen(l_ROMFilepath, "rb");
    if (fPtr == NULL)
    {
        DebugMessage(M64MSG_ERROR, "couldn't open ROM file '%s' for reading.", l_ROMFilepath);
        cfg_shutdown();
        shutdown_all();
        return 7;
    }
    fseek(fPtr, 0L, SEEK_END);
    romlength = ftell(fPtr);
    fseek(fPtr, 0L, SEEK_SET);
    ROM_buffer = (unsigned char *) malloc(romlength > 0 ? romlength : 1);
    if (ROM_buffer == NULL)
    {
        DebugMessage(M64MSG_ERROR, "couldn't allocate %li-byte buffer for ROM image file '%s'.", romlength, l_ROMFilepath);
        fclose(fPtr);
        cfg_shutdown();
        shutdown_all();
        return 8;
    }
    if (fread(ROM_buffer, 1, romlength, fPtr) != (size_t) romlength)
    {
        DebugMessage(M64MSG_ERROR, "couldn't read %li bytes from ROM image file '%s'.", romlength, l_ROMFilepath);
        free(ROM_buffer);
        fclose(fPtr);
        cfg_shutdown();
        shutdown_all();
        return 9;
    }
    fclose(fPtr);

    if ((*CoreDoCommand)(M64CMD_ROM_OPEN, (int) romlength, ROM_buffer) != M64ERR_SUCCESS)
    {
        DebugMessage(M64MSG_ERROR, "core failed to open ROM image file '%s'.", l_ROMFilepath);
        free(ROM_buffer);
        cfg_shutdown();
        shutdown_all();
        return 10;
    }
    free(ROM_buffer);

    /* ROM identity: the menu shows the name; the MD5 keys per-game configs from M4 on */
    {
        m64p_rom_header header;
        m64p_rom_settings settings;
        memset(&header, 0, sizeof(header));
        memset(&settings, 0, sizeof(settings));
        char name[21];
        name[0] = 0;
        if ((*CoreDoCommand)(M64CMD_ROM_GET_HEADER, sizeof(header), &header) == M64ERR_SUCCESS)
        {
            memcpy(name, header.Name, 20);
            name[20] = 0;
            menu_set_game_name(name);
        }
        if ((*CoreDoCommand)(M64CMD_ROM_GET_SETTINGS, sizeof(settings), &settings) == M64ERR_SUCCESS)
            DebugMessage(M64MSG_VERBOSE, "ROM md5 %s  (%s)", settings.MD5, settings.goodname);
        /* the save-state files are named after the ROM, exactly like the core does it */
        states_init(name, settings.goodname, settings.MD5);
        /* the per-game layer: <configdir>/suxty/games/<md5>.cfg */
        cfg_load_game(settings.MD5);
    }

    /* plugins chosen for this game (a command-line choice wins) */
    if (g_GfxPlugin == NULL)    g_GfxPlugin = profile_plugin_choice("video");
    if (g_AudioPlugin == NULL)  g_AudioPlugin = profile_plugin_choice("audio");
    if (g_InputPlugin == NULL)  g_InputPlugin = profile_plugin_choice("input");
    if (g_RspPlugin == NULL)    g_RspPlugin = profile_plugin_choice("rsp");

    /* search for and load plugins */
    rval = PluginSearchLoad(l_ConfigUI);
    if (rval != M64ERR_SUCCESS)
    {
        (*CoreDoCommand)(M64CMD_ROM_CLOSE, 0, NULL);
        cfg_shutdown();
        shutdown_all();
        return 12;
    }

    /* this game's core/plugin options: after the plugins have created their defaults and before anything reads them
     * (profile.h), then the command line again, which wins over the game; whatever it changed is then not ours to restore */
    profile_apply();
    ParseCommandLineMain(argc, (const char **) argv);

    /* plugin options: after the plugins have created their defaults */
    rval = ParseCommandLinePlugin(argc, (const char **) argv);
    profile_note_external_changes();
    if (rval != M64ERR_SUCCESS)
    {
        (*CoreDoCommand)(M64CMD_ROM_CLOSE, 0, NULL);
        cfg_shutdown();
        shutdown_all();
        return 5;
    }

    /* the pad + menu session */
    if (l_GamepadDb != NULL)
    {
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
        if (SDL_GameControllerAddMappingsFromFile(l_GamepadDb) < 0)
            DebugMessage(M64MSG_WARNING, "couldn't read game controller mappings from '%s': %s", l_GamepadDb, SDL_GetError());
    }
    if (session_init() != 0)
        DebugMessage(M64MSG_WARNING, "session init failed; the in-game menu will not work");
    pad_enable_keyboard(l_Keyboard);
    dbg_configure(l_DbgScript, l_DbgShotPrefix, l_DbgOpenAfter);
    {
        char combo[64];
        pad_format_combo(g_cfg.menu_combo, combo, sizeof(combo));
        DebugMessage(M64MSG_INFO, "menu combo: %s", combo);
    }

    /* Controller remap (remap.h): CoreAttachPlugin() runs the input plugin's InitiateControllers(), which reads
     * its Input-SDL-ControlN sections once, so the virtual pad layout has to be in place before the attach. */
    remap_set_save_core_config(l_SaveOptions);
    profile_set_save_core(l_SaveOptions);
    remap_prepare_launch();

    /* attach plugins to core */
    for (i = 0; i < 4; i++)
    {
        if ((*CoreAttachPlugin)(g_PluginMap[i].type, g_PluginMap[i].handle) != M64ERR_SUCCESS)
        {
            DebugMessage(M64MSG_ERROR, "core error while attaching %s plugin.", g_PluginMap[i].name);
            (*CoreDoCommand)(M64CMD_ROM_CLOSE, 0, NULL);
            cfg_shutdown();
            shutdown_all();
            return 13;
        }
    }

    remap_after_plugin_init();         /* the plugin has read its configuration: the real bindings go back */

    if (l_Resume)
        session_resume();

    /* the graphics/audio plugins may have added config defaults; save once before the long-running game (with the
     * global values in place of this game's, see profile.h) */
    if (l_SaveOptions && (*ConfigHasUnsavedChanges)(NULL))
        profile_save_file();
    profile_snapshot();               /* what the plugins start with: the menu compares edits against it */

    /* run the game; this returns when emulation stops */
    (*CoreDoCommand)(M64CMD_EXECUTE, 0, NULL);
    remap_finish();

    /* detach plugins from core and unload them */
    for (i = 0; i < 4; i++)
        (*CoreDetachPlugin)(g_PluginMap[i].type);
    PluginUnload();

    (*CoreDoCommand)(M64CMD_ROM_CLOSE, 0, NULL);

    if (l_SaveOptions)
        profile_save_file();          /* always: it also scrubs anything a plugin may have written during the game */

    {
        int restart = session_restart_requested();
        session_shutdown();
        cfg_shutdown();
        profile_shutdown();
        shutdown_all();
        SDL_Quit();
        if (restart)
            restart_self(argc, argv);
    }
    return 0;
}
