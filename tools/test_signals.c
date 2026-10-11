/* Unit tests for src/signals.c; run with make -C projects/unix check. GPLv2+, like the rest of the project. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "signals.h"

static int l_fail, l_checks;

#define CHECK(cond) do { l_checks++; if (!(cond)) { l_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static sigs_action poll(int menu, int quitting, int busy)
{
    sigs_state st = { menu, quitting, busy };
    return sigs_poll(&st);
}

static void *disposition(int sig)
{
    struct sigaction sa;
    sigaction(sig, NULL, &sa);
    return (void *) sa.sa_handler;
}

int main(void)
{
    /* M64P_AUTOLOAD: only a value starting with '1' counts, and it is removed so the core does not act on it */
    unsetenv("M64P_AUTOLOAD");
    CHECK(sigs_autoload_claim() == 0);
    setenv("M64P_AUTOLOAD", "0", 1);
    CHECK(sigs_autoload_claim() == 0 && getenv("M64P_AUTOLOAD") != NULL);
    setenv("M64P_AUTOLOAD", "", 1);
    CHECK(sigs_autoload_claim() == 0 && getenv("M64P_AUTOLOAD") != NULL);
    setenv("M64P_AUTOLOAD", "1", 1);
    CHECK(sigs_autoload_claim() == 1 && getenv("M64P_AUTOLOAD") == NULL);
    CHECK(sigs_autoload_claim() == 0);
    setenv("M64P_AUTOLOAD", "1abc", 1);
    CHECK(sigs_autoload_claim() == 1 && getenv("M64P_AUTOLOAD") == NULL);

    /* disabled: nothing is installed, flags are never reported */
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);
    sigs_install(0);
    CHECK(disposition(SIGUSR1) == (void *) SIG_IGN);
    raise(SIGUSR1);
    CHECK(poll(0, 0, 0) == SIGS_NONE);
    sigs_arm();
    CHECK(disposition(SIGUSR1) == (void *) SIG_IGN);

    /* enabled */
    sigs_install(1);
    CHECK(disposition(SIGUSR1) != (void *) SIG_IGN && disposition(SIGUSR1) != (void *) SIG_DFL);
    CHECK(poll(0, 0, 0) == SIGS_NONE);

    raise(SIGUSR2);
    CHECK(poll(0, 0, 0) == SIGS_MENU);
    CHECK(poll(0, 0, 0) == SIGS_NONE);                 /* consumed */
    raise(SIGUSR2);
    CHECK(poll(1, 0, 0) == SIGS_MENU);                 /* closes the open menu: the same action */
    CHECK(poll(1, 0, 0) == SIGS_NONE);

    raise(SIGUSR1);
    CHECK(poll(0, 0, 0) == SIGS_SAVE_QUIT);
    CHECK(poll(0, 0, 0) == SIGS_NONE);

    /* SIGUSR1 waits for a save or load in flight, and is not lost */
    raise(SIGUSR1);
    CHECK(poll(0, 0, 1) == SIGS_NONE);
    CHECK(poll(0, 0, 1) == SIGS_NONE);
    CHECK(poll(0, 0, 0) == SIGS_SAVE_QUIT);

    /* with the menu open it closes the menu first and stays pending for the swap after */
    raise(SIGUSR1);
    CHECK(poll(1, 0, 1) == SIGS_MENU);
    CHECK(poll(1, 0, 1) == SIGS_MENU);                 /* the caller closes at once; asking again is harmless */
    CHECK(poll(0, 0, 0) == SIGS_SAVE_QUIT);

    /* both at once: the save wins, SIGUSR2 is dropped rather than reopening the menu after */
    raise(SIGUSR2);
    raise(SIGUSR1);
    CHECK(poll(0, 0, 0) == SIGS_SAVE_QUIT);
    CHECK(poll(0, 0, 0) == SIGS_NONE);

    /* SIGUSR2 behind a waiting SIGUSR1 is held back, too */
    raise(SIGUSR1);
    raise(SIGUSR2);
    CHECK(poll(0, 0, 1) == SIGS_NONE);
    CHECK(poll(0, 0, 0) == SIGS_SAVE_QUIT);
    CHECK(poll(0, 0, 0) == SIGS_NONE);

    /* a quit under way (the quit confirm done, a save for it running): signals are swallowed */
    raise(SIGUSR1);
    CHECK(poll(0, 1, 1) == SIGS_NONE);
    CHECK(poll(0, 0, 0) == SIGS_NONE);
    raise(SIGUSR2);
    CHECK(poll(0, 1, 0) == SIGS_NONE);
    CHECK(poll(0, 0, 0) == SIGS_NONE);
    raise(SIGUSR1);
    CHECK(poll(1, 1, 0) == SIGS_NONE);

    /* arming again (the core's handler is replaced) keeps ours; flags set before are kept */
    raise(SIGUSR2);
    sigs_arm();
    sigs_arm();
    CHECK(poll(0, 0, 0) == SIGS_MENU);
    {
        struct sigaction sa;
        sigs_install(1);
        signal(SIGUSR1, SIG_DFL);                      /* what the core's main_run does to our startup handler */
        sigs_arm();
        sigaction(SIGUSR1, NULL, &sa);
        CHECK(sa.sa_handler != SIG_DFL && sa.sa_handler != SIG_IGN);
        CHECK(sa.sa_flags & SA_RESTART);
        signal(SIGUSR1, SIG_DFL);
        sigs_arm();                                    /* once only: not again on every frame */
        CHECK(disposition(SIGUSR1) == (void *) SIG_DFL);
        sigs_install(1);
    }

    /* shutdown: the library is about to go, so nothing of ours may stay installed */
    sigs_release();
    CHECK(disposition(SIGUSR1) == (void *) SIG_IGN && disposition(SIGUSR2) == (void *) SIG_IGN);
    raise(SIGUSR1);
    CHECK(poll(0, 0, 0) == SIGS_NONE);

    printf("%d checks, %d failed\n", l_checks, l_fail);
    return l_fail ? 1 : 0;
}
