/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   mupen64plus-input-suxty - autosave.c
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
#include <limits.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "autosave.h"

/* ---- a small streaming gzip reader (RFC 1951 and 1952), after the structure of the puff.c of zlib
 * It checks and discards: only a 32 KB window and the first 44 bytes are kept, so the 16.8 MB state is never in memory. */

#define WIN_SIZE   32768u
#define HDR_SIZE   44
#define MAXBITS    15
#define MAXLCODES  286
#define MAXDCODES  30
#define FIXLCODES  288

typedef struct {
    FILE          *f;
    unsigned char  buf[16384];
    size_t         len, pos;
    uint32_t       bitbuf;
    int            bitcnt;
    unsigned char  win[WIN_SIZE];
    uint32_t       wpos;
    uint64_t       total;
    uint32_t       crc;                       /* running, not yet inverted */
    unsigned char  head[HDR_SIZE];
    uint32_t       version;
    jmp_buf        jb;
    const char    *why;
    char           whybuf[64];
} inflater;

typedef struct {
    short count[MAXBITS + 1];
    short symbol[FIXLCODES];
} huffman;

static uint32_t l_crc_table[256];
static int      l_crc_ready;

static void crc_init(void)
{
    uint32_t n, k;
    if (l_crc_ready)
        return;
    for (n = 0; n < 256; n++)
    {
        uint32_t c = n;
        for (k = 0; k < 8; k++)
            c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
        l_crc_table[n] = c;
    }
    l_crc_ready = 1;
}

static void fail(inflater *s, const char *why)
{
    s->why = why;
    longjmp(s->jb, 1);
}

static unsigned getbyte(inflater *s)
{
    if (s->pos >= s->len)
    {
        s->len = fread(s->buf, 1, sizeof(s->buf), s->f);
        s->pos = 0;
        if (s->len == 0)
            fail(s, "truncated");
    }
    return s->buf[s->pos++];
}

static unsigned bits(inflater *s, int need)
{
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need)
    {
        val |= (uint32_t) getbyte(s) << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return val & ((1u << need) - 1);
}

/* the largest size any supported version can have */
#define MAX_TOTAL ((uint64_t) AUTOSAVE_SIZE_V12)

static void check_header(inflater *s)
{
    if (memcmp(s->head, "M64+SAVE", 8) != 0)
        fail(s, "bad magic");
    s->version = ((uint32_t) s->head[8] << 24) | ((uint32_t) s->head[9] << 16) | ((uint32_t) s->head[10] << 8) | s->head[11];
    if ((s->version >> 16) != 1)
    {
        snprintf(s->whybuf, sizeof(s->whybuf), "unsupported version %08x", (unsigned) s->version);
        fail(s, s->whybuf);
    }
}

static void emit(inflater *s, unsigned char b)
{
    if (s->total >= MAX_TOTAL)
        fail(s, "too long");
    s->win[s->wpos++ & (WIN_SIZE - 1)] = b;
    s->crc = l_crc_table[(s->crc ^ b) & 0xff] ^ (s->crc >> 8);
    if (s->total < HDR_SIZE)
    {
        s->head[s->total] = b;
        if (s->total + 1 == HDR_SIZE)
        {
            s->total++;
            check_header(s);
            return;
        }
    }
    s->total++;
}

