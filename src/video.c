/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-ui-suxty - video.c
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


#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "core_interface.h"
#include "gl.h"
#include "m64p_frontend.h"
#include "m64p_types.h"
#include "main.h"
#include "session.h"
#include "video.h"

static SDL_Window    *l_win;
static SDL_GLContext  l_game_ctx;
static SDL_GLContext  l_ui_ctx;
static int            l_ui_ok;
static int            l_swap_control = 0;     /* like the core's own VidExt: no vsync unless the plugin asks */
static int            l_profile_set;          /* has the plugin chosen a context profile itself? */
static int            l_fullscreen;
static char          *l_title;

static int get_current_display(void)
{
    const char *v = SDL_getenv("SDL_VIDEO_FULLSCREEN_DISPLAY");
    if (!v)
        v = SDL_getenv("SDL_VIDEO_FULLSCREEN_HEAD");
    return v ? SDL_atoi(v) : 0;
}

/* ---- VidExt function table ---------------------------------------------------------------- */

static m64p_error vx_init_with_mode(m64p_render_mode mode)
{
    if (mode != M64P_RENDER_OPENGL)
    {
        DebugMessage(M64MSG_ERROR, "suxty: only OpenGL / GLES video plugins are supported (no Vulkan render mode)");
        return M64ERR_UNSUPPORTED;
    }
    /* same as the core's own VidExt: keep joystick input flowing while no window has focus */
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
    {
        DebugMessage(M64MSG_ERROR, "SDL video subsystem init failed: %s", SDL_GetError());
        return M64ERR_SYSTEM_FAIL;
    }
    DebugMessage(M64MSG_INFO, "SDL video driver: %s", SDL_GetCurrentVideoDriver());
    return M64ERR_SUCCESS;
}

static m64p_error vx_init(void)
{
    return vx_init_with_mode(M64P_RENDER_OPENGL);
}

static void destroy_everything(void)
{
    if (l_ui_ctx != NULL)
    {
        if (l_win != NULL && l_ui_ok)
        {
            SDL_GL_MakeCurrent(l_win, l_ui_ctx);
            ugl_shutdown_resources();
        }
        SDL_GL_DeleteContext(l_ui_ctx);
        l_ui_ctx = NULL;
    }
    l_ui_ok = 0;
    if (l_game_ctx != NULL)
    {
        SDL_GL_DeleteContext(l_game_ctx);
        l_game_ctx = NULL;
    }
    if (l_win != NULL)
    {
        SDL_DestroyWindow(l_win);
        l_win = NULL;
    }
}

static m64p_error vx_quit(void)
{
    if (!SDL_WasInit(SDL_INIT_VIDEO))
        return M64ERR_NOT_INIT;
    SDL_ShowCursor(SDL_ENABLE);
    destroy_everything();
    free(l_title);
    l_title = NULL;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return M64ERR_SUCCESS;
}

static m64p_error vx_list_modes(m64p_2d_size *sizes, int *count)
{
    SDL_DisplayMode mode;
    int n, i;

    if (!SDL_WasInit(SDL_INIT_VIDEO))
        return M64ERR_NOT_INIT;
    n = SDL_GetNumDisplayModes(get_current_display());
    if (n <= 0)
    {
        *count = 0;
        return M64ERR_SUCCESS;
    }
    for (i = 0; i < n && i < *count; i++)
    {
        if (SDL_GetDisplayMode(get_current_display(), i, &mode) != 0)
        {
            *count = 0;
            return M64ERR_SYSTEM_FAIL;
        }
        sizes[i].uiWidth = mode.w;
        sizes[i].uiHeight = mode.h;
    }
    *count = i;
    return M64ERR_SUCCESS;
}

static m64p_error vx_list_rates(m64p_2d_size size, int *count, int *rates)
{
    (void) size; (void) count; (void) rates;
    return M64ERR_UNSUPPORTED;
}

