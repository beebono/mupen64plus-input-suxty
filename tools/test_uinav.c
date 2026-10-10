/* Unit tests for src/uinav.c (D-pad navigation over a grid of focusable cells): the two-column main menu with its full-width
 * "In Slot" row, a one-column list, the Save States page (a full-width carousel above a row of three), wrapping, the remembered
 * column, and degenerate inputs.
 *
 *   make -C projects/unix check          GPLv2+, like the rest of the project.
 */
#include <stdio.h>

#include "uinav.h"

static int l_fail, l_checks;

#define CHECK(cond) do { l_checks++; if (!(cond)) { l_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* the main menu: 0 Save, 1 Load | 2 In Slot (full width) | 3 Resume, 4 Reset | 5 Settings, 6 Quit */
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

    /* down / up through the columns */
    CHECK(mv(l_main, 7, 0, UI_NAV_DOWN, &want) == 2);              /* Save -> In Slot */
    CHECK(mv(l_main, 7, 2, UI_NAV_DOWN, &want) == 3);              /* In Slot -> Resume (the column it came from: left) */
    CHECK(mv(l_main, 7, 3, UI_NAV_DOWN, &want) == 5);              /* Resume -> Settings */
    CHECK(mv(l_main, 7, 5, UI_NAV_DOWN, &want) == 0);              /* wraps to the top row, left column */
    CHECK(mv(l_main, 7, 0, UI_NAV_UP, &want) == 5);                /* and back to the bottom */
    CHECK(mv(l_main, 7, 5, UI_NAV_UP, &want) == 3);
    CHECK(mv(l_main, 7, 3, UI_NAV_UP, &want) == 2);
    /* the right column: Load -> In Slot -> Reset (the full-width row remembers where you came from) */
    want = 0;
    CHECK(mv(l_main, 7, 1, UI_NAV_RIGHT, &want) == 0 && want == 0);   /* Load, right -> wraps to Save */
    CHECK(mv(l_main, 7, 0, UI_NAV_RIGHT, &want) == 1 && want == 1);
    CHECK(mv(l_main, 7, 1, UI_NAV_DOWN, &want) == 2 && want == 1);   /* a full-width cell keeps the column */
    CHECK(mv(l_main, 7, 2, UI_NAV_DOWN, &want) == 4);                /* Reset, not Resume */
    CHECK(mv(l_main, 7, 4, UI_NAV_DOWN, &want) == 6);                /* Quit */
    CHECK(mv(l_main, 7, 6, UI_NAV_UP, &want) == 4);
    CHECK(mv(l_main, 7, 4, UI_NAV_UP, &want) == 2);
    CHECK(mv(l_main, 7, 2, UI_NAV_UP, &want) == 1);                  /* Load again */
    /* left / right inside a row, wrapping; the full-width row does not move (its widget uses Left / Right) */
    CHECK(mv(l_main, 7, 3, UI_NAV_RIGHT, &want) == 4);
    CHECK(mv(l_main, 7, 4, UI_NAV_RIGHT, &want) == 3);
    CHECK(mv(l_main, 7, 3, UI_NAV_LEFT, &want) == 4);
    CHECK(mv(l_main, 7, 5, UI_NAV_RIGHT, &want) == 6);
    CHECK(mv(l_main, 7, 2, UI_NAV_LEFT, &want) == 2 && mv(l_main, 7, 2, UI_NAV_RIGHT, &want) == 2);
    /* the remembered column follows the cell you move to sideways */
    want = 0;
    mv(l_main, 7, 3, UI_NAV_RIGHT, &want);
    CHECK(want == 1);
    CHECK(mv(l_main, 7, 4, UI_NAV_UP, &want) == 2 && mv(l_main, 7, 2, UI_NAV_UP, &want) == 1);
}

static void test_list_and_triplet(void)
{
    /* a plain list: every cell is its own full-width row; Left / Right do not move */
    static const ui_navcell list[4] = { { 0, 0, 1 }, { 1, 0, 1 }, { 2, 0, 1 }, { 3, 0, 1 } };
    /* Save States: the carousel (full width of 3 columns), then Save | Load | Delete */
    static const ui_navcell st[4] = { { 0, 0, 3 }, { 1, 0, 1 }, { 1, 1, 1 }, { 1, 2, 1 } };
    int want = 0;

    CHECK(mv(list, 4, 0, UI_NAV_DOWN, &want) == 1 && mv(list, 4, 3, UI_NAV_DOWN, &want) == 0 && mv(list, 4, 0, UI_NAV_UP, &want) == 3);
    CHECK(mv(list, 4, 2, UI_NAV_LEFT, &want) == 2 && mv(list, 4, 2, UI_NAV_RIGHT, &want) == 2);

    want = 0;
    CHECK(mv(st, 4, 0, UI_NAV_DOWN, &want) == 1);                    /* carousel -> Save */
    CHECK(mv(st, 4, 1, UI_NAV_RIGHT, &want) == 2 && mv(st, 4, 2, UI_NAV_RIGHT, &want) == 3);
    CHECK(mv(st, 4, 3, UI_NAV_RIGHT, &want) == 1);                   /* wraps */
    CHECK(mv(st, 4, 1, UI_NAV_LEFT, &want) == 3);
    CHECK(want == 2);
    CHECK(mv(st, 4, 3, UI_NAV_UP, &want) == 0);                      /* Delete -> carousel ... */
    CHECK(mv(st, 4, 0, UI_NAV_DOWN, &want) == 3);                    /* ... and back down to Delete, not Save */
    CHECK(mv(st, 4, 3, UI_NAV_DOWN, &want) == 0);                    /* the page wraps */
}

static void test_edges(void)
{
    static const ui_navcell one[1] = { { 0, 0, 1 } };
    static const ui_navcell scroll[3] = { { 5, 0, 1 }, { 6, 0, 1 }, { 9, 0, 1 } };      /* rows need not be consecutive (labels between) */
    int want = 0, d;

    CHECK(mv(one, 1, 0, UI_NAV_DOWN, &want) == 0 && mv(one, 1, 0, UI_NAV_UP, &want) == 0);
    CHECK(mv(one, 0, 0, UI_NAV_DOWN, &want) == 0);                   /* nothing to focus */
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
