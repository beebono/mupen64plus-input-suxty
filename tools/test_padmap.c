/* Unit tests for src/padmap.c (the N64 mapping model): axis activation and trigger rest values, deadzone/range
 * shaping, presets on three pad layouts (an Xbox-like pad, the RK3566 handheld whose triggers and D-pad are plain
 * buttons, and a pad with neither sticks nor triggers), the modifier layer, and the ini round trip.
 *
 *   make -C projects/unix check          GPLv2+, like the rest of the project.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "padmap.h"

static int l_fail, l_checks;

#define CHECK(cond) do { l_checks++; if (!(cond)) { l_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ---- fake pad state ---- */
typedef struct {
    uint8_t buttons[32];
    int     hat[2];
    int     axis[8];
    int     rest[8];
} fake_pad;

static int f_button(void *c, int i) { return ((fake_pad *) c)->buttons[i]; }
static int f_hat(void *c, int i)    { return ((fake_pad *) c)->hat[i]; }
static int f_axis(void *c, int i)   { return ((fake_pad *) c)->axis[i]; }
static int f_rest(void *c, int i)   { return ((fake_pad *) c)->rest[i]; }

static void fake_reset(fake_pad *f)
{
    memset(f, 0, sizeof(*f));
    f->rest[2] = f->rest[5] = -32768;       /* xbox-style triggers */
    f->axis[2] = f->axis[5] = -32768;
}

static n64_state eval(const padmap *m, fake_pad *f)
{
    padmap_io io = { f_button, f_hat, f_axis, f_rest, f };
    n64_state st;
    padmap_eval(m, &io, &st);
    return st;
}

#define BTN(n) ((pad_bind) { PAD_BIND_BUTTON, (n), 0 })
#define AX(n)  ((pad_bind) { PAD_BIND_AXIS, (n), 0 })
#define HATB(d) ((pad_bind) { PAD_BIND_HAT, 0, (d) })

/* SDL's xbox layout: A=0 B=1 X=2 Y=3 LB=4 RB=5 back=6 start=7 L3=9 R3=10, sticks 0-1 / 3-4, triggers 2 / 5, D-pad hat */
static void layout_xbox(pad_layout *L)
{
    memset(L, 0, sizeof(*L));
    snprintf(L->name, sizeof(L->name), "Xbox-like");
    L->b[PAD_A] = BTN(0); L->b[PAD_B] = BTN(1); L->b[PAD_X] = BTN(2); L->b[PAD_Y] = BTN(3);
    L->b[PAD_L] = BTN(4); L->b[PAD_R] = BTN(5); L->b[PAD_SELECT] = BTN(6); L->b[PAD_START] = BTN(7);
    L->b[PAD_L3] = BTN(9); L->b[PAD_R3] = BTN(10);
    L->b[PAD_UP] = HATB(SDL_HAT_UP); L->b[PAD_DOWN] = HATB(SDL_HAT_DOWN);
    L->b[PAD_LEFT] = HATB(SDL_HAT_LEFT); L->b[PAD_RIGHT] = HATB(SDL_HAT_RIGHT);
    L->b[PAD_L2] = AX(2); L->b[PAD_R2] = AX(5);
    L->lx = AX(0); L->ly = AX(1); L->rx = AX(3); L->ry = AX(4);
    L->has_ls = L->has_rs = 1;
}

/* the RK3566 handheld (retrogame_joypad): A=b0 B=b1 X=b2 Y=b3, L1=b4 R1=b5, L2=b6 R2=b7 (buttons!), select=b8 start=b9,
 * L3=b11 R3=b12, D-pad b13-16, sticks a0-a3 */
static void layout_rk3566(pad_layout *L)
{
    memset(L, 0, sizeof(*L));
    snprintf(L->name, sizeof(L->name), "retrogame_joypad");
    L->b[PAD_A] = BTN(0); L->b[PAD_B] = BTN(1); L->b[PAD_X] = BTN(2); L->b[PAD_Y] = BTN(3);
    L->b[PAD_L] = BTN(4); L->b[PAD_R] = BTN(5); L->b[PAD_L2] = BTN(6); L->b[PAD_R2] = BTN(7);
    L->b[PAD_SELECT] = BTN(8); L->b[PAD_START] = BTN(9); L->b[PAD_L3] = BTN(11); L->b[PAD_R3] = BTN(12);
    L->b[PAD_UP] = BTN(13); L->b[PAD_DOWN] = BTN(14); L->b[PAD_LEFT] = BTN(15); L->b[PAD_RIGHT] = BTN(16);
    L->lx = AX(0); L->ly = AX(1); L->rx = AX(2); L->ry = AX(3);
    L->has_ls = L->has_rs = 1;
}