/* Build the private overlay context.  Runs with the game context current; leaves it current. */
static void setup_overlay_context(void)
{
    l_ui_ok = 0;
    if (ugl_load_functions() != 0)
    {
        DebugMessage(M64MSG_ERROR, "suxty: GL entry points unavailable; the menu is disabled");
        return;
    }
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
    l_ui_ctx = SDL_GL_CreateContext(l_win);        /* becomes current */
    if (l_ui_ctx == NULL)
    {
        DebugMessage(M64MSG_ERROR, "suxty: couldn't create the overlay GL context (%s); the menu is disabled", SDL_GetError());
    }
    else if (ugl_init_resources() != 0)
    {
        DebugMessage(M64MSG_ERROR, "suxty: overlay GL resources failed to initialise; the menu is disabled");
    }
    else
    {
        l_ui_ok = 1;
        DebugMessage(M64MSG_VERBOSE, "overlay GL context ready");
    }
    if (SDL_GL_MakeCurrent(l_win, l_game_ctx) != 0)
        DebugMessage(M64MSG_ERROR, "SDL_GL_MakeCurrent(game) failed: %s", SDL_GetError());
}


static void set_platform_default_context(const char *force)
{
#if defined(__arm__) || defined(__aarch64__)
    int use_es = 1;                    /* handhelds: GLES only */
#else
    int use_es = 0;                    /* desktop dev box: compatibility profile, like the core's own VidExt */
#endif
    if (force != NULL && strcmp(force, "es") == 0)
        use_es = 1;
    else if (force != NULL && strcmp(force, "compat") == 0)
        use_es = 0;

    if (use_es)
    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    }
    else
    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    }
}

static int create_window_and_context(int width, int height, Uint32 wflags, int quiet)
{
    l_win = SDL_CreateWindow(l_title ? l_title : "mupen64plus-ui-suxty", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                             width, height, wflags);
    if (l_win == NULL)
    {
        DebugMessage(M64MSG_ERROR, "SDL_CreateWindow failed: %s", SDL_GetError());
        return -1;
    }
    l_game_ctx = SDL_GL_CreateContext(l_win);
    if (l_game_ctx == NULL || SDL_GL_MakeCurrent(l_win, l_game_ctx) != 0)
    {
        DebugMessage(quiet ? M64MSG_VERBOSE : M64MSG_ERROR, "couldn't create/make current the GL context: %s", SDL_GetError());
        return -1;
    }
    return 0;
}

static m64p_error vx_set_mode(int width, int height, int bpp, int screen_mode, int flags)
{
    Uint32 wflags = SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL;
    int ww = 0, wh = 0;

    if (!SDL_WasInit(SDL_INIT_VIDEO))
        return M64ERR_NOT_INIT;

    if (screen_mode == M64VIDEO_WINDOWED)
    {
        if (flags & M64VIDEOFLAG_SUPPORT_RESIZING)
            wflags |= SDL_WINDOW_RESIZABLE;
    }
    else if (screen_mode == M64VIDEO_FULLSCREEN)
    {
        wflags |= SDL_WINDOW_FULLSCREEN;
    }
    else
    {
        return M64ERR_INPUT_INVALID;
    }

    if (bpp > 0)
        DebugMessage(M64MSG_INFO, "Setting %i-bit video mode: %ix%i", bpp, width, height);
    else
        DebugMessage(M64MSG_INFO, "Setting video mode: %ix%i", width, height);

    if (l_win == NULL)
    {
        /* Context selection.  The stock core on a GLES build (USE_GLES) overrides whatever the plugin asked for
         * with ES 2.0, which is what makes e.g. GLideN64 (which asks for a *core 3.3* context) work on Mali.  We
         * try the plugin's own request first (so a plugin that really wants ES 3.x gets it) and fall back to the
         * platform default if that fails.  SUXTY_GL=es|compat|plugin forces a choice. */
        const char *force = SDL_getenv("SUXTY_GL");
        int attempt;
        int try_plugin_first = l_profile_set && (force == NULL || strcmp(force, "plugin") == 0);

        for (attempt = 0; attempt < 2; attempt++)
        {
            if (attempt == 0 && try_plugin_first)
            {
                DebugMessage(M64MSG_VERBOSE, "creating window/GL context with the video plugin's requested attributes");
            }
            else if (attempt == 1 && !try_plugin_first)
            {
                break;      /* the platform default already failed */
            }
            else
            {
                set_platform_default_context(force);
            }
            if (create_window_and_context(width, height, wflags, attempt == 0 && try_plugin_first) == 0)
                break;
            if (l_win != NULL || l_game_ctx != NULL)
                destroy_everything();
            if (!try_plugin_first)
                break;
            DebugMessage(M64MSG_WARNING, "plugin-requested GL context unavailable; retrying with the platform default");
        }
        if (l_win == NULL)
            return M64ERR_SYSTEM_FAIL;
        SDL_GL_GetDrawableSize(l_win, &ww, &wh);
        DebugMessage(M64MSG_INFO, "window created: %ix%i (drawable %ix%i)", width, height, ww, wh);
    }
    else
    {
        /* the plugin changing mode/size after creation */
        if (((SDL_GetWindowFlags(l_win) & SDL_WINDOW_FULLSCREEN) != 0) != (screen_mode == M64VIDEO_FULLSCREEN))
            SDL_SetWindowFullscreen(l_win, screen_mode == M64VIDEO_FULLSCREEN ? SDL_WINDOW_FULLSCREEN : 0);
    }

    SDL_GetWindowSize(l_win, &ww, &wh);
    if (ww != width || wh != height)
        SDL_SetWindowSize(l_win, width, height);
    SDL_ShowCursor(SDL_DISABLE);

    if (SDL_GL_SetSwapInterval(l_swap_control) != 0)
        DebugMessage(M64MSG_WARNING, "SDL swap interval (VSync) set failed: %s", SDL_GetError());

    if (l_ui_ctx == NULL)
        setup_overlay_context();

    l_fullscreen = (screen_mode == M64VIDEO_FULLSCREEN);
    return M64ERR_SUCCESS;
}

