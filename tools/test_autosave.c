/* Unit tests for src/autosave.c and src/filewatch.c; run with make -C projects/unix check.
 * zlib only produces the test files (the plugin does not link it). GPLv2+, like the rest of the project. */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <zlib.h>

#include "autosave.h"
#include "filewatch.h"

static int l_fail, l_checks;

#define CHECK(cond) do { l_checks++; if (!(cond)) { l_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define STREQ(a, b) (strcmp((a), (b)) == 0)

static char l_dir[256];

static uint32_t l_rng = 12345;
static uint32_t rnd(void) { l_rng = l_rng * 1664525u + 1013904223u; return l_rng >> 8; }

enum { FILL_ZERO_PATTERN, FILL_RANDOM, FILL_TEXT };

/* "M64+SAVE", the version, 32 hex digits of "MD5", then `size - 44` bytes of `fill` */
static unsigned char *make_payload(uint32_t version, size_t size, int fill)
{
    unsigned char *p = malloc(size);
    size_t i;
    memcpy(p, "M64+SAVE", 8);
    p[8] = (unsigned char) (version >> 24); p[9] = (unsigned char) (version >> 16);
    p[10] = (unsigned char) (version >> 8); p[11] = (unsigned char) version;
    memcpy(p + 12, "0123456789ABCDEF0123456789ABCDEF", 32);
    for (i = 44; i < size; i++)
    {
        switch (fill)
        {
            case FILL_RANDOM: p[i] = (unsigned char) rnd(); break;
            case FILL_TEXT:   p[i] = "the quick brown fox jumps over the lazy dog 0123456789 "[(rnd() % 7 == 0 ? rnd() : i) % 55]; break;
            default:          p[i] = (i % 4099 < 200) ? (unsigned char) (i * 7) : 0; break;
        }
    }
    return p;
}

/* Z_FIXED forces fixed-Huffman blocks, level 0 stored blocks */
static void write_gz(const char *path, const unsigned char *data, size_t size, int level, int strategy)
{
    z_stream z;
    unsigned char out[65536];
    FILE *f = fopen(path, "wb");
    size_t done = 0;
    memset(&z, 0, sizeof(z));
    CHECK(f != NULL);
    CHECK(deflateInit2(&z, level, Z_DEFLATED, 15 + 16, 8, strategy) == Z_OK);
    while (done < size || z.avail_in != 0)
    {
        size_t chunk = size - done > 30000 ? 30000 : size - done;
        int fin = done + chunk >= size;
        int rc;
        z.next_in = (unsigned char *) data + done;
        z.avail_in = (unsigned) chunk;
        done += chunk;
        do
        {
            z.next_out = out;
            z.avail_out = sizeof(out);
            rc = deflate(&z, fin ? Z_FINISH : Z_NO_FLUSH);
            fwrite(out, 1, sizeof(out) - z.avail_out, f);
        } while (z.avail_out == 0);
        (void) rc;
        if (fin)
            break;
    }
    deflateEnd(&z);
    fclose(f);
}

static void write_raw(const char *path, const void *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) { perror(path); exit(2); }
    if (n) fwrite(data, 1, n, f);
    fclose(f);
}

static void copy_file(const char *from, const char *to)
{
    FILE *a = fopen(from, "rb"), *b = fopen(to, "wb");
    char buf[65536];
    size_t n;
    if (a == NULL || b == NULL) { perror("copy"); exit(2); }
    while ((n = fread(buf, 1, sizeof(buf), a)) > 0)
        fwrite(buf, 1, n, b);
    fclose(a);
    fclose(b);
}

static long long fsize(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (long long) st.st_size : -1;
}

static int exists(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0;
}

static void set_age(const char *path, int seconds_ago)
{
    struct timeval tv[2];
    gettimeofday(&tv[0], NULL);
    tv[0].tv_sec -= seconds_ago;
    tv[1] = tv[0];
    utimes(path, tv);
}

static void P(char *out, const char *name) { snprintf(out, 600, "%s/%s", l_dir, name); }

static int valid(const char *path, char *why)
{
    return autosave_validate(path, why, 64);
}