/* a small pad: four face buttons, two shoulders, D-pad on a hat, nothing else */
static void layout_tiny(pad_layout *L)
{
    memset(L, 0, sizeof(*L));
    snprintf(L->name, sizeof(L->name), "tiny");
    L->b[PAD_A] = BTN(0); L->b[PAD_B] = BTN(1); L->b[PAD_X] = BTN(2); L->b[PAD_Y] = BTN(3);
    L->b[PAD_L] = BTN(4); L->b[PAD_R] = BTN(5); L->b[PAD_SELECT] = BTN(6); L->b[PAD_START] = BTN(7);
    L->b[PAD_UP] = HATB(SDL_HAT_UP); L->b[PAD_DOWN] = HATB(SDL_HAT_DOWN);
    L->b[PAD_LEFT] = HATB(SDL_HAT_LEFT); L->b[PAD_RIGHT] = HATB(SDL_HAT_RIGHT);
}

static void test_shape(void)
{
    padmap m;
    padmap_clear(&m);                         /* deadzone 12 %, range 100 % */
    CHECK(padmap_shape(&m, 0) == 0);
    CHECK(padmap_shape(&m, 3932) == 0);       /* exactly the deadzone */
    CHECK(padmap_shape(&m, 32767) == 32767);
    CHECK(padmap_shape(&m, 3933) > 0 && padmap_shape(&m, 3933) < 100);
    {
        int mid = padmap_shape(&m, 3932 + (32767 - 3932) / 2);
        CHECK(mid > 16000 && mid < 16800);
    }
    m.deadzone = 0; m.peak = 50;              /* half travel is already full deflection */
    CHECK(padmap_shape(&m, 16383) == 32767 || padmap_shape(&m, 16384) == 32767);
    CHECK(padmap_shape(&m, 8000) > 15000 && padmap_shape(&m, 8000) < 17000);
}

static void test_axes(void)
{
    fake_pad f;
    padsrc s;
    padmap_io io = { f_button, f_hat, f_axis, f_rest, &f };

    fake_reset(&f);
    s = (padsrc) { PAD_BIND_AXIS, 0, 0, 1 };                 /* right half of the left stick */
    f.axis[0] = 20000;  CHECK(padsrc_activation(&s, &io) == 20000);
    f.axis[0] = -20000; CHECK(padsrc_activation(&s, &io) == 0);
    s.dir = -1;         CHECK(padsrc_activation(&s, &io) == 20000);
    s = (padsrc) { PAD_BIND_AXIS, 0, 2, 1 };                 /* a trigger resting at -32768 */
    f.axis[2] = -32768; CHECK(padsrc_activation(&s, &io) == 0);
    f.axis[2] = 0;      CHECK(padsrc_activation(&s, &io) > 16000 && padsrc_activation(&s, &io) < 16800);
    f.axis[2] = 32767;  CHECK(padsrc_activation(&s, &io) == 32767);
    s.dir = -1;         CHECK(padsrc_activation(&s, &io) == 0);   /* the other half of a trigger is never pressed */
    CHECK(padsrc_same(&(padsrc) { PAD_BIND_AXIS, 0, 2, 1 }, &(padsrc) { PAD_BIND_AXIS, 0, 2, 1 }));
    CHECK(!padsrc_same(&(padsrc) { PAD_BIND_AXIS, 0, 2, 1 }, &(padsrc) { PAD_BIND_AXIS, 0, 2, -1 }));
    CHECK(!padsrc_same(&(padsrc) { PAD_BIND_HAT, SDL_HAT_UP, 0, 0 }, &(padsrc) { PAD_BIND_HAT, SDL_HAT_DOWN, 0, 0 }));
}