static m64p_error vx_set_mode_with_rate(int width, int height, int rate, int bpp, int screen_mode, int flags)
{
    (void) rate;
    return vx_set_mode(width, height, bpp, screen_mode, flags);
}

static m64p_error vx_resize(int width, int height)
{
    int ww = 0, wh = 0;
    if (l_win == NULL || !SDL_WasInit(SDL_INIT_VIDEO))
        return M64ERR_NOT_INIT;
    if (l_fullscreen)
        return M64ERR_INVALID_STATE;
    SDL_GetWindowSize(l_win, &ww, &wh);
    if (ww != width || wh != height)
        SDL_SetWindowSize(l_win, width, height);
    return M64ERR_SUCCESS;
}

static m64p_error vx_set_caption(const char *title)
{
    if (!SDL_WasInit(SDL_INIT_VIDEO))
        return M64ERR_NOT_INIT;
    if (l_win != NULL)
    {
        SDL_SetWindowTitle(l_win, title);
    }
    else
    {
        free(l_title);
        l_title = strdup(title);
    }
    return M64ERR_SUCCESS;
}

static m64p_error vx_toggle_fs(void)
{
    if (l_win == NULL || !SDL_WasInit(SDL_INIT_VIDEO))
        return M64ERR_NOT_INIT;
    if (SDL_SetWindowFullscreen(l_win, (SDL_GetWindowFlags(l_win) & SDL_WINDOW_FULLSCREEN) ? 0 : SDL_WINDOW_FULLSCREEN) != 0)
    {
        DebugMessage(M64MSG_ERROR, "SDL_SetWindowFullscreen failed: %s", SDL_GetError());
        return M64ERR_SYSTEM_FAIL;
    }
    l_fullscreen = !l_fullscreen;
    return M64ERR_SUCCESS;
}

static m64p_function vx_gl_get_proc(const char *name)
{
    if (!SDL_WasInit(SDL_INIT_VIDEO))
        return NULL;
    return (m64p_function) SDL_GL_GetProcAddress(name);
}

