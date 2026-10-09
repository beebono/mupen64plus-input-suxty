/* mupen64plus-input-suxtytest: a tiny input plugin that does what the stock mupen64plus-input-sdl does
 * (SDL_JoystickUpdate + SDL_JoystickGetButton/GetAxis/GetHat) and logs every change of what it sees.
 * It is how suxty's input filter (src/inputfilter.c) is tested end to end: with the filter working, the
 * log must show no button, hat or axis activity while the hotkey is held.  GPLv2+.
 *
 *   gcc -shared -fPIC -I<core>/src/api -o mupen64plus-input-suxtytest.so testinput.c $(sdl2-config --cflags --libs) -ldl
 *
 * TESTINPUT_NAME selects the joystick by name substring (default: "suxty vpad"; else joystick 0).
 *
 * WRAP MODE: with TESTINPUT_WRAP=/path/to/mupen64plus-input-sdl.so the plugin loads the *stock* input plugin
 * and forwards every call to it, and logs the N64 controller state the stock plugin produces
 * ("TESTINPUT: N64[0] buttons=0x0080 (A) stick=(0,0)").  That is how the controller remap (M3) is tested
 * end to end: the stock plugin, configured through the Input-SDL-ControlN sections suxty writes, reads suxty's
 * SDL joystick layer, and this log shows what the game would get.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_common.h"
#include "m64p_config.h"
#include "m64p_plugin.h"
#include "m64p_types.h"

/* ---- wrap mode ---- */
static void *l_wrap;
static m64p_error (*w_startup)(m64p_dynlib_handle, void *, void (*)(void *, int, const char *));
static m64p_error (*w_shutdown)(void);
static void (*w_init)(CONTROL_INFO);
static void (*w_getkeys)(int, BUTTONS *);
static void (*w_command)(int, unsigned char *);
static void (*w_readctl)(int, unsigned char *);
static void (*w_keydown)(int, int);
static void (*w_keyup)(int, int);
static int  (*w_romopen)(void);
static void (*w_romclosed)(void);
static unsigned l_n64_last[4] = { 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu };
static unsigned l_n64_polls;

static void wrap_load(void)
{
    const char *path = getenv("TESTINPUT_WRAP");
    if (path == NULL || l_wrap != NULL)
        return;
    l_wrap = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (l_wrap == NULL)
    {
        printf("TESTINPUT: can't load the wrapped plugin '%s': %s\n", path, dlerror());
        fflush(stdout);
        return;
    }
    *(void **) &w_startup = dlsym(l_wrap, "PluginStartup");
    *(void **) &w_shutdown = dlsym(l_wrap, "PluginShutdown");
    *(void **) &w_init = dlsym(l_wrap, "InitiateControllers");
    *(void **) &w_getkeys = dlsym(l_wrap, "GetKeys");
    *(void **) &w_command = dlsym(l_wrap, "ControllerCommand");
    *(void **) &w_readctl = dlsym(l_wrap, "ReadController");
    *(void **) &w_keydown = dlsym(l_wrap, "SDL_KeyDown");
    *(void **) &w_keyup = dlsym(l_wrap, "SDL_KeyUp");
    *(void **) &w_romopen = dlsym(l_wrap, "RomOpen");
    *(void **) &w_romclosed = dlsym(l_wrap, "RomClosed");
    printf("TESTINPUT: wrapping the stock input plugin '%s'\n", path);
    fflush(stdout);
}

static void log_n64(int Control, const BUTTONS *k)
{
    static const char *const names[14] = { "D-Right", "D-Left", "D-Down", "D-Up", "Start", "Z", "B", "A",
                                           "C-Right", "C-Left", "C-Down", "C-Up", "R", "L" };
    char list[160];
    size_t n = 0;
    int i;
    unsigned key = (k->Value & 0xffffu) | ((unsigned) (k->X_AXIS & 0xff) << 16) | ((unsigned) (k->Y_AXIS & 0xff) << 24);
    if (Control < 0 || Control > 3 || key == l_n64_last[Control])
        return;
    l_n64_last[Control] = key;
    list[0] = 0;
    for (i = 0; i < 14; i++)
        if (k->Value & (1u << i))
            n += (size_t) snprintf(list + n, sizeof(list) - n, "%s%s", n ? " " : "", names[i]);
    printf("TESTINPUT: N64[%d] buttons=0x%04x (%s) stick=(%d,%d) poll=%u\n", Control, k->Value & 0xffffu, list,
           (int) k->X_AXIS, (int) k->Y_AXIS, l_n64_polls);
    fflush(stdout);
}

static SDL_Joystick *l_js;
static unsigned l_poll;
static char l_last[256];

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *t, int *v, int *a, const char **n, int *c)
{
    if (t) *t = M64PLUGIN_INPUT;
    if (v) *v = 0x010000;
    if (a) *a = 0x020100;
    if (n) *n = "suxty test input plugin";
    if (c) *c = 0;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle core, void *ctx, void (*dbg)(void *, int, const char *))
{
    wrap_load();
    if (l_wrap != NULL && w_startup != NULL)
        return w_startup(core, ctx, dbg);
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void)
{
    if (l_wrap != NULL && w_shutdown != NULL)
        return w_shutdown();
    return M64ERR_SUCCESS;
}

