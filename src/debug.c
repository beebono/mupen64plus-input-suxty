/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-ui-suxty - debug.c
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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "m64p_types.h"
#include "main.h"
#include "pad.h"
#include "padmap.h"

#define MAX_CMDS 512

typedef struct {
    enum { C_WAIT, C_MENU, C_SHOT, C_QUIT, C_KILL, C_STAT, C_PJSAVE, C_BUTTON, C_HOLD, C_CAP } kind;
    int arg;
    uint32_t mask;
    padsrc src;
} cmd_t;

static cmd_t l_cmds[MAX_CMDS];
static int   l_ncmds, l_pos;
static int   l_wait_left;
static int   l_release_tick;
static int   l_hold_left;
static uint32_t l_hold_mask;
static char  l_prefix[1024];
static int   l_shot_n;

static void add(int kind, int arg, uint32_t mask)
{
    if (l_ncmds < MAX_CMDS)
    {
        l_cmds[l_ncmds].kind = kind;
        l_cmds[l_ncmds].arg = arg;
        l_cmds[l_ncmds].mask = mask;
        l_ncmds++;
    }
}

static void parse(const char *script)
{
    char *copy = strdup(script), *tok, *save = NULL;
    if (copy == NULL)
        return;
    for (tok = strtok_r(copy, ", ", &save); tok != NULL; tok = strtok_r(NULL, ", ", &save))
    {
        int i, found = 0;
        if (strncmp(tok, "wait", 4) == 0)
            add(C_WAIT, atoi(tok + 4), 0);
        else if (strcmp(tok, "menu") == 0)
            add(C_MENU, 0, 0);
        else if (strcmp(tok, "shot") == 0)
            add(C_SHOT, 0, 0);
        else if (strcmp(tok, "quit") == 0)
            add(C_QUIT, 0, 0);
        else if (strcmp(tok, "kill") == 0)
            add(C_KILL, 0, 0);
        else if (strcmp(tok, "stat") == 0)
            add(C_STAT, 0, 0);
        else if (strncmp(tok, "pjsave:", 7) == 0)
            add(C_PJSAVE, atoi(tok + 7), 0);
        else if (strncmp(tok, "pjzip:", 6) == 0)
            add(C_PJSAVE, atoi(tok + 6), 1);
        else if (strncmp(tok, "cap:", 4) == 0)
        {
            padsrc s;
            if (padsrc_parse(tok + 4, &s) == 0 && s.type != PAD_BIND_NONE)
            {
                add(C_CAP, 0, 0);
                l_cmds[l_ncmds - 1].src = s;
            }
            else
                DebugMessage(M64MSG_WARNING, "debug script: bad capture token '%s'", tok);
        }
        else
        {
            /* "a", "select+r", or "hold30:select+y" */
            const char *btns = tok;
            int hold = 0;
            uint32_t mask;
            if (strncmp(tok, "hold", 4) == 0 && strchr(tok, ':') != NULL)
            {
                hold = atoi(tok + 4);
                btns = strchr(tok, ':') + 1;
            }
            mask = pad_parse_combo(btns);
            found = mask != 0;
            (void) i;
            if (found)
                add(hold > 0 ? C_HOLD : C_BUTTON, hold, mask);
            else
                DebugMessage(M64MSG_WARNING, "debug script: unknown token '%s'", tok);
        }
    }
    free(copy);
}

void dbg_configure(const char *script, const char *shot_prefix, int open_menu_after)
{
    l_ncmds = l_pos = l_wait_left = l_release_tick = l_hold_left = 0;
    l_hold_mask = 0;
    l_shot_n = 0;
    l_prefix[0] = 0;
    if (shot_prefix != NULL)
        snprintf(l_prefix, sizeof(l_prefix), "%s", shot_prefix);
    if (open_menu_after > 0)
    {
        add(C_WAIT, open_menu_after, 0);
        add(C_MENU, 0, 0);
    }
    if (script != NULL)
        parse(script);
}

int dbg_active(void)
{
    return l_pos < l_ncmds;
}