typedef struct { m64p_GLattr m64; SDL_GLattr sdl; } attr_map_t;
static const attr_map_t l_attr_map[] = {
    { M64P_GL_DOUBLEBUFFER, SDL_GL_DOUBLEBUFFER },
    { M64P_GL_BUFFER_SIZE, SDL_GL_BUFFER_SIZE },
    { M64P_GL_DEPTH_SIZE, SDL_GL_DEPTH_SIZE },
    { M64P_GL_RED_SIZE, SDL_GL_RED_SIZE },
    { M64P_GL_GREEN_SIZE, SDL_GL_GREEN_SIZE },
    { M64P_GL_BLUE_SIZE, SDL_GL_BLUE_SIZE },
    { M64P_GL_ALPHA_SIZE, SDL_GL_ALPHA_SIZE },
    { M64P_GL_MULTISAMPLEBUFFERS, SDL_GL_MULTISAMPLEBUFFERS },
    { M64P_GL_MULTISAMPLESAMPLES, SDL_GL_MULTISAMPLESAMPLES },
    { M64P_GL_CONTEXT_MAJOR_VERSION, SDL_GL_CONTEXT_MAJOR_VERSION },
    { M64P_GL_CONTEXT_MINOR_VERSION, SDL_GL_CONTEXT_MINOR_VERSION },
    { M64P_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_MASK },
};

static m64p_error vx_gl_set_attr(m64p_GLattr attr, int value)
{
    size_t i;
    if (!SDL_WasInit(SDL_INIT_VIDEO))
        return M64ERR_NOT_INIT;
    DebugMessage(M64MSG_VERBOSE, "VidExt_GL_SetAttribute(%d, %d)", (int) attr, value);
    if (attr == M64P_GL_SWAP_CONTROL)
    {
        l_swap_control = value;        /* applied after the context exists */
        return M64ERR_SUCCESS;
    }
    if (attr == M64P_GL_CONTEXT_PROFILE_MASK)
    {
        switch (value)
        {
            case M64P_GL_CONTEXT_PROFILE_CORE:          value = SDL_GL_CONTEXT_PROFILE_CORE; break;
            case M64P_GL_CONTEXT_PROFILE_COMPATIBILITY: value = SDL_GL_CONTEXT_PROFILE_COMPATIBILITY; break;
            case M64P_GL_CONTEXT_PROFILE_ES:            value = SDL_GL_CONTEXT_PROFILE_ES; break;
            default:                                    value = 0;
        }
        l_profile_set = 1;
    }
    for (i = 0; i < sizeof(l_attr_map) / sizeof(l_attr_map[0]); i++)
        if (l_attr_map[i].m64 == attr)
            return SDL_GL_SetAttribute(l_attr_map[i].sdl, value) == 0 ? M64ERR_SUCCESS : M64ERR_SYSTEM_FAIL;
    return M64ERR_INPUT_INVALID;
}

static m64p_error vx_gl_get_attr(m64p_GLattr attr, int *value)
{
    size_t i;
    if (!SDL_WasInit(SDL_INIT_VIDEO))
        return M64ERR_NOT_INIT;
    if (attr == M64P_GL_SWAP_CONTROL)
    {
        *value = SDL_GL_GetSwapInterval();
        return M64ERR_SUCCESS;
    }
    for (i = 0; i < sizeof(l_attr_map) / sizeof(l_attr_map[0]); i++)
    {
        if (l_attr_map[i].m64 == attr)
        {
            int v = 0;
            if (SDL_GL_GetAttribute(l_attr_map[i].sdl, &v) != 0)
                return M64ERR_SYSTEM_FAIL;
            if (attr == M64P_GL_CONTEXT_PROFILE_MASK)
            {
                switch (v)
                {
                    case SDL_GL_CONTEXT_PROFILE_CORE:          v = M64P_GL_CONTEXT_PROFILE_CORE; break;
                    case SDL_GL_CONTEXT_PROFILE_COMPATIBILITY: v = M64P_GL_CONTEXT_PROFILE_COMPATIBILITY; break;
                    case SDL_GL_CONTEXT_PROFILE_ES:            v = M64P_GL_CONTEXT_PROFILE_ES; break;
                    default:                                   v = 0;
                }
            }
            *value = v;
            return M64ERR_SUCCESS;
        }
    }
    return M64ERR_INPUT_INVALID;
}

static m64p_error vx_swap(void)
{
    if (l_win == NULL)
        return M64ERR_NOT_INIT;
    /* a menu is open on the emulation thread; a swap from any other thread (threaded video plugin,
     * core pause loop) must not touch the window or the contexts */
    if (session_menu_blocks_this_thread())
        return M64ERR_SUCCESS;
    if (l_ui_ok && session_before_swap())
        return M64ERR_SUCCESS;           /* the session presented the frame itself */
    SDL_GL_SwapWindow(l_win);
    return M64ERR_SUCCESS;
}

