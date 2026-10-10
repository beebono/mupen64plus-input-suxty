/* mupen64plus-video-suxtytest: tiny video plugin to exercise suxty's VidExt override without a real one. GPLv2+, like
 * the rest of the project.
 * Sets GL state once and checks every frame that it is unchanged: "TESTGFX: GL STATE CORRUPTED" if the menu disturbed
 * it.
 * gcc -shared -fPIC -I<core>/src/api -o mupen64plus-video-suxtytest.so testgfx.c -ldl */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_common.h"
#include "m64p_config.h"
#include "m64p_plugin.h"
#include "m64p_types.h"
#include "m64p_vidext.h"

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef char GLchar;
typedef unsigned char GLboolean;
typedef long GLsizeiptr;

#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_TRIANGLES 4
#define GL_FLOAT 0x1406
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_ARRAY_BUFFER_BINDING 0x8894
#define GL_BLEND 0x0BE2
#define GL_VIEWPORT 0x0BA2
#define GL_BLEND_DST 0x0BE0
#define GL_DST_ALPHA 0x0304

static m64p_dynlib_handle l_core;
static ptr_VidExt_Init          p_Init;
static ptr_VidExt_Quit          p_Quit;
static ptr_VidExt_SetVideoMode  p_SetMode;
static ptr_VidExt_GL_SetAttribute p_SetAttr;
static ptr_VidExt_GL_GetProcAddress p_Proc;
static ptr_VidExt_GL_SwapBuffers p_Swap;
static ptr_VidExt_SetCaption    p_Caption;

#define GLFUNC(ret, name, args) static ret (*name) args;
GLFUNC(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))
GLFUNC(void, glClear, (unsigned))
GLFUNC(GLuint, glCreateShader, (GLenum))
GLFUNC(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *))
GLFUNC(void, glCompileShader, (GLuint))
GLFUNC(GLuint, glCreateProgram, (void))
GLFUNC(void, glAttachShader, (GLuint, GLuint))
GLFUNC(void, glBindAttribLocation, (GLuint, GLuint, const GLchar *))
GLFUNC(void, glLinkProgram, (GLuint))
GLFUNC(void, glUseProgram, (GLuint))
GLFUNC(void, glGenBuffers, (GLsizei, GLuint *))
GLFUNC(void, glBindBuffer, (GLenum, GLuint))
GLFUNC(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum))
GLFUNC(void, glEnableVertexAttribArray, (GLuint))
GLFUNC(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *))
GLFUNC(void, glDrawArrays, (GLenum, GLint, GLsizei))
GLFUNC(void, glGetIntegerv, (GLenum, GLint *))
GLFUNC(void, glEnable, (GLenum))
GLFUNC(void, glViewport, (GLint, GLint, GLsizei, GLsizei))
GLFUNC(void, glBlendFunc, (GLenum, GLenum))
GLFUNC(GLint, glGetUniformLocation, (GLuint, const GLchar *))
GLFUNC(void, glUniform1f, (GLint, GLfloat))
GLFUNC(GLboolean, glIsEnabled, (GLenum))
GLFUNC(GLenum, glGetError, (void))

static GLuint l_prog, l_vbo;
static GLint l_loc_t;
static int l_w = 640, l_h = 480;
static unsigned l_frame;
static int l_gl_ready;
static int l_reported;

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *t, int *v, int *a, const char **n, int *c)
{
    if (t) *t = M64PLUGIN_GFX;
    if (v) *v = 0x010000;
    if (a) *a = 0x020500;
    if (n) *n = "suxty test video plugin";
    if (c) *c = 0;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle core, void *ctx, void (*dbg)(void *, int, const char *))
{
    (void) ctx; (void) dbg;
    l_core = core;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void) { return M64ERR_SUCCESS; }

#define LOAD(p, name) p = (void *) dlsym(l_core, name)
#define LOADGL(name) name = (void *) p_Proc(#name)

static GLuint shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    return s;
}

static GFX_INFO l_info;

/* a real video plugin raises the RDP "full sync" interrupt; games wait for it and otherwise never poll the controller */
static void rdp_done(void)
{
    if (l_info.MI_INTR_REG != NULL && l_info.CheckInterrupts != NULL)
    {
        *l_info.MI_INTR_REG |= 0x20;     /* MI_INTR_DP */
        l_info.CheckInterrupts();
    }
}

EXPORT int CALL InitiateGFX(GFX_INFO info)
{
    l_info = info;
    LOAD(p_Init, "VidExt_Init");
    LOAD(p_Quit, "VidExt_Quit");
    LOAD(p_SetMode, "VidExt_SetVideoMode");
    LOAD(p_SetAttr, "VidExt_GL_SetAttribute");
    LOAD(p_Proc, "VidExt_GL_GetProcAddress");
    LOAD(p_Swap, "VidExt_GL_SwapBuffers");
    LOAD(p_Caption, "VidExt_SetCaption");
    return 1;
}

