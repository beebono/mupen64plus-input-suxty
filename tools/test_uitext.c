/* Unit tests for src/uitext.c; run with make -C projects/unix check. GPLv2+, like the rest of the project.
 * The text width is 12 pixels per character, like the hint face at 1x. */
#include <stdio.h>
#include <string.h>

#include "font.h"
#include "uitext.h"

static int l_fail, l_checks;

#define CHECK(cond) do { l_checks++; if (!(cond)) { l_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STR(got, want) do { l_checks++; if (strcmp((got), (want)) != 0) { l_fail++; printf("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (got), (want)); } } while (0)

static int mw(void *ctx, const char *s)
{
    (void) ctx;
    return 12 * (int) strlen(s);
}

#define W(chars) (12 * (chars))

static void test_fit(void)
{
    char b[64];
    uitext_fit(b, sizeof(b), "hello", W(5), mw, NULL);
    CHECK_STR(b, "hello");
    uitext_fit(b, sizeof(b), "hello", W(4), mw, NULL);
    CHECK_STR(b, "he..");
    uitext_fit(b, sizeof(b), "hello", 0, mw, NULL);          /* one character stays, whatever the room */
    CHECK_STR(b, "h..");
    uitext_fit(b, sizeof(b), "", W(3), mw, NULL);
    CHECK_STR(b, "");
}

static void test_wrap(void)
{
    char a[96], b[96];
    const char *msg = "L-Stick Left " FONT_CH_RIGHT " N64 Stick Left (was Stick Right)";
    int n, avail, i;
    static const char *const texts[] = {
        "South " FONT_CH_RIGHT " A", "South " FONT_CH_RIGHT " A (was B)", "L-Stick Left " FONT_CH_RIGHT " C-Face Shift (was Soft Tilt)",
        "Takes effect the next time the game starts", "Applied now; saved for all games", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "a b c d e f g h i j k l m n o p q r s t u v w x y z a b c d e f", "x", ""
    };

    n = uitext_wrap2("South " FONT_CH_RIGHT " A (was B)", W(40), mw, NULL, a, b, sizeof(a));
    CHECK(n == 1 && b[0] == 0);
    CHECK_STR(a, "South " FONT_CH_RIGHT " A (was B)");
    n = uitext_wrap2("", W(40), mw, NULL, a, b, sizeof(a));
    CHECK(n == 1 && a[0] == 0 && b[0] == 0);

    n = uitext_wrap2("12345 12345", W(11), mw, NULL, a, b, sizeof(a));
    CHECK(n == 1);
    n = uitext_wrap2("12345 12345", W(10), mw, NULL, a, b, sizeof(a));
    CHECK(n == 2);
    CHECK_STR(a, "12345");
    CHECK_STR(b, "12345");

    n = uitext_wrap2(msg, W(40), mw, NULL, a, b, sizeof(a));
    CHECK(n == 2);
    CHECK_STR(a, "L-Stick Left " FONT_CH_RIGHT " N64 Stick Left (was Stick");     /* 40 characters: the last word that fits */
    CHECK_STR(b, "Right)");
    n = uitext_wrap2(msg, W(39), mw, NULL, a, b, sizeof(a));
    CHECK(n == 2);
    CHECK_STR(a, "L-Stick Left " FONT_CH_RIGHT " N64 Stick Left (was");
    CHECK_STR(b, "Stick Right)");
    n = uitext_wrap2("one two three four", W(9), mw, NULL, a, b, sizeof(a));
    CHECK(n == 2);
    CHECK_STR(a, "one two");                  /* "one two " would be 8 + the space; the break is at the space after "two" */
    CHECK_STR(b, "three f..");                /* "three four" is 10 wide: the second line is cut, not wrapped again */

    n = uitext_wrap2("first line\nsecond line", W(40), mw, NULL, a, b, sizeof(a));
    CHECK(n == 2);
    CHECK_STR(a, "first line");
    CHECK_STR(b, "second line");
    n = uitext_wrap2("first line\nsecond line", W(8), mw, NULL, a, b, sizeof(a));
    CHECK(n == 2 && strcmp(a, "first ..") == 0 && strcmp(b, "second..") == 0);
    n = uitext_wrap2("only\n", W(40), mw, NULL, a, b, sizeof(a));
    CHECK(n == 1 && strcmp(a, "only") == 0);

    n = uitext_wrap2("abcdefghijklmnop", W(10), mw, NULL, a, b, sizeof(a));
    CHECK(n == 2);
    CHECK_STR(a, "abcdefghij");
    CHECK_STR(b, "klmnop");

    n = uitext_wrap2("abcdefghijklmnopqrstuvwxyz0123456789", W(10), mw, NULL, a, b, sizeof(a));
    CHECK(n == 2 && strcmp(a, "abcdefghij") == 0 && strcmp(b, "klmnopqr..") == 0);

    for (i = 0; i < (int) (sizeof(texts) / sizeof(texts[0])); i++)
        for (avail = W(3); avail <= W(70); avail += 12)
        {
            char joined[200];
            n = uitext_wrap2(texts[i], avail, mw, NULL, a, b, sizeof(a));
            CHECK(n == 1 || n == 2);
            CHECK(mw(NULL, a) <= avail && mw(NULL, b) <= avail);
            CHECK((n == 2) == (b[0] != 0));
            if (!strstr(b, "..") && n == 2)
            {
                snprintf(joined, sizeof(joined), "%s %s", a, b);
                if (strchr(texts[i], ' ') != NULL)
                    CHECK(strcmp(joined, texts[i]) == 0 || strlen(joined) == strlen(texts[i]) + 1);   /* hard break in a long word: no space */
            }
            if (mw(NULL, texts[i]) <= avail)
                CHECK(n == 1 && strcmp(a, texts[i]) == 0);
        }
}

