/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - uitext.c
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
#include <string.h>

#include "font.h"
#include "uitext.h"

void uitext_fit(char *buf, size_t size, const char *src, int avail, uitext_measure m, void *ctx)
{
    size_t k = strlen(src);
    snprintf(buf, size, "%s", src);
    while (k > 1 && m(ctx, buf) > avail)
    {
        k--;
        snprintf(buf, size, "%.*s..", (int) k, src);
    }
}

int uitext_wrap2(const char *src, int avail, uitext_measure m, void *ctx, char *line1, char *line2, size_t size)
{
    char tmp[256];
    const char *nl = strchr(src, '\n');
    size_t len = nl != NULL ? (size_t) (nl - src) : strlen(src), brk, i;

    line2[0] = 0;
    if (len >= sizeof(tmp))
        len = sizeof(tmp) - 1;
    memcpy(tmp, src, len);
    tmp[len] = 0;
    if (nl == NULL && m(ctx, tmp) <= avail)
    {
        snprintf(line1, size, "%s", tmp);
        return 1;
    }
    if (nl != NULL)
    {
        snprintf(line1, size, "%s", tmp);
        uitext_fit(line1, size, tmp, avail, m, ctx);
        snprintf(tmp, sizeof(tmp), "%s", nl + 1);
        uitext_fit(line2, size, tmp, avail, m, ctx);
        return line2[0] ? 2 : 1;
    }
    brk = 0;
    for (i = 1; i <= len; i++)
    {
        char keep = tmp[i];
        tmp[i] = 0;
        if (m(ctx, tmp) > avail)
        {
            tmp[i] = keep;
            break;
        }
        tmp[i] = keep;
        brk = i;
    }
    if (brk < len)
    {
        size_t sp = brk;
        while (sp > 0 && tmp[sp] != ' ')
            sp--;
        if (sp > 0)
            brk = sp;
    }
    if (brk == 0)
        brk = 1;
    {
        char first[256];
        const char *rest = tmp + brk;
        memcpy(first, tmp, brk);
        first[brk] = 0;
        while (brk > 1 && first[brk - 1] == ' ')
            first[--brk] = 0;
        while (*rest == ' ')
            rest++;
        snprintf(line1, size, "%s", first);
        uitext_fit(line2, size, rest, avail, m, ctx);
    }
    return line2[0] ? 2 : 1;
}

void uitext_arrows(int up, int down, int left, int right, char out[UITEXT_ARROWS_LEN + 1])
{
    snprintf(out, UITEXT_ARROWS_LEN + 1, "%s %s %s %s",
             up ? FONT_CH_UP : " ", down ? FONT_CH_DOWN : " ", left ? FONT_CH_LEFT : " ", right ? FONT_CH_RIGHT : " ");
}

int uitext_btn_cells(const uitext_btn *tab, int n, uint32_t mask, char *ghost, char *on, size_t size)
{
    size_t gl = 0, ol = 0;
    int npress = 0, i;

    if (size == 0)
        return 0;
    ghost[0] = on[0] = 0;
    for (i = 0; i < n; i++)
    {
        size_t k, len = strlen(tab[i].name);
        int down = (mask & tab[i].bit) != 0;

        if (gl + (i ? 1u : 0u) + len + 1 > size)
            break;                                       /* cannot happen with the N64's six buttons; never overrun */
        if (i)
        {
            ghost[gl++] = ' ';
            on[ol++] = ' ';
        }
        memcpy(ghost + gl, tab[i].name, len);
        gl += len;
        for (k = 0; k < len; k++)
            on[ol++] = down ? tab[i].name[k] : ' ';
        npress += down;
    }
    ghost[gl] = on[ol] = 0;
    return npress;
}

void uitext_remap_msg(char *out, size_t size, const char *control, const char *target, const char *was)
{
    if (was != NULL && was[0])
        snprintf(out, size, "%s " FONT_CH_RIGHT " %s (was %s)", control, target, was);
    else
        snprintf(out, size, "%s " FONT_CH_RIGHT " %s", control, target);
}