static void test_rstick_preset(void)
{
    pad_layout L; padmap m; fake_pad f; n64_state st; char msg[64];

    layout_xbox(&L);
    padmap_clear(&m);
    CHECK(padmap_apply_preset(&m, PRESET_DEFAULT, &L, msg, sizeof(msg)) == 0);
    CHECK(strcmp(m.preset, "default") == 0);
    CHECK(m.src[0][N64_A].type == PAD_BIND_BUTTON && m.src[0][N64_A].index == 0);
    CHECK(m.src[0][N64_B].index == 2);                         /* west */
    CHECK(m.src[0][N64_Z].type == PAD_BIND_AXIS && m.src[0][N64_Z].index == 2);   /* left trigger */
    CHECK(m.src[0][N64_CUP].type == PAD_BIND_AXIS && m.src[0][N64_CUP].index == 4 && m.src[0][N64_CUP].dir == -1);
    CHECK(m.src[0][N64_SLEFT].index == 0 && m.src[0][N64_SLEFT].dir == -1);
    CHECK(m.modifier.type == PAD_BIND_NONE);

    fake_reset(&f);
    f.axis[0] = 32767; f.axis[4] = -32767; f.axis[2] = 32767; f.buttons[0] = 1;
    st = eval(&m, &f);
    CHECK(st.sx == 32767 && st.sy == 0);
    CHECK((st.buttons & (1u << N64_CUP)) && (st.buttons & (1u << N64_Z)) && (st.buttons & (1u << N64_A)));
    CHECK(!(st.buttons & (1u << N64_B)));
    f.axis[0] = 2000;                                           /* inside the deadzone */
    st = eval(&m, &f);
    CHECK(st.sx == 0);
    f.axis[1] = -32767;                                         /* stick up -> y negative (SDL) */
    st = eval(&m, &f);
    CHECK(st.sy == -32767);
}

static void test_face_c_modifier(void)
{
    pad_layout L; padmap m; fake_pad f; n64_state st; char msg[64];

    layout_rk3566(&L);
    padmap_clear(&m);
    CHECK(padmap_apply_preset(&m, PRESET_FACE_C, &L, msg, sizeof(msg)) == 0);
    CHECK(m.modifier.type == PAD_BIND_BUTTON && m.modifier.index == 7);     /* R2, because Z is L2 */
    CHECK(m.src[0][N64_Z].index == 6);
    CHECK(m.src[1][N64_CDOWN].index == 0 && m.src[1][N64_CRIGHT].index == 1 &&
          m.src[1][N64_CLEFT].index == 2 && m.src[1][N64_CUP].index == 3);

    fake_reset(&f);
    f.buttons[0] = 1;                                           /* A alone: N64 A */
    st = eval(&m, &f);
    CHECK(st.buttons == (1u << N64_A) && !st.mod);
    f.buttons[7] = 1;                                           /* hold the modifier: A becomes C-Down, not A */
    st = eval(&m, &f);
    CHECK(st.mod && st.buttons == (1u << N64_CDOWN));
    f.buttons[2] = 1; f.buttons[3] = 1;                         /* X, Y -> C-Left, C-Up (B is not N64 B meanwhile) */
    st = eval(&m, &f);
    CHECK(st.buttons == ((1u << N64_CDOWN) | (1u << N64_CLEFT) | (1u << N64_CUP)));
    f.buttons[7] = 0;                                           /* modifier released: back to normal */
    st = eval(&m, &f);
    CHECK(st.buttons == ((1u << N64_A) | (1u << N64_B)));
    f.buttons[6] = 1;                                           /* L2 = Z, unaffected by the modifier */
    st = eval(&m, &f);
    CHECK(st.buttons & (1u << N64_Z));
}

static void test_dpad_stick_and_tiny(void)
{
    pad_layout L; padmap m; fake_pad f; n64_state st; char msg[96];

    layout_tiny(&L);
    padmap_clear(&m);
    CHECK(padmap_apply_preset(&m, PRESET_DEFAULT, &L, msg, sizeof(msg)) == 0);    /* no sticks: D-pad is the stick */
    CHECK(m.src[0][N64_SLEFT].type == PAD_BIND_HAT && m.src[0][N64_SLEFT].hat_mask == SDL_HAT_LEFT);
    CHECK(m.src[0][N64_Z].type == PAD_BIND_NONE);                                 /* nothing sensible for Z */
    CHECK(m.modifier.type == PAD_BIND_NONE && msg[0] != 0);                       /* and it says so */
    CHECK(m.src[0][N64_CUP].index == 3 && m.src[0][N64_CRIGHT].index == 1);       /* last-resort C buttons */
    fake_reset(&f);
    f.hat[0] = SDL_HAT_RIGHT | SDL_HAT_UP;
    st = eval(&m, &f);
    CHECK(st.sx == 32767 && st.sy == -32767);

    layout_rk3566(&L);
    padmap_clear(&m);
    CHECK(padmap_apply_preset(&m, PRESET_DPAD_STICK, &L, msg, sizeof(msg)) == 0);
    CHECK(m.src[0][N64_SUP].index == 13);                                         /* D-pad up button */
    CHECK(m.src[0][N64_DUP].type == PAD_BIND_AXIS && m.src[0][N64_DUP].index == 1);   /* left stick is the N64 D-pad */
    CHECK(m.src[0][N64_CUP].index == 3 && m.src[0][N64_CUP].dir == -1);          /* right stick = C */

    layout_tiny(&L);
    padmap_clear(&m);
    CHECK(padmap_apply_preset(&m, PRESET_RSTICK_C, &L, msg, sizeof(msg)) == -1);  /* needs two sticks, map untouched */
    CHECK(m.src[0][N64_A].type == PAD_BIND_NONE);
}

