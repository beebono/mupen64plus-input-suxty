/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - gl.c
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


#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "font.h"
#include "gl.h"
#include "m64p_types.h"
#include "main.h"

/* ---- minimal GL type / enum definitions (so no GLES headers are needed) ---- */
typedef unsigned int  GLenum;
typedef unsigned int  GLuint;
typedef int           GLint;
typedef int           GLsizei;
typedef unsigned char GLboolean;
typedef unsigned int  GLbitfield;
typedef float         GLfloat;
typedef char          GLchar;
typedef ptrdiff_t     GLsizeiptr;

#define GL_NO_ERROR               0
#define GL_COLOR_BUFFER_BIT       0x00004000
#define GL_TRIANGLES              0x0004
#define GL_FLOAT                  0x1406
#define GL_UNSIGNED_BYTE          0x1401
#define GL_RGBA                   0x1908
#define GL_TEXTURE_2D             0x0DE1
#define GL_TEXTURE_MAG_FILTER     0x2800
#define GL_TEXTURE_MIN_FILTER     0x2801
#define GL_TEXTURE_WRAP_S         0x2802
#define GL_TEXTURE_WRAP_T         0x2803
#define GL_NEAREST                0x2600
#define GL_LINEAR                 0x2601
#define GL_CLAMP_TO_EDGE          0x812F
#define GL_BLEND                  0x0BE2
#define GL_DEPTH_TEST             0x0B71
#define GL_CULL_FACE              0x0B44
#define GL_SCISSOR_TEST           0x0C11
#define GL_STENCIL_TEST           0x0B90
#define GL_DITHER                 0x0BD0
#define GL_SRC_ALPHA              0x0302
#define GL_ONE_MINUS_SRC_ALPHA    0x0303
#define GL_FRAMEBUFFER            0x8D40
#define GL_FRAMEBUFFER_BINDING    0x8CA6
#define GL_ARRAY_BUFFER           0x8892
#define GL_PIXEL_PACK_BUFFER      0x88EB
#define GL_PIXEL_PACK_BUFFER_BINDING 0x88ED
#define GL_VERTEX_SHADER          0x8B31
#define GL_FRAGMENT_SHADER        0x8B30
#define GL_COMPILE_STATUS         0x8B81
#define GL_LINK_STATUS            0x8B82
#define GL_INFO_LOG_LENGTH        0x8B84
#define GL_TEXTURE0               0x84C0
#define GL_UNPACK_ALIGNMENT       0x0CF5
#define GL_VERSION                0x1F02

typedef unsigned char GLubyte_;

