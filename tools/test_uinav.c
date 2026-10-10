/* Unit tests for src/uinav.c; run with make -C projects/unix check. GPLv2+, like the rest of the project. */
#include <stdio.h>

#include "uinav.h"

static int l_fail, l_checks;

#define CHECK(cond) do { l_checks++; if (!(cond)) { l_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const ui_navcell l_main[7] = {
    { 0, 0, 1 }, { 0, 1, 1 },
    { 1, 0, 2 },
    { 2, 0, 1 }, { 2, 1, 1 },
    { 3, 0, 1 }, { 3, 1, 1 }
};

static int mv(const ui_navcell *c, int n, int cur, int dir, int *want)
{
    return uinav_move(c, n, cur, dir, want);
}

static void test_main_grid(void)
{
    int want = 0;

    CHECK(mv(l_main, 7, 0, UI_NAV_DOWN, &want) == 2);
    CHECK(mv(l_main, 7, 2, UI_NAV_DOWN, &want) == 3);
    CHECK(mv(l_main, 7, 3, UI_NAV_DOWN, &want) == 5);
    CHECK(mv(l_main, 7, 5, UI_NAV_DOWN, &want) == 0);
    CHECK(mv(l_main, 7, 0, UI_NAV_UP, &want) == 5);
    CHECK(mv(l_main, 7, 5, UI_NAV_UP, &want) == 3);
    CHECK(mv(l_main, 7, 3, UI_NAV_UP, &want) == 2);
    want = 0;
    CHECK(mv(l_main, 7, 1, UI_NAV_RIGHT, &want) == 0 && want == 0);
    CHECK(mv(l_main, 7, 0, UI_NAV_RIGHT, &want) == 1 && want == 1);
    CHECK(mv(l_main, 7, 1, UI_NAV_DOWN, &want) == 2 && want == 1);
    CHECK(mv(l_main, 7, 2, UI_NAV_DOWN, &want) == 4);
    CHECK(mv(l_main, 7, 4, UI_NAV_DOWN, &want) == 6);
    CHECK(mv(l_main, 7, 6, UI_NAV_UP, &want) == 4);
    CHECK(mv(l_main, 7, 4, UI_NAV_UP, &want) == 2);
    CHECK(mv(l_main, 7, 2, UI_NAV_UP, &want) == 1);
    CHECK(mv(l_main, 7, 3, UI_NAV_RIGHT, &want) == 4);
    CHECK(mv(l_main, 7, 4, UI_NAV_RIGHT, &want) == 3);
    CHECK(mv(l_main, 7, 3, UI_NAV_LEFT, &want) == 4);
    CHECK(mv(l_main, 7, 5, UI_NAV_RIGHT, &want) == 6);
    CHECK(mv(l_main, 7, 2, UI_NAV_LEFT, &want) == 2 && mv(l_main, 7, 2, UI_NAV_RIGHT, &want) == 2);
    want = 0;
    mv(l_main, 7, 3, UI_NAV_RIGHT, &want);
    CHECK(want == 1);
    CHECK(mv(l_main, 7, 4, UI_NAV_UP, &want) == 2 && mv(l_main, 7, 2, UI_NAV_UP, &want) == 1);
}

static void test_list_and_triplet(void)
{
    static const ui_navcell list[4] = { { 0, 0, 1 }, { 1, 0, 1 }, { 2, 0, 1 }, { 3, 0, 1 } };
    static const ui_navcell st[4] = { { 0, 0, 3 }, { 1, 0, 1 }, { 1, 1, 1 }, { 1, 2, 1 } };
    int want = 0;

    CHECK(mv(list, 4, 0, UI_NAV_DOWN, &want) == 1 && mv(list, 4, 3, UI_NAV_DOWN, &want) == 0 && mv(list, 4, 0, UI_NAV_UP, &want) == 3);
    CHECK(mv(list, 4, 2, UI_NAV_LEFT, &want) == 2 && mv(list, 4, 2, UI_NAV_RIGHT, &want) == 2);

    want = 0;
    CHECK(mv(st, 4, 0, UI_NAV_DOWN, &want) == 1);
    CHECK(mv(st, 4, 1, UI_NAV_RIGHT, &want) == 2 && mv(st, 4, 2, UI_NAV_RIGHT, &want) == 3);
    CHECK(mv(st, 4, 3, UI_NAV_RIGHT, &want) == 1);
    CHECK(mv(st, 4, 1, UI_NAV_LEFT, &want) == 3);
    CHECK(want == 2);
    CHECK(mv(st, 4, 3, UI_NAV_UP, &want) == 0);
    CHECK(mv(st, 4, 0, UI_NAV_DOWN, &want) == 3);
    CHECK(mv(st, 4, 3, UI_NAV_DOWN, &want) == 0);
}

static void test_edges(void)
{
    static const ui_navcell one[1] = { { 0, 0, 1 } };
    static const ui_navcell scroll[3] = { { 5, 0, 1 }, { 6, 0, 1 }, { 9, 0, 1 } };      /* rows need not be consecutive (labels between) */
    int want = 0, d;

    CHECK(mv(one, 1, 0, UI_NAV_DOWN, &want) == 0 && mv(one, 1, 0, UI_NAV_UP, &want) == 0);
    CHECK(mv(one, 0, 0, UI_NAV_DOWN, &want) == 0);
    CHECK(mv(one, 1, 7, UI_NAV_DOWN, &want) == 0);                   /* a stale focus is pulled back in range */
    CHECK(mv(scroll, 3, 0, UI_NAV_DOWN, &want) == 1 && mv(scroll, 3, 1, UI_NAV_DOWN, &want) == 2 && mv(scroll, 3, 2, UI_NAV_DOWN, &want) == 0);
    CHECK(mv(scroll, 3, 0, UI_NAV_UP, &want) == 2);
    for (d = UI_NAV_UP; d <= UI_NAV_RIGHT; d++)                      /* NULL want_col is fine */
        CHECK(mv(scroll, 3, 1, d, NULL) >= 0);
}

int main(void)
{
    test_main_grid();
    test_list_and_triplet();
    test_edges();
    printf("%d checks, %d failed\n", l_checks, l_fail);
    return l_fail ? 1 : 0;
}