static uint32_t vx_default_fbo(void)
{
    return 0;
}

static m64p_error vx_vk_surface(void **surface, void *instance)
{
    (void) surface; (void) instance;
    return M64ERR_UNSUPPORTED;
}

static m64p_error vx_vk_exts(const char **exts[], uint32_t *count)
{
    (void) exts; (void) count;
    return M64ERR_UNSUPPORTED;
}

m64p_error video_install(void)
{
    static m64p_video_extension_functions table;

    table.Functions = 17;
    table.VidExtFuncInit = vx_init;
    table.VidExtFuncQuit = vx_quit;
    table.VidExtFuncListModes = vx_list_modes;
    table.VidExtFuncListRates = vx_list_rates;
    table.VidExtFuncSetMode = vx_set_mode;
    table.VidExtFuncSetModeWithRate = vx_set_mode_with_rate;
    table.VidExtFuncGLGetProc = vx_gl_get_proc;
    table.VidExtFuncGLSetAttr = vx_gl_set_attr;
    table.VidExtFuncGLGetAttr = vx_gl_get_attr;
    table.VidExtFuncGLSwapBuf = vx_swap;
    table.VidExtFuncSetCaption = vx_set_caption;
    table.VidExtFuncToggleFS = vx_toggle_fs;
    table.VidExtFuncResizeWindow = vx_resize;
    table.VidExtFuncGLGetDefaultFramebuffer = vx_default_fbo;
    table.VidExtFuncInitWithRenderMode = vx_init_with_mode;
    table.VidExtFuncVKGetSurface = vx_vk_surface;
    table.VidExtFuncVKGetInstanceExtensions = vx_vk_exts;
    return CoreOverrideVidExt(&table);
}

#ifdef SUXTY_PLUGIN
/* PluginShutdown: stop the core calling into this library (our table's pointers die with the dlclose), and tear the
 * window down if the video plugin never called VidExt_Quit() */
void video_uninstall(void)
{
    static m64p_video_extension_functions none;     /* any NULL entry deactivates the override */
    none.Functions = 17;
    if (CoreOverrideVidExt != NULL)
        (*CoreOverrideVidExt)(&none);
    if (l_win != NULL || l_game_ctx != NULL)
    {
        DebugMessage(M64MSG_WARNING, "suxty: the video plugin left the window open; closing it");
        vx_quit();
    }
}
#endif

/* ---- helpers for session.c ------------------------------------------------------------------ */

int video_ui_available(void)
{
    return l_win != NULL && l_ui_ok;
}

void video_drawable_size(int *w, int *h)
{
    int dw = 0, dh = 0;
    if (l_win != NULL)
        SDL_GL_GetDrawableSize(l_win, &dw, &dh);
    *w = dw;
    *h = dh;
}

int video_enter_ui(void)
{
    if (!video_ui_available())
        return -1;
    if (SDL_GL_MakeCurrent(l_win, l_ui_ctx) != 0)
    {
        DebugMessage(M64MSG_ERROR, "SDL_GL_MakeCurrent(overlay) failed: %s", SDL_GetError());
        return -1;
    }
    return 0;
}

void video_leave_ui(void)
{
    if (l_win != NULL && SDL_GL_MakeCurrent(l_win, l_game_ctx) != 0)
        DebugMessage(M64MSG_ERROR, "SDL_GL_MakeCurrent(game) failed: %s", SDL_GetError());
}

void video_present(void)
{
    if (l_win != NULL)
        SDL_GL_SwapWindow(l_win);
}

/* Fast-forward: a vsynced swap would cap the game at the display refresh rate no matter what speed the core
 * is told to run at.  Must be called on the thread that has the game context current. */
void video_set_unthrottled(int unthrottled)
{
    int want = unthrottled ? 0 : l_swap_control;
    if (l_win == NULL)
        return;
    if (SDL_GL_SetSwapInterval(want) != 0)
        DebugMessage(M64MSG_VERBOSE, "couldn't change the swap interval: %s", SDL_GetError());
}