#define GLAPI_FUNCS(X) \
    X(GLenum,   glGetError,        (void)) \
    X(void,     glGetIntegerv,     (GLenum, GLint *)) \
    X(const GLubyte_ *, glGetString, (GLenum)) \
    X(void,     glReadPixels,      (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) \
    X(void,     glBindFramebuffer, (GLenum, GLuint)) \
    X(void,     glBindBuffer,      (GLenum, GLuint)) \
    X(void,     glViewport,        (GLint, GLint, GLsizei, GLsizei)) \
    X(void,     glEnable,          (GLenum)) \
    X(void,     glDisable,         (GLenum)) \
    X(void,     glClearColor,      (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void,     glClear,           (GLbitfield)) \
    X(void,     glBlendFunc,       (GLenum, GLenum)) \
    X(GLuint,   glCreateShader,    (GLenum)) \
    X(void,     glShaderSource,    (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
    X(void,     glCompileShader,   (GLuint)) \
    X(void,     glGetShaderiv,     (GLuint, GLenum, GLint *)) \
    X(void,     glGetShaderInfoLog,(GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void,     glDeleteShader,    (GLuint)) \
    X(GLuint,   glCreateProgram,   (void)) \
    X(void,     glAttachShader,    (GLuint, GLuint)) \
    X(void,     glBindAttribLocation, (GLuint, GLuint, const GLchar *)) \
    X(void,     glLinkProgram,     (GLuint)) \
    X(void,     glGetProgramiv,    (GLuint, GLenum, GLint *)) \
    X(void,     glGetProgramInfoLog,(GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void,     glUseProgram,      (GLuint)) \
    X(void,     glDeleteProgram,   (GLuint)) \
    X(GLint,    glGetUniformLocation, (GLuint, const GLchar *)) \
    X(void,     glUniform1i,       (GLint, GLint)) \
    X(void,     glUniform2f,       (GLint, GLfloat, GLfloat)) \
    X(void,     glGenTextures,     (GLsizei, GLuint *)) \
    X(void,     glDeleteTextures,  (GLsizei, const GLuint *)) \
    X(void,     glBindTexture,     (GLenum, GLuint)) \
    X(void,     glActiveTexture,   (GLenum)) \
    X(void,     glTexImage2D,      (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(void,     glTexParameteri,   (GLenum, GLenum, GLint)) \
    X(void,     glPixelStorei,     (GLenum, GLint)) \
    X(void,     glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    X(void,     glEnableVertexAttribArray, (GLuint)) \
    X(void,     glDrawArrays,      (GLenum, GLint, GLsizei))


#define X(ret, name, args) static ret (*p_##name) args;
GLAPI_FUNCS(X)
#undef X

/* ---- renderer state ---- */
typedef struct {
    float   x, y, u, v;
    uint8_t r, g, b, a;
} vertex_t;

#define MAX_QUADS 4096
#define MAX_VERTS (MAX_QUADS * 6)

static vertex_t l_verts[MAX_VERTS];
static int      l_nverts;
static GLuint   l_prog, l_atlas_tex, l_bg_tex;
static GLint    l_loc_inv, l_loc_tex;
static GLuint   l_cur_tex;
static int      l_bg_w, l_bg_h;
static int      l_screen_w, l_screen_h;
static int      l_have_pbo_enum;          /* GL >= ES 3.0 or desktop GL: PIXEL_PACK_BUFFER exists */

static uint8_t *l_cap_buf;
static int      l_cap_w, l_cap_h;
static uint8_t *l_rb_buf;
static size_t   l_rb_size;

/* ---- loading ---- */
int ugl_load_functions(void)
{
    int missing = 0;
#define X(ret, name, args) \
    p_##name = (ret (*) args) SDL_GL_GetProcAddress(#name); \
    if (p_##name == NULL) { DebugMessage(M64MSG_ERROR, "GL entry point %s not found", #name); missing++; }
    GLAPI_FUNCS(X)
#undef X
    if (missing)
        return -1;

    const char *ver = (const char *) p_glGetString(GL_VERSION);
    l_have_pbo_enum = 1;
    if (ver != NULL && strncmp(ver, "OpenGL ES ", 10) == 0)
    {
        /* "OpenGL ES 2.0 ..." has no PBOs; ES 3.x does */
        l_have_pbo_enum = (ver[10] >= '3');
    }
    DebugMessage(M64MSG_VERBOSE, "GL version string: %s", ver ? ver : "(null)");
    return 0;
}

static GLuint compile(GLenum type, const char *src)
{
    GLuint sh = p_glCreateShader(type);
    GLint ok = 0;
    p_glShaderSource(sh, 1, &src, NULL);
    p_glCompileShader(sh);
    p_glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512] = "";
        p_glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        DebugMessage(M64MSG_ERROR, "overlay shader compile failed: %s", log);
        p_glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static const char *vs_src =
    "uniform vec2 u_inv;\n"
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "attribute vec4 a_col;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_col;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos.x * u_inv.x - 1.0, 1.0 - a_pos.y * u_inv.y, 0.0, 1.0);\n"
    "    v_uv = a_uv;\n"
    "    v_col = a_col;\n"
    "}\n";

static const char *fs_src =
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "uniform sampler2D u_tex;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_col;\n"
    "void main() {\n"
    "    gl_FragColor = v_col * texture2D(u_tex, v_uv);\n"
    "}\n";

static GLuint make_texture(int w, int h, const void *data, GLint filter)
{
    GLuint t = 0;
    p_glGenTextures(1, &t);
    p_glActiveTexture(GL_TEXTURE0);
    p_glBindTexture(GL_TEXTURE_2D, t);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    p_glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    return t;
}

int ugl_init_resources(void)
{
    GLuint vs, fs;
    GLint ok = 0;
    uint8_t *rgba;
    int i;

    vs = compile(GL_VERTEX_SHADER, vs_src);
    fs = compile(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs)
        return -1;
    l_prog = p_glCreateProgram();
    p_glAttachShader(l_prog, vs);
    p_glAttachShader(l_prog, fs);
    p_glBindAttribLocation(l_prog, 0, "a_pos");
    p_glBindAttribLocation(l_prog, 1, "a_uv");
    p_glBindAttribLocation(l_prog, 2, "a_col");
    p_glLinkProgram(l_prog);
    p_glGetProgramiv(l_prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[512] = "";
        p_glGetProgramInfoLog(l_prog, sizeof(log), NULL, log);
        DebugMessage(M64MSG_ERROR, "overlay shader link failed: %s", log);
        return -1;
    }
    p_glDeleteShader(vs);
    p_glDeleteShader(fs);
    l_loc_inv = p_glGetUniformLocation(l_prog, "u_inv");
    l_loc_tex = p_glGetUniformLocation(l_prog, "u_tex");

    /* font atlas: white RGB, glyph coverage in alpha */
    rgba = (uint8_t *) malloc(FONT_ATLAS_W * FONT_ATLAS_H * 4);
    if (rgba == NULL)
        return -1;
    for (i = 0; i < FONT_ATLAS_W * FONT_ATLAS_H; i++)
    {
        rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = font_atlas_alpha[i];
    }
    l_atlas_tex = make_texture(FONT_ATLAS_W, FONT_ATLAS_H, rgba, GL_NEAREST);
    free(rgba);
    l_bg_tex = 0;
    l_cur_tex = 0;
    return p_glGetError() == GL_NO_ERROR ? 0 : -1;
}

void ugl_shutdown_resources(void)
{
    if (l_prog) p_glDeleteProgram(l_prog);
    if (l_atlas_tex) p_glDeleteTextures(1, &l_atlas_tex);
    if (l_bg_tex) p_glDeleteTextures(1, &l_bg_tex);
    l_prog = l_atlas_tex = l_bg_tex = 0;
    free(l_cap_buf); l_cap_buf = NULL;
    free(l_rb_buf);  l_rb_buf = NULL; l_rb_size = 0;
}

/* ---- capturing the game frame (runs in the GAME context; must not disturb it) ---- */
int ugl_capture_frame(int w, int h)
{
    GLint fbo = 0, pbo = 0;
    int x, y;

    if (w <= 0 || h <= 0)
        return -1;
    if (l_cap_w != w || l_cap_h != h || l_cap_buf == NULL)
    {
        free(l_cap_buf);
        l_cap_buf = (uint8_t *) malloc((size_t) w * h * 4);
        if (l_cap_buf == NULL)
            return -1;
        l_cap_w = w;
        l_cap_h = h;
    }

    /* the video plugin may have an FBO and/or a pixel-pack buffer bound; neither may redirect our read,
     * and both bindings have to be put back exactly as they were */
    p_glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    if (l_have_pbo_enum)
        p_glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pbo);
    if (fbo != 0)
        p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (pbo != 0)
        p_glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    p_glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, l_cap_buf);

    if (pbo != 0)
        p_glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint) pbo);
    if (fbo != 0)
        p_glBindFramebuffer(GL_FRAMEBUFFER, (GLuint) fbo);

    /* never let our own failure leak into the plugin's glGetError() checks */
    {
        int guard = 8;
        int failed = 0;
        while (guard-- > 0 && p_glGetError() != GL_NO_ERROR)
            failed = 1;
        if (failed)
            DebugMessage(M64MSG_VERBOSE, "GL error while capturing the game frame");
    }

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            l_cap_buf[((size_t) y * w + x) * 4 + 3] = 255;
    return 0;
}

const uint8_t *ugl_capture_data(int *w, int *h)
{
    if (w) *w = l_cap_w;
    if (h) *h = l_cap_h;
    return l_cap_buf;
}

/* ---- overlay-context drawing ---- */
void ugl_background_upload(void)
{
    if (l_cap_buf == NULL)
        return;
    if (l_bg_tex != 0)
        p_glDeleteTextures(1, &l_bg_tex);
    l_bg_tex = make_texture(l_cap_w, l_cap_h, l_cap_buf, GL_LINEAR);
    l_bg_w = l_cap_w;
    l_bg_h = l_cap_h;
}

static void flush_batch(void)
{
    if (l_nverts == 0)
        return;
    p_glVertexAttribPointer(0, 2, GL_FLOAT, 0, sizeof(vertex_t), &l_verts[0].x);
    p_glVertexAttribPointer(1, 2, GL_FLOAT, 0, sizeof(vertex_t), &l_verts[0].u);
    p_glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, 1, sizeof(vertex_t), &l_verts[0].r);
    p_glDrawArrays(GL_TRIANGLES, 0, l_nverts);
    l_nverts = 0;
}

static void bind_tex(GLuint t)
{
    if (t != l_cur_tex)
    {
        flush_batch();
        p_glBindTexture(GL_TEXTURE_2D, t);
        l_cur_tex = t;
    }
}

static void begin_common(int w, int h, int clear)
{
    l_screen_w = w;
    l_screen_h = h;
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    p_glViewport(0, 0, w, h);
    p_glDisable(GL_DEPTH_TEST);
    p_glDisable(GL_CULL_FACE);
    p_glDisable(GL_SCISSOR_TEST);
    p_glDisable(GL_STENCIL_TEST);
    p_glDisable(GL_DITHER);
    p_glEnable(GL_BLEND);
    p_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (clear)
    {
        p_glClearColor(0.f, 0.f, 0.f, 1.f);
        p_glClear(GL_COLOR_BUFFER_BIT);
    }

    p_glUseProgram(l_prog);
    p_glUniform2f(l_loc_inv, 2.0f / (float) w, 2.0f / (float) h);
    p_glUniform1i(l_loc_tex, 0);
    p_glActiveTexture(GL_TEXTURE0);
    p_glEnableVertexAttribArray(0);
    p_glEnableVertexAttribArray(1);
    p_glEnableVertexAttribArray(2);
    l_nverts = 0;
    l_cur_tex = 0;
    bind_tex(l_atlas_tex);
}

void ugl_begin(int w, int h)
{
    begin_common(w, h, 1);
}

void ugl_begin_overlay(int w, int h)
{
    begin_common(w, h, 0);
}

void ugl_end(void)
{
    flush_batch();
}

static void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t rgba)
{
    vertex_t *v;
    uint8_t r = (uint8_t) (rgba >> 24), g = (uint8_t) (rgba >> 16), b = (uint8_t) (rgba >> 8), a = (uint8_t) rgba;
    if (l_nverts + 6 > MAX_VERTS)
        flush_batch();
    v = &l_verts[l_nverts];
    v[0] = (vertex_t) { x0, y0, u0, v0, r, g, b, a };
    v[1] = (vertex_t) { x1, y0, u1, v0, r, g, b, a };
    v[2] = (vertex_t) { x0, y1, u0, v1, r, g, b, a };
    v[3] = (vertex_t) { x1, y0, u1, v0, r, g, b, a };
    v[4] = (vertex_t) { x1, y1, u1, v1, r, g, b, a };
    v[5] = (vertex_t) { x0, y1, u0, v1, r, g, b, a };
    l_nverts += 6;
}

void ugl_background_draw(int w, int h, float brightness)
{
    uint8_t c;
    if (l_bg_tex == 0)
        return;
    if (brightness < 0.f) brightness = 0.f;
    if (brightness > 1.f) brightness = 1.f;
    c = (uint8_t) (brightness * 255.f + 0.5f);
    bind_tex(l_bg_tex);
    /* captured data is bottom-up, which is exactly GL's texture orientation: v=0 is the screen bottom */
    quad(0.f, 0.f, (float) w, (float) h, 0.f, 1.f, 1.f, 0.f, UGL_RGBA(c, c, c, 255));
    bind_tex(l_atlas_tex);
}

void ugl_rect(int x, int y, int w, int h, uint32_t rgba)
{
    /* sample the middle of the solid cell */
    float cu = ((FONT_SOLID_CELL % FONT_ATLAS_COLS) * FONT_GLYPH_W + FONT_GLYPH_W / 2) / (float) FONT_ATLAS_W;
    float cv = ((FONT_SOLID_CELL / FONT_ATLAS_COLS) * FONT_GLYPH_H + FONT_GLYPH_H / 2) / (float) FONT_ATLAS_H;
    bind_tex(l_atlas_tex);
    quad((float) x, (float) y, (float) (x + w), (float) (y + h), cu, cv, cu, cv, rgba);
}

int ugl_text_width(int scale, const char *str)
{
    return (int) strlen(str) * FONT_GLYPH_W * scale;
}

void ugl_text(int x, int y, int scale, uint32_t rgba, const char *str)
{
    bind_tex(l_atlas_tex);
    for (; *str; str++)
    {
        unsigned char c = (unsigned char) *str;
        if (c < FONT_FIRST_CHAR || c > FONT_LAST_CHAR)
            c = '?';
        if (c != ' ')
        {
            int idx = c - FONT_FIRST_CHAR;
            float u0 = (idx % FONT_ATLAS_COLS) * FONT_GLYPH_W / (float) FONT_ATLAS_W;
            float v0 = (idx / FONT_ATLAS_COLS) * FONT_GLYPH_H / (float) FONT_ATLAS_H;
            quad((float) x, (float) y, (float) (x + FONT_GLYPH_W * scale), (float) (y + FONT_GLYPH_H * scale),
                 u0, v0, u0 + FONT_GLYPH_W / (float) FONT_ATLAS_W, v0 + FONT_GLYPH_H / (float) FONT_ATLAS_H, rgba);
        }
        x += FONT_GLYPH_W * scale;
    }
}

const uint8_t *ugl_readback(int w, int h)
{
    size_t need = (size_t) w * h * 4;
    flush_batch();
    if (l_rb_size < need)
    {
        free(l_rb_buf);
        l_rb_buf = (uint8_t *) malloc(need);
        l_rb_size = l_rb_buf ? need : 0;
        if (l_rb_buf == NULL)
            return NULL;
    }
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    p_glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, l_rb_buf);
    return l_rb_buf;
}