void dbg_tick(dbg_tick_t *out)
{
    memset(out, 0, sizeof(*out));
    out->pj_slot = -1;
    if (l_hold_left > 0)            /* a hold command in progress */
    {
        out->buttons = l_hold_mask;
        if (--l_hold_left == 0)
            l_release_tick = 1;
        return;
    }
    if (l_release_tick)             /* the gap tick after a button press */
    {
        l_release_tick = 0;
        return;
    }
    if (l_wait_left > 0)
    {
        l_wait_left--;
        return;
    }
    if (l_pos >= l_ncmds)
        return;
    {
        cmd_t *c = &l_cmds[l_pos++];
        switch (c->kind)
        {
            case C_WAIT:   l_wait_left = c->arg > 0 ? c->arg - 1 : 0; break;
            case C_MENU:   out->toggle_menu = 1; break;
            case C_SHOT:   out->shot = 1; break;
            case C_QUIT:   out->quit = 1; break;
            case C_KILL:   out->kill = 1; break;
            case C_STAT:   out->stat = 1; break;
            case C_PJSAVE: out->pj_slot = c->arg; out->pj_zip = (int) c->mask; break;
            case C_CAP:    out->cap = 1; out->capsrc = c->src; break;
            case C_BUTTON: out->buttons = c->mask; l_release_tick = 1; break;
            case C_HOLD:   out->buttons = c->mask; l_hold_mask = c->mask; l_hold_left = c->arg - 1; if (l_hold_left == 0) l_release_tick = 1; break;
        }
    }
}

const char *dbg_next_shot_path(char *buf, unsigned size)
{
    if (l_prefix[0] == 0)
        return NULL;
    snprintf(buf, size, "%s-%d.png", l_prefix, ++l_shot_n);
    return buf;
}

/* ---- PNG ---- */
static uint32_t l_crc_table[256];

static void crc_init(void)
{
    uint32_t n, k, c;
    if (l_crc_table[1])
        return;
    for (n = 0; n < 256; n++)
    {
        c = n;
        for (k = 0; k < 8; k++)
            c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
        l_crc_table[n] = c;
    }
}

static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        crc = l_crc_table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    return crc;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24); p[1] = (uint8_t) (v >> 16); p[2] = (uint8_t) (v >> 8); p[3] = (uint8_t) v;
}

static void chunk(FILE *f, const char *type, const uint8_t *data, size_t len)
{
    uint8_t hdr[8], tail[4];
    uint32_t crc;
    put32(hdr, (uint32_t) len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len)
        fwrite(data, 1, len, f);
    crc = crc_update(0xffffffffu, (const uint8_t *) type, 4);
    if (len)
        crc = crc_update(crc, data, len);
    put32(tail, crc ^ 0xffffffffu);
    fwrite(tail, 1, 4, f);
}

int dbg_save_png(const char *path, const uint8_t *rgba, int w, int h)
{
    FILE *f;
    uint8_t ihdr[13];
    size_t rowbytes = (size_t) w * 3 + 1;
    size_t rawlen = rowbytes * (size_t) h;
    size_t zlen = 2 + rawlen + ((rawlen + 65534) / 65535) * 5 + 4;
    uint8_t *raw, *z;
    size_t pos = 0, off = 0;
    uint32_t a = 1, b = 0;
    int x, y;

    if (rgba == NULL || w <= 0 || h <= 0)
        return -1;
    crc_init();
    raw = (uint8_t *) malloc(rawlen);
    z = (uint8_t *) malloc(zlen);
    if (raw == NULL || z == NULL)
    {
        free(raw);
        free(z);
        return -1;
    }
    for (y = 0; y < h; y++)
    {
        const uint8_t *src = rgba + (size_t) (h - 1 - y) * w * 4;     /* flip: GL is bottom-up */
        uint8_t *dst = raw + (size_t) y * rowbytes;
        *dst++ = 0;
        for (x = 0; x < w; x++, src += 4, dst += 3)
        {
            dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
        }
    }
    /* zlib stream made of stored deflate blocks */
    z[pos++] = 0x78;
    z[pos++] = 0x01;
    while (off < rawlen)
    {
        size_t n = rawlen - off > 65535 ? 65535 : rawlen - off;
        z[pos++] = (off + n == rawlen) ? 1 : 0;
        z[pos++] = (uint8_t) (n & 0xff);
        z[pos++] = (uint8_t) (n >> 8);
        z[pos++] = (uint8_t) (~n & 0xff);
        z[pos++] = (uint8_t) ((~n >> 8) & 0xff);
        memcpy(z + pos, raw + off, n);
        pos += n;
        off += n;
    }
    for (off = 0; off < rawlen; off++)
    {
        a = (a + raw[off]) % 65521u;
        b = (b + a) % 65521u;
    }
    put32(z + pos, (b << 16) | a);
    pos += 4;

    f = fopen(path, "wb");
    if (f == NULL)
    {
        free(raw);
        free(z);
        return -1;
    }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    put32(ihdr, (uint32_t) w);
    put32(ihdr + 4, (uint32_t) h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, pos);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
    return 0;
}