static void test_swap_and_clear(void)
{
    pad_layout L; padmap m; char msg[64];
    layout_xbox(&L);
    padmap_clear(&m);
    m.deadzone = 20; m.peak = 80;
    CHECK(padmap_apply_preset(&m, PRESET_SWAP_STICKS, &L, msg, sizeof(msg)) == 0);
    CHECK(m.deadzone == 20 && m.peak == 80);                    /* stick settings survive a preset */
    CHECK(m.src[0][N64_SRIGHT].index == 3 && m.src[0][N64_CRIGHT].index == 0);
}

static void test_ini_roundtrip(void)
{
    pad_layout L; padmap a, b; char msg[64], sec[96];
    ini_t *ini = ini_new();
    int n, l;

    layout_rk3566(&L);
    padmap_clear(&a);
    padmap_apply_preset(&a, PRESET_FACE_C, &L, msg, sizeof(msg));
    a.deadzone = 17; a.peak = 90;
    padmap_section("retrogame_joypad", sec, sizeof(sec));
    CHECK(strcmp(sec, "pad:retrogame_joypad") == 0);
    padmap_store(&a, ini, sec);
    padmap_clear(&b);
    CHECK(padmap_load(&b, ini, NULL, sec) == 1);
    CHECK(b.deadzone == 17 && b.peak == 90 && strcmp(b.preset, a.preset) == 0);
    CHECK(memcmp(&b.modifier, &a.modifier, sizeof(padsrc)) == 0);
    for (l = 0; l < PADMAP_LAYERS; l++)
        for (n = 0; n < N64_NUM; n++)
            CHECK(memcmp(&b.src[l][n], &a.src[l][n], sizeof(padsrc)) == 0);
    /* an overlay (the per-game file of M4) wins over the global one */
    {
        ini_t *game = ini_new();
        padmap c;
        ini_set(game, sec, "a", "button:5");
        ini_set(game, sec, "deadzone", "30");
        padmap_clear(&c);
        padmap_load(&c, ini, game, sec);
        CHECK(c.src[0][N64_A].index == 5 && c.deadzone == 30 && c.peak == 90 && c.src[0][N64_B].index == a.src[0][N64_B].index);
        ini_free(game);
    }
    /* odd characters in a pad name cannot break the ini */
    padmap_section("Odd [pad]=name;#", sec, sizeof(sec));
    CHECK(strchr(sec, '[') == NULL && strchr(sec, ']') == NULL && strchr(sec, '=') == NULL);
    {
        padsrc s;
        CHECK(padsrc_parse("hat:1:left", &s) == 0 && s.type == PAD_BIND_HAT && s.index == 1 && s.hat_mask == SDL_HAT_LEFT);
        CHECK(padsrc_parse("axis:3-", &s) == 0 && s.type == PAD_BIND_AXIS && s.index == 3 && s.dir == -1);
        CHECK(padsrc_parse("none", &s) == 0 && s.type == PAD_BIND_NONE);
        CHECK(padsrc_parse("bogus", &s) == -1 && s.type == PAD_BIND_NONE);
    }
    ini_free(ini);
}

/* DebugMessage is declared in main.h for the rest of the program; padmap.c does not use it */
int main(void)
{
    test_shape();
    test_axes();
    test_rstick_preset();
    test_face_c_modifier();
    test_dpad_stick_and_tiny();
    test_swap_and_clear();
    test_ini_roundtrip();
    printf("%d checks, %d failed\n", l_checks, l_fail);
    return l_fail ? 1 : 0;
}
