/* Unit tests for src/quitarm.c (the double-press logic of "Confirm Quit"): off = every press quits; on = the first press arms,
 * a second press within the window quits, a late press arms again; the clock may wrap.
 *
 *   make -C projects/unix check          GPLv2+, like the rest of the project.
 */
#include <stdio.h>

#include "quitarm.h"

static int l_fail, l_checks;

#define CHECK(cond) do { l_checks++; if (!(cond)) { l_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main(void)
{
    quitarm_t q;

    /* Confirm Quit off: instant */
    quitarm_reset(&q);
    CHECK(quitarm_press(&q, 0, 1000) == QUITARM_QUIT);
    CHECK(quitarm_press(&q, 0, 1001) == QUITARM_QUIT);

    /* on: the first press only arms, the second one quits */
    quitarm_reset(&q);
    CHECK(quitarm_press(&q, 1, 5000) == QUITARM_ARMED);
    CHECK(quitarm_press(&q, 1, 5800) == QUITARM_QUIT);
    CHECK(quitarm_press(&q, 1, 5900) == QUITARM_ARMED);              /* quitting disarms: it takes two presses again */

    /* the window: 2 s, edges included */
    quitarm_reset(&q);
    CHECK(quitarm_press(&q, 1, 10000) == QUITARM_ARMED);
    CHECK(quitarm_press(&q, 1, 10000 + QUITARM_WINDOW_MS) == QUITARM_QUIT);
    CHECK(quitarm_press(&q, 1, 20000) == QUITARM_ARMED);
    CHECK(quitarm_press(&q, 1, 20000 + QUITARM_WINDOW_MS + 1) == QUITARM_ARMED);          /* too late: this press arms again ... */
    CHECK(quitarm_press(&q, 1, 20000 + QUITARM_WINDOW_MS + 500) == QUITARM_QUIT);         /* ... and is the first of a new pair */

    /* a reset (the menu was opened) forgets the first press */
    quitarm_reset(&q);
    CHECK(quitarm_press(&q, 1, 30000) == QUITARM_ARMED);
    quitarm_reset(&q);
    CHECK(quitarm_press(&q, 1, 30100) == QUITARM_ARMED);

    /* turning Confirm Quit off while armed: the next press quits at once */
    quitarm_reset(&q);
    quitarm_press(&q, 1, 40000);
    CHECK(quitarm_press(&q, 0, 45000) == QUITARM_QUIT);

    /* the tick counter wraps (about every 49 days) */
    quitarm_reset(&q);
    CHECK(quitarm_press(&q, 1, 0xfffffe00u) == QUITARM_ARMED);
    CHECK(quitarm_press(&q, 1, 0x00000100u) == QUITARM_QUIT);                              /* 0x300 ms later */

    /* the very first tick (0) is a time like any other */
    quitarm_reset(&q);
    CHECK(quitarm_press(&q, 1, 0) == QUITARM_ARMED);
    CHECK(quitarm_press(&q, 1, 500) == QUITARM_QUIT);

    printf("%d checks, %d failed\n", l_checks, l_fail);
    return l_fail ? 1 : 0;
}