static void open_js(void)
{
    const char *want = getenv("TESTINPUT_NAME");
    int i, n;
    if (want == NULL)
        want = "suxty vpad";
    SDL_InitSubSystem(SDL_INIT_JOYSTICK);
    n = SDL_NumJoysticks();
    for (i = 0; i < n; i++)
        if (strstr(SDL_JoystickNameForIndex(i), want) != NULL)
        {
            l_js = SDL_JoystickOpen(i);
            break;
        }
    if (l_js == NULL && n > 0)
        l_js = SDL_JoystickOpen(0);
    printf("TESTINPUT: using joystick '%s' (%d joysticks)\n", l_js ? SDL_JoystickName(l_js) : "(none)", n);
    fflush(stdout);
}

EXPORT void CALL InitiateControllers(CONTROL_INFO ControlInfo)
{
    if (l_wrap != NULL && w_init != NULL)
    {
        int i;
        w_init(ControlInfo);
        for (i = 0; i < 4; i++)
            printf("TESTINPUT: controller %d present=%d plugin=%d\n", i, ControlInfo.Controls[i].Present, ControlInfo.Controls[i].Plugin);
        fflush(stdout);
        return;
    }
    ControlInfo.Controls[0].Present = 1;
    ControlInfo.Controls[0].Plugin = PLUGIN_NONE;
    open_js();
}

EXPORT void CALL GetKeys(int Control, BUTTONS *Keys)
{
    char cur[256];
    size_t n = 0;
    int i;

    if (l_wrap != NULL && w_getkeys != NULL)
    {
        if (Control == 0)
            l_n64_polls++;
        w_getkeys(Control, Keys);
        log_n64(Control, Keys);
        return;
    }
    if (Control != 0 || l_js == NULL)
        return;
    l_poll++;
    SDL_JoystickUpdate();
    cur[0] = 0;
    n += (size_t) snprintf(cur + n, sizeof(cur) - n, "buttons[");
    for (i = 0; i < SDL_JoystickNumButtons(l_js) && n < sizeof(cur) - 16; i++)
        if (SDL_JoystickGetButton(l_js, i))
            n += (size_t) snprintf(cur + n, sizeof(cur) - n, "%d ", i);
    n += (size_t) snprintf(cur + n, sizeof(cur) - n, "] hat[");
    for (i = 0; i < SDL_JoystickNumHats(l_js) && n < sizeof(cur) - 16; i++)
        n += (size_t) snprintf(cur + n, sizeof(cur) - n, "%d ", SDL_JoystickGetHat(l_js, i));
    n += (size_t) snprintf(cur + n, sizeof(cur) - n, "] axes[");
    for (i = 0; i < SDL_JoystickNumAxes(l_js) && n < sizeof(cur) - 16; i++)
    {
        int v = SDL_JoystickGetAxis(l_js, i);
        if (v > 8000 || v < -8000)
            n += (size_t) snprintf(cur + n, sizeof(cur) - n, "%d:%d ", i, v);
    }
    snprintf(cur + n, sizeof(cur) - n, "]");
    if (strcmp(cur, l_last) != 0)
    {
        printf("TESTINPUT: poll %u sees %s\n", l_poll, cur);
        fflush(stdout);
        strcpy(l_last, cur);
    }
    /* A button -> N64 A, so a real game could be steered with it */
    Keys->Value = 0;
    if (SDL_JoystickGetButton(l_js, 0))
        Keys->A_BUTTON = 1;
}

EXPORT void CALL ControllerCommand(int Control, unsigned char *Command)
{
    if (l_wrap != NULL && w_command != NULL)
        w_command(Control, Command);
}
EXPORT void CALL ReadController(int Control, unsigned char *Command)
{
    if (l_wrap != NULL && w_readctl != NULL)
        w_readctl(Control, Command);
}
EXPORT void CALL SDL_KeyDown(int keymod, int keysym)
{
    if (l_wrap != NULL && w_keydown != NULL)
        w_keydown(keymod, keysym);
}
EXPORT void CALL SDL_KeyUp(int keymod, int keysym)
{
    if (l_wrap != NULL && w_keyup != NULL)
        w_keyup(keymod, keysym);
}
EXPORT int  CALL RomOpen(void)
{
    if (l_wrap != NULL && w_romopen != NULL)
        return w_romopen();
    return 1;
}
EXPORT void CALL RomClosed(void)
{
    if (l_wrap != NULL && w_romclosed != NULL)
    {
        w_romclosed();
        printf("TESTINPUT: GetKeys was called %u times\n", l_n64_polls);
        fflush(stdout);
        return;
    }
    printf("TESTINPUT: GetKeys was called %u times\n", l_poll);
    if (l_js)
        SDL_JoystickClose(l_js);
    l_js = NULL;
}