static unsigned char *l_good;
static char l_goodgz[600];

static void validate_tests(void)
{
    char p[600], why[64];
    unsigned char *d;
    FILE *f;

    printf("validate\n");
    CHECK(AUTOSAVE_SIZE_V12 == 16788288LL + 1024 + 4 + 4096);

    l_good = make_payload(0x00010900, AUTOSAVE_SIZE_V12, FILL_ZERO_PATTERN);
    P(l_goodgz, "good.gz");
    write_gz(l_goodgz, l_good, AUTOSAVE_SIZE_V12, 6, Z_DEFAULT_STRATEGY);
    CHECK(valid(l_goodgz, why) == 0);

    P(p, "t.gz");
    write_gz(p, l_good, AUTOSAVE_SIZE_V12, 1, Z_DEFAULT_STRATEGY);  CHECK(valid(p, why) == 0);
    write_gz(p, l_good, AUTOSAVE_SIZE_V12, 9, Z_DEFAULT_STRATEGY);  CHECK(valid(p, why) == 0);
    write_gz(p, l_good, AUTOSAVE_SIZE_V12, 6, Z_FIXED);             CHECK(valid(p, why) == 0);
    write_gz(p, l_good, AUTOSAVE_SIZE_V12, 0, Z_DEFAULT_STRATEGY);  CHECK(valid(p, why) == 0);
    write_gz(p, l_good, AUTOSAVE_SIZE_V12, 6, Z_HUFFMAN_ONLY);      CHECK(valid(p, why) == 0);
    write_gz(p, l_good, AUTOSAVE_SIZE_V12, 6, Z_RLE);               CHECK(valid(p, why) == 0);
    d = make_payload(0x00010900, AUTOSAVE_SIZE_V12, FILL_RANDOM);
    write_gz(p, d, AUTOSAVE_SIZE_V12, 6, Z_DEFAULT_STRATEGY);       CHECK(valid(p, why) == 0);
    free(d);
    d = make_payload(0x00010900, AUTOSAVE_SIZE_V12, FILL_TEXT);
    write_gz(p, d, AUTOSAVE_SIZE_V12, 6, Z_DEFAULT_STRATEGY);       CHECK(valid(p, why) == 0);
    free(d);

    d = make_payload(0x00010100, AUTOSAVE_SIZE_V11, FILL_ZERO_PATTERN);
    write_gz(p, d, AUTOSAVE_SIZE_V11, 6, Z_DEFAULT_STRATEGY);       CHECK(valid(p, why) == 0);
    write_gz(p, d, AUTOSAVE_SIZE_V11 - 4, 6, Z_DEFAULT_STRATEGY);   CHECK(valid(p, why) != 0 && STREQ(why, "wrong size"));
    free(d);
    d = make_payload(0x00010000, AUTOSAVE_SIZE_V10_MIN + 1024, FILL_ZERO_PATTERN);
    write_gz(p, d, AUTOSAVE_SIZE_V10_MIN + 1024, 6, Z_DEFAULT_STRATEGY);  CHECK(valid(p, why) == 0);
    write_gz(p, d, AUTOSAVE_SIZE_V10_MIN + 1, 6, Z_DEFAULT_STRATEGY);     CHECK(valid(p, why) != 0 && STREQ(why, "wrong size"));
    free(d);
    d = make_payload(0x00010100, AUTOSAVE_SIZE_V12, FILL_ZERO_PATTERN);                                  /* 1.1 with the 1.2+ size */
    write_gz(p, d, AUTOSAVE_SIZE_V12, 6, Z_DEFAULT_STRATEGY);       CHECK(valid(p, why) != 0 && STREQ(why, "wrong size"));
    free(d);

    d = malloc(AUTOSAVE_SIZE_V12);
    memcpy(d, l_good, AUTOSAVE_SIZE_V12);
    d[0] = 'X';
    write_gz(p, d, AUTOSAVE_SIZE_V12, 6, Z_DEFAULT_STRATEGY);       CHECK(valid(p, why) != 0 && STREQ(why, "bad magic"));
    d[0] = 'M';
    d[9] = 2;                                                                                           /* 0x00020900 */
    write_gz(p, d, AUTOSAVE_SIZE_V12, 6, Z_DEFAULT_STRATEGY);       CHECK(valid(p, why) != 0 && strncmp(why, "unsupported version", 19) == 0);
    free(d);

    write_gz(p, l_good, AUTOSAVE_SIZE_V12 - 1, 6, Z_DEFAULT_STRATEGY);  CHECK(valid(p, why) != 0 && STREQ(why, "wrong size"));
    d = make_payload(0x00010900, AUTOSAVE_SIZE_V12 + 100, FILL_ZERO_PATTERN);
    write_gz(p, d, AUTOSAVE_SIZE_V12 + 100, 6, Z_DEFAULT_STRATEGY);     CHECK(valid(p, why) != 0 && STREQ(why, "too long"));
    free(d);
    write_gz(p, l_good, 30, 6, Z_DEFAULT_STRATEGY);                     CHECK(valid(p, why) != 0 && STREQ(why, "too short"));

    {
        long long sz = fsize(l_goodgz);
        unsigned char *raw = malloc((size_t) sz);
        f = fopen(l_goodgz, "rb");
        CHECK(fread(raw, 1, (size_t) sz, f) == (size_t) sz);
        fclose(f);
        write_raw(p, raw, (size_t) sz / 2);        CHECK(valid(p, why) != 0 && STREQ(why, "truncated"));
        write_raw(p, raw, (size_t) sz - 1);        CHECK(valid(p, why) != 0 && STREQ(why, "truncated"));
        write_raw(p, raw, (size_t) sz - 8);        CHECK(valid(p, why) != 0 && STREQ(why, "truncated"));
        write_raw(p, raw, 10);                     CHECK(valid(p, why) != 0 && STREQ(why, "truncated"));
        raw[sz / 2] ^= 0x10;
        write_raw(p, raw, (size_t) sz);            CHECK(valid(p, why) != 0);
        raw[sz / 2] ^= 0x10;
        raw[sz - 6] ^= 0x01;
        write_raw(p, raw, (size_t) sz);            CHECK(valid(p, why) != 0 && STREQ(why, "bad crc"));
        raw[sz - 6] ^= 0x01;
        raw[sz - 2] ^= 0x01;
        write_raw(p, raw, (size_t) sz);            CHECK(valid(p, why) != 0 && STREQ(why, "bad length"));
        raw[sz - 2] ^= 0x01;
        write_raw(p, raw, (size_t) sz);            CHECK(valid(p, why) == 0);
        free(raw);
    }

    write_raw(p, "hello, this is not a state", 26);   CHECK(valid(p, why) != 0 && STREQ(why, "not gzip"));
    write_raw(p, "", 0);                              CHECK(valid(p, why) != 0 && STREQ(why, "truncated"));
    P(p, "nonexistent");                              CHECK(valid(p, why) != 0);
    CHECK(valid(l_dir, why) != 0);
}