static void test_arrows(void)
{
    char o[UITEXT_ARROWS_LEN + 1];
    int bits, up, down, left, right;

    uitext_arrows(0, 0, 0, 0, o);
    CHECK_STR(o, "       ");
    uitext_arrows(1, 1, 1, 1, o);
    CHECK_STR(o, UITEXT_ARROWS_ALL);
    CHECK_STR(o, FONT_CH_UP " " FONT_CH_DOWN " " FONT_CH_LEFT " " FONT_CH_RIGHT);
    for (bits = 0; bits < 16; bits++)
    {
        up = bits & 1; down = (bits >> 1) & 1; left = (bits >> 2) & 1; right = (bits >> 3) & 1;
        uitext_arrows(up, down, left, right, o);
        CHECK(strlen(o) == UITEXT_ARROWS_LEN);                       /* never wider, whatever is pressed: nothing shifts */
        CHECK(o[1] == ' ' && o[3] == ' ' && o[5] == ' ');
        CHECK(o[0] == (up ? FONT_CH_UP[0] : ' ') && o[2] == (down ? FONT_CH_DOWN[0] : ' '));
        CHECK(o[4] == (left ? FONT_CH_LEFT[0] : ' ') && o[6] == (right ? FONT_CH_RIGHT[0] : ' '));
    }
    CHECK(strlen(UITEXT_ARROWS_ALL) == UITEXT_ARROWS_LEN);
}

static const uitext_btn l_tab[] = {
    { 1u << 0, "A" }, { 1u << 1, "B" }, { 1u << 2, "Z" }, { 1u << 3, "St" }, { 1u << 4, "L" }, { 1u << 5, "R" },
};
#define NTAB ((int) (sizeof(l_tab) / sizeof(l_tab[0])))

static void test_btn_cells(void)
{
    char g[40], o[40];
    int n, i, k;
    unsigned mask;

    n = uitext_btn_cells(l_tab, NTAB, 0, g, o, sizeof(g));
    CHECK(n == 0);
    CHECK_STR(g, "A B Z St L R");
    CHECK_STR(o, "            ");
    n = uitext_btn_cells(l_tab, NTAB, 1u << 2, g, o, sizeof(g));
    CHECK(n == 1);
    CHECK_STR(g, "A B Z St L R");
    CHECK_STR(o, "    Z       ");
    n = uitext_btn_cells(l_tab, NTAB, 1u << 3, g, o, sizeof(g));
    CHECK(n == 1);
    CHECK_STR(o, "      St    ");
    n = uitext_btn_cells(l_tab, NTAB, (1u << 5) | (1u << 3) | (1u << 0), g, o, sizeof(g));
    CHECK(n == 3);
    CHECK_STR(o, "A     St   R");
    n = uitext_btn_cells(l_tab, NTAB, 0x3f, g, o, sizeof(g));
    CHECK(n == 6);
    CHECK_STR(o, "A B Z St L R");
    n = uitext_btn_cells(l_tab, NTAB, 0xffffffc0u, g, o, sizeof(g));          /* bits outside the table are ignored */
    CHECK(n == 0);
    CHECK_STR(o, "            ");

    for (mask = 0; mask < 64; mask++)
    {
        n = uitext_btn_cells(l_tab, NTAB, mask, g, o, sizeof(g));
        CHECK(strlen(g) == strlen(o));
        CHECK_STR(g, "A B Z St L R");
        CHECK(n == __builtin_popcount(mask));
        for (i = 0; i < (int) strlen(g); i++)
            CHECK(o[i] == ' ' || o[i] == g[i]);
        for (k = 0, i = 0; k < NTAB; k++)
        {
            size_t len = strlen(l_tab[k].name);
            int lit = (int) ((mask >> k) & 1);
            CHECK(strncmp(g + i, l_tab[k].name, len) == 0);
            CHECK(lit == (strncmp(o + i, l_tab[k].name, len) == 0));
            i += (int) len + 1;
        }
    }

    /* a small buffer is cut at a whole name, not overrun */
    n = uitext_btn_cells(l_tab, NTAB, 0x3f, g, o, 8);
    CHECK(strlen(g) < 8 && strlen(o) == strlen(g));
    CHECK(n <= 6);
}

static void test_remap_msg(void)
{
    char o[96];
    uitext_remap_msg(o, sizeof(o), "South", "A", NULL);
    CHECK_STR(o, "South " FONT_CH_RIGHT " A");
    uitext_remap_msg(o, sizeof(o), "South", "A", "");
    CHECK_STR(o, "South " FONT_CH_RIGHT " A");
    uitext_remap_msg(o, sizeof(o), "South", "A", "B");
    CHECK_STR(o, "South " FONT_CH_RIGHT " A (was B)");
    uitext_remap_msg(o, sizeof(o), "L-Stick Left", "C-Face Shift", "Soft Tilt");
    CHECK_STR(o, "L-Stick Left " FONT_CH_RIGHT " C-Face Shift (was Soft Tilt)");
    uitext_remap_msg(o, 8, "South", "A", "B");
    CHECK(strlen(o) == 7);
}

int main(void)
{
    test_fit();
    test_wrap();
    test_arrows();
    test_btn_cells();
    test_remap_msg();
    printf("%d checks, %d failed\n", l_checks, l_fail);
    return l_fail ? 1 : 0;
}
