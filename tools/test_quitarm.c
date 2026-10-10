/* Unit tests for src/quitarm.c; run with make -C projects/unix check. GPLv2+, like the rest of the project. */
#include <stdio.h>

#include "quitarm.h"

static int l_fail, l_checks;

#define CHECK(cond) do { l_checks++; if (!(cond)) { l_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main(void)
{
    quitarm_t q;

    quitarm_reset(&q);
    CHECK(quitarm_press(&q, 0, 1000) == QUITARM_QUIT);
    CHECK(quitarm_press(&q, 0, 1001) == QUITARM_QUIT);

    quitarm_reset(&q);
    CHECK(quitarm_press(&q, 1, 5000) == QUITARM_ARMED);
    CHECK(quitarm_press(&q, 1, 5800) == QUITARM_QUIT);
    CHECK(quitarm_press(&q, 1, 5900) == QUITARM_ARMED);

    /* the window: 2 s, edges included */
    quitarm_reset(&q);
    CHECK(quitarm_press(&q, 1, 10000) == QUITARM_ARMED);
    CHECK(quitarm_press(&q, 1, 10000 + QUITARM_WINDOW_MS) == QUITARM_QUIT);
    CHECK(quitarm_press(&q, 1, 20000) == QUITARM_ARMED);
    CHECK(quitarm_press(&q, 1, 20000 + QUITARM_WINDOW_MS + 1) == QUITARM_ARMED);
    CHECK(quitarm_press(&q, 1, 20000 + QUITARM_WINDOW_MS + 500) == QUITARM_QUIT);

    quitarm_reset(&q);
    CHECK(quitarm_press(&q, 1, 30000) == QUITARM_ARMED);
    quitarm_reset(&q);
    CHECK(quitarm_press(&q, 1, 30100) == QUITARM_ARMED);

    quitarm_reset(&q);
    quitarm_press(&q, 1, 40000);
    CHECK(quitarm_press(&q, 0, 45000) == QUITARM_QUIT);

    /* the tick counter wraps (about every 49 days) */
    quitarm_reset(&q);
    CHECK(quitarm_press(&q, 1, 0xfffffe00u) == QUITARM_ARMED);
    CHECK(quitarm_press(&q, 1, 0x00000100u) == QUITARM_QUIT);                              /* 0x300 ms later */

    quitarm_reset(&q);
    CHECK(quitarm_press(&q, 1, 0) == QUITARM_ARMED);
    CHECK(quitarm_press(&q, 1, 500) == QUITARM_QUIT);

    printf("%d checks, %d failed\n", l_checks, l_fail);
    return l_fail ? 1 : 0;
}