static void content(const char *path, char *out, size_t n)
{
    FILE *f = fopen(path, "rb");
    size_t r;
    out[0] = 0;
    if (f == NULL)
        return;
    r = fread(out, 1, n - 1, f);
    out[r] = 0;
    fclose(f);
}

static int has(const char *path, const char *text)
{
    char c[64];
    content(path, c, sizeof(c));
    return STREQ(c, text);
}

static void clean_dir(void)
{
    char cmd[1100];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'/auto* '%s'/other* '%s'/x*", l_dir, l_dir, l_dir);
    if (system(cmd) != 0) { }
}

static void rotation_tests(void)
{
    char a[600], b1[600], b2[600], b3[600], b4[600];

    printf("rotation\n");
    clean_dir();
    P(a, "auto.st");
    autosave_backup_path(a, 0, b1, sizeof(b1));
    autosave_backup_path(a, 1, b2, sizeof(b2));
    autosave_backup_path(a, 2, b3, sizeof(b3));
    CHECK(STREQ(b1 + strlen(a), ".prev") && STREQ(b2 + strlen(a), ".prev2") && STREQ(b3 + strlen(a), ".prev3"));

    CHECK(autosave_rotate(a) == 0);
    write_raw(a, "one", 3);
    CHECK(autosave_rotate(a) == 1);
    CHECK(!exists(a) && has(b1, "one"));
    write_raw(a, "two", 3);
    CHECK(autosave_rotate(a) == 1);
    CHECK(has(b1, "one") && has(b2, "two") && !exists(a));
    write_raw(a, "three", 5);
    CHECK(autosave_rotate(a) == 1);
    CHECK(has(b1, "one") && has(b2, "two") && has(b3, "three"));
    set_age(b1, 300); set_age(b2, 100); set_age(b3, 200);
    write_raw(a, "four", 4);
    CHECK(autosave_rotate(a) == 1);
    CHECK(!exists(a) && has(b1, "four") && has(b2, "two") && has(b3, "three"));

    set_age(b1, 400); set_age(b2, 50); set_age(b3, 150);
    CHECK(autosave_unrotate(a) == 1);
    CHECK(has(a, "two") && !exists(b2) && exists(b1) && exists(b3));
    CHECK(autosave_unrotate(a) == 0);
    CHECK(has(a, "two"));

    P(b4, "auto.st.bak"); write_raw(b4, "keep", 4);
    CHECK(autosave_remove_all(a) == 0);
    CHECK(!exists(a) && !exists(b1) && !exists(b2) && !exists(b3));
    CHECK(exists(b4));
    CHECK(autosave_remove_all(a) == 0);

    clean_dir();
    CHECK(mkdir(a, 0755) == 0);
    CHECK(autosave_rotate(a) == 0 && exists(a));
    rmdir(a);
}