EXPORT int CALL RomOpen(void)
{
    static const float tri[] = { -0.6f, -0.6f, 0.6f, -0.6f, 0.0f, 0.7f };
    const char *env_w = getenv("TESTGFX_W"), *env_h = getenv("TESTGFX_H");
    int fs = getenv("TESTGFX_FULLSCREEN") != NULL;
    const char *prof = getenv("TESTGFX_PROFILE");

    if (env_w) l_w = atoi(env_w);
    if (env_h) l_h = atoi(env_h);
    if (p_Init() != M64ERR_SUCCESS)
        return 0;
    p_SetAttr(M64P_GL_DOUBLEBUFFER, 1);
    p_SetAttr(M64P_GL_SWAP_CONTROL, 1);
    p_SetAttr(M64P_GL_DEPTH_SIZE, 16);
    if (prof && strcmp(prof, "es") == 0)
    {
        p_SetAttr(M64P_GL_CONTEXT_PROFILE_MASK, M64P_GL_CONTEXT_PROFILE_ES);
        p_SetAttr(M64P_GL_CONTEXT_MAJOR_VERSION, 3);
        p_SetAttr(M64P_GL_CONTEXT_MINOR_VERSION, 0);
    }
    p_Caption("suxty test gfx");
    if (p_SetMode(l_w, l_h, 0, fs ? M64VIDEO_FULLSCREEN : M64VIDEO_WINDOWED, 0) != M64ERR_SUCCESS)
        return 0;

    LOADGL(glClearColor); LOADGL(glClear); LOADGL(glCreateShader); LOADGL(glShaderSource); LOADGL(glCompileShader);
    LOADGL(glCreateProgram); LOADGL(glAttachShader); LOADGL(glBindAttribLocation); LOADGL(glLinkProgram);
    LOADGL(glUseProgram); LOADGL(glGenBuffers); LOADGL(glBindBuffer); LOADGL(glBufferData);
    LOADGL(glEnableVertexAttribArray); LOADGL(glVertexAttribPointer); LOADGL(glDrawArrays); LOADGL(glGetIntegerv);
    LOADGL(glEnable); LOADGL(glViewport); LOADGL(glBlendFunc); LOADGL(glGetUniformLocation); LOADGL(glUniform1f);
    LOADGL(glIsEnabled); LOADGL(glGetError);

    l_prog = glCreateProgram();
    glAttachShader(l_prog, shader(GL_VERTEX_SHADER,
        "attribute vec2 p; uniform float t; varying vec2 q;\n"
        "void main(){ float c=cos(t), s=sin(t); q=p; gl_Position=vec4(c*p.x-s*p.y, s*p.x+c*p.y, 0.0, 1.0); }\n"));
    glAttachShader(l_prog, shader(GL_FRAGMENT_SHADER,
        "#ifdef GL_ES\nprecision mediump float;\n#endif\n"
        "varying vec2 q; void main(){ gl_FragColor=vec4(0.5+0.5*q.x, 0.5+0.5*q.y, 0.8, 1.0); }\n"));
    glBindAttribLocation(l_prog, 0, "p");
    glLinkProgram(l_prog);
    glGenBuffers(1, &l_vbo);

    glUseProgram(l_prog);
    glBindBuffer(GL_ARRAY_BUFFER, l_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, 0);
    glEnable(GL_BLEND);
    glBlendFunc(0x0302, GL_DST_ALPHA);          /* SRC_ALPHA, DST_ALPHA: deliberately unusual */
    glViewport(3, 5, l_w - 6, l_h - 10);
    l_loc_t = glGetUniformLocation(l_prog, "t");
    l_gl_ready = 1;
    printf("TESTGFX: ready, %dx%d\n", l_w, l_h);
    return 1;
}

EXPORT void CALL RomClosed(void)
{
    if (p_Quit)
        p_Quit();
    l_gl_ready = 0;
}

EXPORT void CALL UpdateScreen(void)
{
    GLint prog = 0, buf = 0, vp[4] = { 0 }, dst = 0;
    if (!l_gl_ready)
        return;
    l_frame++;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buf);
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetIntegerv(GL_BLEND_DST, &dst);
    if (((unsigned) prog != l_prog || (unsigned) buf != l_vbo || vp[0] != 3 || vp[1] != 5 || !glIsEnabled(GL_BLEND) ||
         (unsigned) dst != GL_DST_ALPHA) && !l_reported)
    {
        printf("TESTGFX: GL STATE CORRUPTED at frame %u (prog %d/%u vbo %d/%u vp %d,%d blend dst %x)\n", l_frame, prog, l_prog, buf, l_vbo, vp[0], vp[1], dst);
        l_reported = 1;
    }
    {
        float t = l_frame * 0.03f;
        glClearColor(0.1f + 0.1f * (float) ((l_frame / 60) % 5), 0.12f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUniform1f(l_loc_t, t);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    if (l_frame % 120 == 0)
        printf("TESTGFX: frame %u\n", l_frame);
    p_Swap();
}

EXPORT void CALL ChangeWindow(void) {}
EXPORT void CALL MoveScreen(int x, int y) { (void) x; (void) y; }
EXPORT void CALL ProcessDList(void) { rdp_done(); }
EXPORT void CALL ProcessRDPList(void) { rdp_done(); }
EXPORT void CALL ShowCFB(void) {}
EXPORT void CALL ViStatusChanged(void) {}
EXPORT void CALL ViWidthChanged(void) {}
EXPORT void CALL ReadScreen2(void *d, int *w, int *h, int f) { (void) d; (void) f; if (w) *w = l_w; if (h) *h = l_h; }
EXPORT void CALL SetRenderingCallback(void (*cb)(int)) { (void) cb; }
EXPORT void CALL ResizeVideoOutput(int w, int h) { (void) w; (void) h; }
EXPORT void CALL FBRead(unsigned int a) { (void) a; }
EXPORT void CALL FBWrite(unsigned int a, unsigned int s) { (void) a; (void) s; }
EXPORT void CALL FBGetFrameBufferInfo(void *p) { (void) p; }