static int decode(inflater *s, const huffman *h)
{
    int len, code = 0, first = 0, index = 0;
    for (len = 1; len <= MAXBITS; len++)
    {
        int count;
        code |= (int) bits(s, 1);
        count = h->count[len];
        if (code - count < first)
            return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    fail(s, "bad code");
    return -1;
}

/* 0 = complete code, > 0 = incomplete, < 0 = over-subscribed */
static int construct(huffman *h, const short *length, int n)
{
    int symbol, len, left;
    short offs[MAXBITS + 1];

    for (len = 0; len <= MAXBITS; len++)
        h->count[len] = 0;
    for (symbol = 0; symbol < n; symbol++)
        h->count[length[symbol]]++;
    if (h->count[0] == n)
        return 0;
    left = 1;
    for (len = 1; len <= MAXBITS; len++)
    {
        left <<= 1;
        left -= h->count[len];
        if (left < 0)
            return left;
    }
    offs[1] = 0;
    for (len = 1; len < MAXBITS; len++)
        offs[len + 1] = (short) (offs[len] + h->count[len]);
    for (symbol = 0; symbol < n; symbol++)
        if (length[symbol] != 0)
            h->symbol[offs[length[symbol]]++] = (short) symbol;
    return left;
}

static void stored_block(inflater *s)
{
    unsigned len, nlen;
    s->bitbuf = 0;
    s->bitcnt = 0;
    len = getbyte(s);
    len |= getbyte(s) << 8;
    nlen = getbyte(s);
    nlen |= getbyte(s) << 8;
    if (len != (~nlen & 0xffff))
        fail(s, "bad stored block");
    while (len--)
        emit(s, (unsigned char) getbyte(s));
}

static void codes(inflater *s, const huffman *lencode, const huffman *distcode)
{
    static const short lens[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
    static const short lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
    static const short dists[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
    static const short dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
    int symbol;

    for (;;)
    {
        symbol = decode(s, lencode);
        if (symbol < 256)
            emit(s, (unsigned char) symbol);
        else if (symbol == 256)
            return;
        else
        {
            unsigned len, dist;
            symbol -= 257;
            if (symbol >= 29)
                fail(s, "bad length symbol");
            len = (unsigned) lens[symbol] + bits(s, lext[symbol]);
            symbol = decode(s, distcode);
            if (symbol >= 30)
                fail(s, "bad distance symbol");
            dist = (unsigned) dists[symbol] + bits(s, dext[symbol]);
            if (dist > s->total || dist > WIN_SIZE)
                fail(s, "bad distance");
            while (len--)
                emit(s, s->win[(s->wpos - dist) & (WIN_SIZE - 1)]);
        }
    }
}

static void fixed_block(inflater *s)
{
    huffman lencode, distcode;
    short lengths[FIXLCODES];
    int symbol;

    for (symbol = 0; symbol < 144; symbol++) lengths[symbol] = 8;
    for (; symbol < 256; symbol++)           lengths[symbol] = 9;
    for (; symbol < 280; symbol++)           lengths[symbol] = 7;
    for (; symbol < FIXLCODES; symbol++)     lengths[symbol] = 8;
    construct(&lencode, lengths, FIXLCODES);
    for (symbol = 0; symbol < MAXDCODES; symbol++)
        lengths[symbol] = 5;
    construct(&distcode, lengths, MAXDCODES);
    codes(s, &lencode, &distcode);
}

static void dynamic_block(inflater *s)
{
    static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    int nlen, ndist, ncode, index, err;
    short lengths[MAXLCODES + MAXDCODES + 19];
    huffman lencode, distcode;

    nlen = (int) bits(s, 5) + 257;
    ndist = (int) bits(s, 5) + 1;
    ncode = (int) bits(s, 4) + 4;
    if (nlen > MAXLCODES || ndist > MAXDCODES)
        fail(s, "bad counts");
    for (index = 0; index < ncode; index++)
        lengths[order[index]] = (short) bits(s, 3);
    for (; index < 19; index++)
        lengths[order[index]] = 0;
    if (construct(&lencode, lengths, 19) != 0)
        fail(s, "bad code lengths code");
    index = 0;
    while (index < nlen + ndist)
    {
        int symbol = decode(s, &lencode);
        if (symbol < 16)
            lengths[index++] = (short) symbol;
        else
        {
            int len = 0;
            if (symbol == 16)
            {
                if (index == 0)
                    fail(s, "bad repeat");
                len = lengths[index - 1];
                symbol = 3 + (int) bits(s, 2);
            }
            else if (symbol == 17)
                symbol = 3 + (int) bits(s, 3);
            else
                symbol = 11 + (int) bits(s, 7);
            if (index + symbol > nlen + ndist)
                fail(s, "bad repeat");
            while (symbol--)
                lengths[index++] = (short) len;
        }
    }
    if (lengths[256] == 0)
        fail(s, "no end-of-block code");
    err = construct(&lencode, lengths, nlen);
    if (err != 0 && (err < 0 || nlen != lencode.count[0] + lencode.count[1]))
        fail(s, "bad literal/length code");
    err = construct(&distcode, lengths + nlen, ndist);
    if (err != 0 && (err < 0 || ndist != distcode.count[0] + distcode.count[1]))
        fail(s, "bad distance code");
    codes(s, &lencode, &distcode);
}

static int size_ok(uint32_t version, uint64_t total)
{
    if (version == 0x00010000u)
        return total >= (uint64_t) AUTOSAVE_SIZE_V10_MIN && total <= (uint64_t) AUTOSAVE_SIZE_V10_MAX &&
               ((total - (uint64_t) AUTOSAVE_SIZE_V10_MIN) % 4) == 0;      /* the core only needs the queue to be a multiple of 4 */
    if (version == 0x00010100u)
        return total == (uint64_t) AUTOSAVE_SIZE_V11;
    return total == (uint64_t) AUTOSAVE_SIZE_V12;
}

int autosave_validate(const char *path, char *why, size_t why_size)
{
    inflater *s;
    volatile int rc = -1;
    const char *volatile w = "unreadable";
    unsigned flags, b;

    if (why_size > 0)
        why[0] = 0;
    s = calloc(1, sizeof(*s));
    if (s == NULL)
        return -1;
    crc_init();
    s->f = fopen(path, "rb");
    if (s->f == NULL)
    {
        snprintf(why, why_size, "%s", strerror(errno));
        free(s);
        return -1;
    }
    s->crc = 0xffffffffu;
    if (setjmp(s->jb) == 0)
    {
        int last;
        uint32_t tcrc = 0, tsize = 0;
        int i;

        if (getbyte(s) != 0x1f || getbyte(s) != 0x8b)
            fail(s, "not gzip");
        if (getbyte(s) != 8)
            fail(s, "not deflate");
        flags = getbyte(s);
        for (i = 0; i < 6; i++)
            (void) getbyte(s);                                   /* mtime, xfl, os */
        if (flags & 4)
        {
            unsigned xlen = getbyte(s);
            xlen |= getbyte(s) << 8;
            while (xlen--)
                (void) getbyte(s);
        }
        if (flags & 8)
            while ((b = getbyte(s)) != 0) { }
        if (flags & 16)
            while ((b = getbyte(s)) != 0) { }
        if (flags & 2)
        {
            (void) getbyte(s);
            (void) getbyte(s);
        }
        do
        {
            unsigned type;
            last = (int) bits(s, 1);
            type = bits(s, 2);
            if (type == 0)
                stored_block(s);
            else if (type == 1)
                fixed_block(s);
            else if (type == 2)
                dynamic_block(s);
            else
                fail(s, "bad block type");
        } while (!last);
        s->bitbuf = 0;
        s->bitcnt = 0;
        for (i = 0; i < 4; i++)
            tcrc |= (uint32_t) getbyte(s) << (8 * i);
        for (i = 0; i < 4; i++)
            tsize |= (uint32_t) getbyte(s) << (8 * i);
        if (s->total < HDR_SIZE)
            fail(s, "too short");
        if (tcrc != ~s->crc)
            fail(s, "bad crc");
        if (tsize != (uint32_t) s->total)
            fail(s, "bad length");
        if (!size_ok(s->version, s->total))
            fail(s, "wrong size");
        rc = 0;
    }
    else
        w = s->why != NULL ? s->why : "invalid";
    if (rc != 0 && why_size > 0)
        snprintf(why, why_size, "%s", w);
    fclose(s->f);
    free(s);
    return rc;
}

/* ---- backups ------------------------------------------------------------------------------------------------------ */

void autosave_backup_path(const char *path, int i, char *out, size_t size)
{
    if (i <= 0)
        snprintf(out, size, "%s.prev", path);
    else
        snprintf(out, size, "%s.prev%d", path, i + 1);
}

static int is_reg(const char *p, struct stat *st)
{
    struct stat tmp;
    if (st == NULL)
        st = &tmp;
    return lstat(p, st) == 0 && S_ISREG(st->st_mode);
}

int autosave_rotate(const char *path)
{
    char b[PATH_MAX + 16];
    struct stat st, bs;
    int i, slot = -1, oldest = -1;
    time_t oldest_t = 0;

    if (!is_reg(path, &st))
        return 0;
    for (i = 0; i < AUTOSAVE_MAX_BACKUPS; i++)
    {
        autosave_backup_path(path, i, b, sizeof(b));
        if (lstat(b, &bs) != 0)
        {
            if (slot < 0)
                slot = i;
        }
        else if (oldest < 0 || bs.st_mtime < oldest_t)
        {
            oldest = i;
            oldest_t = bs.st_mtime;
        }
    }
    if (slot < 0)
    {
        slot = oldest;                                  /* all taken: the oldest one goes */
        autosave_backup_path(path, slot, b, sizeof(b));
        if (unlink(b) != 0)
            return -1;
    }
    autosave_backup_path(path, slot, b, sizeof(b));
    return rename(path, b) == 0 ? 1 : -1;
}

/* the backup slots that exist, newest first (by modification time); returns how many */
static int backups_newest_first(const char *path, int order[AUTOSAVE_MAX_BACKUPS])
{
    char b[PATH_MAX + 16];
    time_t t[AUTOSAVE_MAX_BACKUPS];
    int i, n = 0, j;
    struct stat st;

    for (i = 0; i < AUTOSAVE_MAX_BACKUPS; i++)
    {
        autosave_backup_path(path, i, b, sizeof(b));
        if (!is_reg(b, &st))
            continue;
        for (j = n; j > 0 && t[j - 1] < st.st_mtime; j--)
        {
            t[j] = t[j - 1];
            order[j] = order[j - 1];
        }
        t[j] = st.st_mtime;
        order[j] = i;
        n++;
    }
    return n;
}

int autosave_unrotate(const char *path)
{
    int order[AUTOSAVE_MAX_BACKUPS];
    char b[PATH_MAX + 16];
    struct stat st;

    if (lstat(path, &st) == 0 || errno != ENOENT)
        return 0;
    if (backups_newest_first(path, order) == 0)
        return 0;
    autosave_backup_path(path, order[0], b, sizeof(b));
    return rename(b, path) == 0 ? 1 : 0;
}

int autosave_remove_all(const char *path)
{
    char b[PATH_MAX + 16];
    int i, rc = 0;

    if (unlink(path) != 0 && errno != ENOENT)
        rc = -1;
    for (i = 0; i < AUTOSAVE_MAX_BACKUPS; i++)
    {
        autosave_backup_path(path, i, b, sizeof(b));
        if (unlink(b) != 0 && errno != ENOENT)
            rc = -1;
    }
    return rc;
}

static int remove_backups(const char *path, int except)
{
    char b[PATH_MAX + 16];
    int i, n = 0;
    for (i = 0; i < AUTOSAVE_MAX_BACKUPS; i++)
    {
        if (i == except)
            continue;
        autosave_backup_path(path, i, b, sizeof(b));
        if (unlink(b) == 0)
            n++;
    }
    return n;
}

void autosave_reconcile(const char *path, autosave_report *rep)
{
    struct stat st;
    int order[AUTOSAVE_MAX_BACKUPS], nb, i;
    char why[64];

    memset(rep, 0, sizeof(*rep));
    if (lstat(path, &st) == 0 && !S_ISREG(st.st_mode))
        return;                                              /* a directory or so is not ours to judge */
    nb = backups_newest_first(path, order);
    rep->backups = nb;
    rep->was_missing = (lstat(path, &st) != 0);
    if (!rep->was_missing)
    {
        if (autosave_validate(path, why, sizeof(why)) == 0)
        {
            rep->status = AUTOSAVE_GOOD;
            rep->removed = remove_backups(path, -1);
            return;
        }
        snprintf(rep->why, sizeof(rep->why), "%s", why);
    }
    else
    {
        if (nb == 0)
            return;                                          /* nothing at all: ABSENT */
        snprintf(rep->why, sizeof(rep->why), "missing");
    }
    rep->status = AUTOSAVE_BAD;
    for (i = 0; i < nb; i++)
    {
        char b[PATH_MAX + 16];
        autosave_backup_path(path, order[i], b, sizeof(b));
        if (autosave_validate(b, why, sizeof(why)) != 0)
            continue;
        if (rename(b, path) != 0)                            /* replaces the damaged file atomically */
            break;
        snprintf(rep->from, sizeof(rep->from), "%s", b);
        rep->status = AUTOSAVE_RESTORED;
        rep->removed = remove_backups(path, -1);
        return;
    }
}

/* ---- the launcher's --savestate ------------------------------------------------------------------------------------ */

int autosave_cmdline_arg(const char *buf, size_t len, const char *opt, char *out, size_t out_size)
{
    size_t i = 0, ol = strlen(opt);

    while (i < len)
    {
        const char *a = buf + i;
        size_t al = strnlen(a, len - i);
        if (al == ol && memcmp(a, opt, ol) == 0)
        {
            size_t j = i + al + 1;
            if (j < len)
            {
                snprintf(out, out_size, "%.*s", (int) strnlen(buf + j, len - j), buf + j);
                return 1;
            }
            return 0;
        }
        if (al > ol + 1 && memcmp(a, opt, ol) == 0 && a[ol] == '=')
        {
            snprintf(out, out_size, "%.*s", (int) (al - ol - 1), a + ol + 1);
            return 1;
        }
        i += al + 1;
    }
    return 0;
}

int autosave_cmdline_savestate(char *out, size_t out_size)
{
    char buf[8192];
    size_t n;
    FILE *f = fopen("/proc/self/cmdline", "rb");
    if (f == NULL)
        return 0;
    n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    return autosave_cmdline_arg(buf, n, "--savestate", out, out_size);
}