static void reconcile_tests(void)
{
    char a[600], b1[600], b2[600], b3[600], o[600], ob[600], other[600], gz2[600];
    autosave_report r;
    char why[64];
    unsigned char *d;

    printf("reconcile\n");
    clean_dir();
    P(a, "auto.st");
    autosave_backup_path(a, 0, b1, sizeof(b1));
    autosave_backup_path(a, 1, b2, sizeof(b2));
    autosave_backup_path(a, 2, b3, sizeof(b3));
    P(o, "other.auto.st");
    autosave_backup_path(o, 0, ob, sizeof(ob));
    P(other, "x.st0");
    P(gz2, "good2.gz");
    d = make_payload(0x00010900, AUTOSAVE_SIZE_V12, FILL_TEXT);
    write_gz(gz2, d, AUTOSAVE_SIZE_V12, 1, Z_DEFAULT_STRATEGY);
    free(d);

    autosave_reconcile(a, &r);
    CHECK(r.status == AUTOSAVE_ABSENT);

    copy_file(l_goodgz, a);
    write_raw(b1, "junk", 4); write_raw(b2, "junk", 4);
    copy_file(l_goodgz, o); copy_file(l_goodgz, ob); write_raw(other, "slot", 4);
    autosave_reconcile(a, &r);
    CHECK(r.status == AUTOSAVE_GOOD && r.removed == 2 && r.backups == 2);
    CHECK(exists(a) && !exists(b1) && !exists(b2));
    CHECK(exists(o) && exists(ob) && exists(other));
    CHECK(valid(a, why) == 0);

    autosave_reconcile(a, &r);
    CHECK(r.status == AUTOSAVE_GOOD && r.removed == 0);

    {
        long long sz = fsize(l_goodgz);
        unsigned char *raw = malloc((size_t) sz);
        FILE *f = fopen(l_goodgz, "rb");
        CHECK(fread(raw, 1, (size_t) sz, f) == (size_t) sz);
        fclose(f);
        write_raw(a, raw, (size_t) sz / 2);
        free(raw);
    }
    copy_file(l_goodgz, b1);
    autosave_reconcile(a, &r);
    CHECK(r.status == AUTOSAVE_RESTORED && !r.was_missing && STREQ(r.why, "truncated") && STREQ(r.from, b1));
    CHECK(valid(a, why) == 0 && fsize(a) == fsize(l_goodgz) && !exists(b1));
    CHECK(exists(o) && exists(ob));

    unlink(a);
    copy_file(l_goodgz, b2);
    autosave_reconcile(a, &r);
    CHECK(r.status == AUTOSAVE_RESTORED && r.was_missing && valid(a, why) == 0 && !exists(b2));

    write_raw(a, "M64+SAVE not really", 19);
    copy_file(l_goodgz, b1);
    autosave_reconcile(a, &r);
    CHECK(r.status == AUTOSAVE_RESTORED && valid(a, why) == 0);

    unlink(a);
    write_raw(b1, "garbage", 7);
    copy_file(gz2, b2);
    copy_file(l_goodgz, b3);
    set_age(b1, 10); set_age(b2, 100); set_age(b3, 500);
    autosave_reconcile(a, &r);
    CHECK(r.status == AUTOSAVE_RESTORED && r.was_missing && STREQ(r.from, b2));
    CHECK(fsize(a) == fsize(gz2) && !exists(b1) && !exists(b2) && !exists(b3));

    write_raw(a, "damaged", 7);
    write_raw(b1, "junk", 4);
    autosave_reconcile(a, &r);
    CHECK(r.status == AUTOSAVE_BAD && !r.was_missing);
    CHECK(has(a, "damaged") && has(b1, "junk"));
    unlink(a);
    autosave_reconcile(a, &r);
    CHECK(r.status == AUTOSAVE_BAD && r.was_missing && has(b1, "junk"));
    unlink(b1);

    CHECK(mkdir(a, 0755) == 0);
    copy_file(l_goodgz, b1);
    autosave_reconcile(a, &r);
    CHECK(r.status == AUTOSAVE_ABSENT && exists(b1));
    rmdir(a);
    clean_dir();

    copy_file(l_goodgz, a);
    CHECK(autosave_rotate(a) == 1);
    copy_file(gz2, a);
    autosave_reconcile(a, &r);
    CHECK(r.status == AUTOSAVE_GOOD && r.removed == 1 && fsize(a) == fsize(gz2) && !exists(b1));
    CHECK(autosave_rotate(a) == 1);
    write_raw(a, "cut off by a power loss", 23);
    autosave_reconcile(a, &r);
    CHECK(r.status == AUTOSAVE_RESTORED && fsize(a) == fsize(gz2) && valid(a, why) == 0);

    {
        char out[64];
        static const char cl1[] = "mupen64plus\0--gfx\0x.so\0--savestate\0/a b/c.auto.st\0rom.z64";
        static const char cl2[] = "mupen64plus\0--savestate=/p/q.st\0rom.z64";
        static const char cl3[] = "mupen64plus\0--savestates\0x\0--savestate";
        static const char cl4[] = "mupen64plus\0rom.z64";
        CHECK(autosave_cmdline_arg(cl1, sizeof(cl1) - 1, "--savestate", out, sizeof(out)) && STREQ(out, "/a b/c.auto.st"));
        CHECK(autosave_cmdline_arg(cl2, sizeof(cl2) - 1, "--savestate", out, sizeof(out)) && STREQ(out, "/p/q.st"));
        CHECK(!autosave_cmdline_arg(cl3, sizeof(cl3) - 1, "--savestate", out, sizeof(out)));
        CHECK(!autosave_cmdline_arg(cl4, sizeof(cl4) - 1, "--savestate", out, sizeof(out)));
    }
}

static void msleep(int ms) { usleep((useconds_t) ms * 1000); }

static int wait_hit(filewatch *w, int ms)
{
    int t;
    for (t = 0; t <= ms; t += 5)
    {
        if (filewatch_poll(w))
            return 1;
        msleep(5);
    }
    return 0;
}

/* truncate-open, write, linger, write, close (like gzopen "wb" ... gzclose) */
static pid_t writer(const char *path, int linger_ms, int do_write)
{
    pid_t pid = fork();
    if (pid == 0)
    {
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) _exit(1);
        if (do_write && write(fd, "abcd", 4) != 4) _exit(1);
        usleep((useconds_t) linger_ms * 1000);
        if (do_write && write(fd, "efgh", 4) != 4) _exit(1);
        close(fd);
        _exit(0);
    }
    return pid;
}

static void watch_tests(void)
{
    char a[600], o[600], t[600];
    filewatch w;
    int st;
    pid_t pid;

    printf("filewatch\n");
    clean_dir();
    P(a, "auto.st");
    P(o, "other.st");

    CHECK(filewatch_start(&w, a, 0) == 0);
    CHECK(filewatch_uses_inotify(&w));
    CHECK(!wait_hit(&w, 100));

    pid = writer(a, 400, 1);
    CHECK(!wait_hit(&w, 250));
    CHECK(wait_hit(&w, 1000));
    waitpid(pid, &st, 0);
    CHECK(fsize(a) == 8);
    CHECK(!wait_hit(&w, 100));

    /* rewritten with the same size and (to the second) the same time: still seen (the polling fallback can't) */
    filewatch_stop(&w);
    write_raw(a, "abcdefgh", 8);
    CHECK(filewatch_start(&w, a, 0) == 0);
    pid = writer(a, 20, 1);
    CHECK(wait_hit(&w, 1000));
    waitpid(pid, &st, 0);

    CHECK(!wait_hit(&w, 20));
    pid = writer(o, 20, 1);
    CHECK(!wait_hit(&w, 300));
    waitpid(pid, &st, 0);

    P(t, "x.tmp");
    write_raw(t, "moved", 5);
    CHECK(!wait_hit(&w, 50));
    CHECK(rename(t, a) == 0);
    CHECK(wait_hit(&w, 500));

    pid = writer(a, 10, 0);
    CHECK(wait_hit(&w, 500));
    waitpid(pid, &st, 0);
    CHECK(fsize(a) == 0);
    filewatch_stop(&w);

    /* the forced-failure case: the writer's open fails, nothing is ever reported */
    unlink(a);
    CHECK(mkdir(a, 0755) == 0);
    CHECK(filewatch_start(&w, a, 0) == 0);
    pid = writer(a, 10, 1);
    waitpid(pid, &st, 0);
    CHECK(WEXITSTATUS(st) == 1);
    CHECK(!wait_hit(&w, 400));
    filewatch_stop(&w);
    rmdir(a);

    snprintf(t, sizeof(t), "%s/nodir/auto.st", l_dir);
    CHECK(filewatch_start(&w, t, 0) == 0 && !filewatch_uses_inotify(&w));
    CHECK(!wait_hit(&w, 50));
    filewatch_stop(&w);

    P(a, "auto.st");
    unlink(a);
    CHECK(filewatch_start(&w, a, 1) == 0 && !filewatch_uses_inotify(&w));
    CHECK(!wait_hit(&w, 50));
    pid = writer(a, 500, 1);                                  /* created at once, grows at 500 ms, closed after that */
    {
        int early = 0, t0;
        /* while the size is still changing it must not report within the first 250 ms */
        for (t0 = 0; t0 < 250; t0 += 10) { early |= filewatch_poll(&w); msleep(10); }
        CHECK(!early);
    }
    CHECK(wait_hit(&w, 2000));
    waitpid(pid, &st, 0);
    CHECK(fsize(a) == 8);
    filewatch_stop(&w);
    CHECK(filewatch_start(&w, a, 1) == 0);
    CHECK(!wait_hit(&w, 500));
    filewatch_stop(&w);
}

int main(void)
{
    const char *tmp = getenv("TMPDIR");
    snprintf(l_dir, sizeof(l_dir), "%s/suxty-test-auto-XXXXXX", tmp != NULL && tmp[0] ? tmp : "/tmp");
    if (mkdtemp(l_dir) == NULL)
    {
        perror("mkdtemp");
        return 2;
    }

    validate_tests();
    rotation_tests();
    reconcile_tests();
    watch_tests();

    {
        char cmd[400];
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", l_dir);
        if (system(cmd) != 0) { }
    }
    free(l_good);
    printf("%d checks, %d failed\n", l_checks, l_fail);
    return l_fail != 0;
}
