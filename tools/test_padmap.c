/* Unit tests for src/padmap.c; run with make -C projects/unix check. GPLv2+, like the rest of the project. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "padmap.h"

static int l_fail, l_checks;

#define CHECK(cond) do { l_checks++; if (!(cond)) { l_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

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
    f->rest[2] = f->rest[5] = -32768;
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

/* the RK3566 handheld (retrogame_joypad): A=b0 B=b1 X=b2 Y=b3, L1=b4 R1=b5, L2=b6 R2=b7 (buttons!), select=b8
 * start=b9, L3=b11 R3=b12, D-pad b13-16, sticks a0-a3 */
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
    s = (padsrc) { PAD_BIND_AXIS, 0, 0, 1 };
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

static void test_default_two_sticks(void)
{
    pad_layout L; padmap m; fake_pad f; n64_state st; char msg[64];

    layout_xbox(&L);
    padmap_clear(&m);
    m.deadzone = 20; m.peak = 80;
    padmap_default(&m, &L, msg, sizeof(msg));
    CHECK(m.deadzone == 20 && m.peak == 80);                   /* stick settings survive a reset to the default mapping */
    CHECK(m.src[N64_A].type == PAD_BIND_BUTTON && m.src[N64_A].index == 0);
    CHECK(m.src[N64_B].index == 2);
    CHECK(m.src[N64_Z].type == PAD_BIND_AXIS && m.src[N64_Z].index == 2);
    CHECK(m.src[N64_CUP].type == PAD_BIND_AXIS && m.src[N64_CUP].index == 4 && m.src[N64_CUP].dir == -1);
    CHECK(m.src[N64_SLEFT].index == 0 && m.src[N64_SLEFT].dir == -1);
    CHECK(m.src[N64_DUP].type == PAD_BIND_HAT && m.src[N64_DUP].hat_mask == SDL_HAT_UP);
    CHECK(m.cface.type == PAD_BIND_NONE && m.softtilt.type == PAD_BIND_NONE && m.dpad_swap == 0);
    m.deadzone = PADMAP_DEFAULT_DEADZONE; m.peak = 100;

    fake_reset(&f);
    f.axis[0] = 32767; f.axis[4] = -32767; f.axis[2] = 32767; f.buttons[0] = 1;
    st = eval(&m, &f);
    CHECK(st.sx == 32767 && st.sy == 0);
    CHECK((st.buttons & (1u << N64_CUP)) && (st.buttons & (1u << N64_Z)) && (st.buttons & (1u << N64_A)));
    CHECK(!(st.buttons & (1u << N64_B)));
    f.axis[0] = 2000;
    st = eval(&m, &f);
    CHECK(st.sx == 0);
    f.axis[1] = -32767;
    st = eval(&m, &f);
    CHECK(st.sy == -32767);
}

static void test_cface_shift(void)
{
    pad_layout L; padmap m; fake_pad f; n64_state st; char msg[64];

    layout_rk3566(&L);
    L.has_rs = 0;
    padmap_clear(&m);
    padmap_default(&m, &L, msg, sizeof(msg));
    CHECK(m.cface.type == PAD_BIND_BUTTON && m.cface.index == 7);          /* R2, because Z is L2 */
    CHECK(m.src[N64_Z].index == 6);
    CHECK(m.cface_src[N64_CDOWN - N64_CUP].index == 0 && m.cface_src[N64_CRIGHT - N64_CUP].index == 1 &&
          m.cface_src[N64_CLEFT - N64_CUP].index == 2 && m.cface_src[N64_CUP - N64_CUP].index == 3);

    fake_reset(&f);
    f.buttons[0] = 1;
    st = eval(&m, &f);
    CHECK(st.buttons == (1u << N64_A) && !st.cshift);
    f.buttons[7] = 1;
    st = eval(&m, &f);
    CHECK(st.cshift && st.buttons == (1u << N64_CDOWN));
    f.buttons[2] = 1; f.buttons[3] = 1;
    st = eval(&m, &f);
    CHECK(st.buttons == ((1u << N64_CDOWN) | (1u << N64_CLEFT) | (1u << N64_CUP)));
    f.buttons[7] = 0;
    st = eval(&m, &f);
    CHECK(st.buttons == ((1u << N64_A) | (1u << N64_B)));
    f.buttons[6] = 1;
    st = eval(&m, &f);
    CHECK(st.buttons & (1u << N64_Z));
    /* the shift button is never also an input: map it to Start as well and Start still does not fire with it */
    m.src[N64_START] = m.cface;
    f.buttons[7] = 1;
    st = eval(&m, &f);
    CHECK(!(st.buttons & (1u << N64_START)));
    padmap_set_cface(&m, &L, &(padsrc) { 0 });
    CHECK(m.cface.type == PAD_BIND_NONE && m.cface_src[0].type == PAD_BIND_NONE);
    padmap_set_cface(&m, &L, &(padsrc) { PAD_BIND_BUTTON, 0, 12, 0 });
    CHECK(m.cface.index == 12 && m.cface_src[N64_CUP - N64_CUP].index == 3);
}

static void test_tiny_default(void)
{
    pad_layout L; padmap m; fake_pad f; n64_state st; char msg[96];

    layout_tiny(&L);
    padmap_clear(&m);
    padmap_default(&m, &L, msg, sizeof(msg));                                     /* no sticks: D-pad is the stick */
    CHECK(m.src[N64_SLEFT].type == PAD_BIND_HAT && m.src[N64_SLEFT].hat_mask == SDL_HAT_LEFT);
    CHECK(m.src[N64_DLEFT].type == PAD_BIND_NONE && m.dpad_swap == 0);
    CHECK(m.src[N64_Z].type == PAD_BIND_NONE);
    CHECK(m.cface.type == PAD_BIND_NONE && msg[0] != 0);
    CHECK(m.src[N64_CUP].index == 3 && m.src[N64_CRIGHT].index == 1);
    fake_reset(&f);
    f.hat[0] = SDL_HAT_RIGHT | SDL_HAT_UP;
    st = eval(&m, &f);
    CHECK(st.sx == 32767 && st.sy == -32767);

    layout_rk3566(&L);
    padmap_clear(&m);
    padmap_default(&m, &L, msg, sizeof(msg));
    CHECK(m.src[N64_DUP].index == 13);
    CHECK(m.src[N64_SUP].type == PAD_BIND_AXIS && m.src[N64_SUP].index == 1);
    CHECK(m.src[N64_CUP].index == 3 && m.src[N64_CUP].dir == -1);
}

static void test_dpad_swap(void)
{
    pad_layout L; padmap m; fake_pad f; n64_state st; char msg[64];
    int i;

    layout_rk3566(&L);
    padmap_clear(&m);
    padmap_default(&m, &L, msg, sizeof(msg));
    CHECK(padmap_dpad_is_stick(&m, &L) == 0 && padmap_dpad_is_stick(&m, NULL) == 0);
    fake_reset(&f);
    f.buttons[13] = 1;
    st = eval(&m, &f);
    CHECK(st.buttons == (1u << N64_DUP) && st.sy == 0);

    m.dpad_swap = 1;
    CHECK(padmap_dpad_is_stick(&m, &L) == 1 && padmap_dpad_is_stick(&m, NULL) == 1);
    st = eval(&m, &f);
    CHECK(st.buttons == 0 && st.sy == -32767 && st.sx == 0);
    f.buttons[13] = 0;
    f.axis[0] = 32767;
    st = eval(&m, &f);
    CHECK(st.buttons == (1u << N64_DRIGHT) && st.sx == 0);
    f.axis[0] = 10000;
    st = eval(&m, &f);
    CHECK(st.buttons == 0);
    f.axis[0] = 0;
    f.buttons[15] = 1; f.buttons[14] = 1;
    st = eval(&m, &f);
    CHECK(st.sx == -32767 && st.sy == 32767 && st.buttons == 0);

    CHECK(padmap_chan(&m, N64_DUP) == N64_SUP && padmap_chan(&m, N64_SLEFT) == N64_DLEFT && padmap_chan(&m, N64_A) == N64_A);
    for (i = 0; i < N64_NUM; i++)
        CHECK(padmap_chan(&m, padmap_chan(&m, (n64_ctl) i)) == (n64_ctl) i);
    m.dpad_swap = 0;
    CHECK(padmap_chan(&m, N64_DUP) == N64_DUP);

    layout_tiny(&L);
    padmap_default(&m, &L, msg, sizeof(msg));
    CHECK(padmap_dpad_is_stick(&m, &L) == 1 && padmap_dpad_is_stick(&m, NULL) == 0);   /* by the rows; the flag alone does not know */
    m.dpad_swap = 1;
    CHECK(padmap_dpad_is_stick(&m, &L) == 0);
    fake_reset(&f);
    f.hat[0] = SDL_HAT_LEFT;
    st = eval(&m, &f);
    CHECK(st.buttons == (1u << N64_DLEFT) && st.sx == 0);
}

static void test_soft_tilt(void)
{
    pad_layout L; padmap m; fake_pad f; n64_state st; char msg[64];

    layout_rk3566(&L);
    padmap_clear(&m);
    padmap_default(&m, &L, msg, sizeof(msg));
    m.softtilt = (padsrc) { PAD_BIND_BUTTON, 0, 7, 0 };
    m.tilt_pct = 50;
    fake_reset(&f);
    f.axis[0] = 32767;
    st = eval(&m, &f);
    CHECK(st.sx == 32767 && !st.tilt);
    f.buttons[7] = 1;
    st = eval(&m, &f);
    CHECK(st.tilt && st.sx == 16383 && st.sy == 0);
    /* applied after the shaping: a stick with little travel (full deflection at 50 %) is halved too */
    m.peak = 50;
    f.buttons[7] = 0; f.axis[0] = 16384;
    st = eval(&m, &f);
    CHECK(st.sx == 32767);
    f.buttons[7] = 1;
    st = eval(&m, &f);
    CHECK(st.sx == 16383);
    m.peak = 100;
    /* the diagonal keeps its direction: both axes are scaled by the same factor */
    f.axis[0] = 32767; f.axis[1] = -32767;
    st = eval(&m, &f);
    CHECK(st.sx == 16383 && st.sy == -16383);
    f.axis[1] = -16000;                                                           /* not a perfect diagonal: 2 : 1 stays 2 : 1 */
    {
        n64_state a;
        f.buttons[7] = 0;
        a = eval(&m, &f);
        f.buttons[7] = 1;
        st = eval(&m, &f);
        CHECK(st.sx == a.sx / 2 && (st.sy == a.sy / 2 || st.sy == a.sy / 2 + 1 || st.sy == a.sy / 2 - 1));
    }
    /* digital sources: the D-pad as the stick at full deflection gives half strength, diagonals included */
    m.dpad_swap = 1;
    f.axis[0] = f.axis[1] = 0;
    f.buttons[13] = 1; f.buttons[16] = 1;
    st = eval(&m, &f);
    CHECK(st.sx == 16383 && st.sy == -16383);
    f.buttons[7] = 0;
    st = eval(&m, &f);
    CHECK(st.sx == 32767 && st.sy == -32767);
    m.dpad_swap = 0;
    f.buttons[13] = f.buttons[16] = 0;
    f.axis[0] = 32767; f.buttons[7] = 1;
    m.tilt_pct = 25;  st = eval(&m, &f); CHECK(st.sx == 8191);
    m.tilt_pct = 0;   st = eval(&m, &f); CHECK(st.sx == 0);
    m.tilt_pct = 100; st = eval(&m, &f); CHECK(st.sx == 32767);
    m.tilt_pct = 250; st = eval(&m, &f); CHECK(st.sx == 32767);                   /* clamped */
    m.tilt_pct = 50;
    /* the Soft Tilt button is never also an input */
    m.src[N64_START] = m.softtilt;
    st = eval(&m, &f);
    CHECK(!(st.buttons & (1u << N64_START)));
}

static void test_reset_default(void)
{
    pad_layout L; padmap m; char msg[64];
    layout_xbox(&L);
    padmap_clear(&m);
    m.deadzone = 20; m.peak = 80; m.tilt_pct = 30;
    m.dpad_swap = 1;
    m.softtilt = (padsrc) { PAD_BIND_BUTTON, 0, 9, 0 };
    m.src[N64_A].type = PAD_BIND_BUTTON; m.src[N64_A].index = 9;
    padmap_default(&m, &L, msg, sizeof(msg));
    CHECK(m.dpad_swap == 0 && m.softtilt.type == PAD_BIND_NONE && m.src[N64_A].index == 0);
    CHECK(m.deadzone == 20 && m.peak == 80 && m.tilt_pct == 30);
}

static void test_ini_roundtrip(void)
{
    pad_layout L; padmap a, b; char msg[64], sec[96];
    ini_t *ini = ini_new();
    int n;

    layout_rk3566(&L);
    padmap_clear(&a);
    L.has_rs = 0;
    padmap_default(&a, &L, msg, sizeof(msg));
    a.deadzone = 17; a.peak = 90; a.dpad_swap = 1;
    a.softtilt = (padsrc) { PAD_BIND_AXIS, 0, 3, -1 };
    padmap_section("retrogame_joypad", sec, sizeof(sec));
    CHECK(strcmp(sec, "pad:retrogame_joypad") == 0);
    padmap_store(&a, ini, sec);
    padmap_clear(&b);
    CHECK(padmap_load(&b, ini, NULL, sec) == 1);
    CHECK(b.deadzone == 17 && b.peak == 90 && b.dpad_swap == 1);
    CHECK(memcmp(&b.cface, &a.cface, sizeof(padsrc)) == 0 && b.cface.index == 7);
    CHECK(memcmp(&b.softtilt, &a.softtilt, sizeof(padsrc)) == 0);
    for (n = 0; n < 4; n++)
        CHECK(memcmp(&b.cface_src[n], &a.cface_src[n], sizeof(padsrc)) == 0);
    for (n = 0; n < N64_NUM; n++)
        CHECK(memcmp(&b.src[n], &a.src[n], sizeof(padsrc)) == 0);
    CHECK(ini_has(ini, sec, "dpad_swap") && ini_has(ini, sec, "cface") && ini_has(ini, sec, "softtilt") && !ini_has(ini, sec, "preset"));
    {
        ini_t *game = ini_new();
        padmap c;
        ini_set(game, sec, "a", "button:5");
        ini_set(game, sec, "deadzone", "30");
        ini_set(game, sec, "dpad_swap", "false");
        padmap_clear(&c);
        padmap_load(&c, ini, game, sec);
        CHECK(c.src[N64_A].index == 5 && c.deadzone == 30 && c.peak == 90 && c.src[N64_B].index == a.src[N64_B].index);
        CHECK(c.dpad_swap == 0 && c.cface.index == 7);
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

static void test_migration(void)
{
    ini_t *ini = ini_new();
    padmap m;
    fake_pad f;
    n64_state st;
    int n;

    ini_set(ini, "pad:one", "preset", "Face buttons = C");
    ini_set(ini, "pad:one", "deadzone", "15");
    ini_set(ini, "pad:one", "modifier", "button:7");
    ini_set(ini, "pad:one", "a", "button:0");
    ini_set(ini, "pad:one", "b", "button:2");
    ini_set(ini, "pad:one", "mod_c_down", "button:0");
    ini_set(ini, "pad:one", "mod_c_right", "button:1");
    ini_set(ini, "pad:one", "mod_c_left", "button:2");
    ini_set(ini, "pad:one", "mod_c_up", "button:3");
    ini_set(ini, "pad:one", "mod_dpad_up", "button:9");
    ini_set(ini, "pad:two", "modifier", "button:6");
    ini_set(ini, "pad:two", "mod_a", "button:4");
    ini_set(ini, "pad:two", "a", "button:0");
    ini_set(ini, "pad:three", "mod_c_up", "button:3");
    ini_set(ini, "pad:three", "a", "button:0");
    ini_set(ini, "menu", "dim_percent", "40");
    n = padmap_migrate_ini(ini);
    CHECK(n > 0);
    CHECK(strcmp(ini_get(ini, "pad:one", "cface", ""), "button:7") == 0);
    CHECK(strcmp(ini_get(ini, "pad:one", "cface_c_up", ""), "button:3") == 0 && strcmp(ini_get(ini, "pad:one", "cface_c_down", ""), "button:0") == 0);
    CHECK(!ini_has(ini, "pad:one", "modifier") && !ini_has(ini, "pad:one", "preset") && !ini_has(ini, "pad:one", "mod_c_down") &&
          !ini_has(ini, "pad:one", "mod_dpad_up"));
    CHECK(strcmp(ini_get(ini, "pad:one", "a", ""), "button:0") == 0 && ini_get_int(ini, "pad:one", "deadzone", 0) == 15);
    CHECK(!ini_has(ini, "pad:two", "modifier") && !ini_has(ini, "pad:two", "mod_a") && !ini_has(ini, "pad:two", "cface") &&
          strcmp(ini_get(ini, "pad:two", "a", ""), "button:0") == 0);
    CHECK(!ini_has(ini, "pad:three", "mod_c_up") && !ini_has(ini, "pad:three", "cface") && ini_has(ini, "pad:three", "a"));
    CHECK(ini_get_int(ini, "menu", "dim_percent", 0) == 40);
    CHECK(padmap_migrate_ini(ini) == 0);
    padmap_clear(&m);
    CHECK(padmap_load(&m, ini, NULL, "pad:one") == 1);
    CHECK(m.cface.index == 7 && m.cface_src[N64_CDOWN - N64_CUP].index == 0);
    fake_reset(&f);
    f.buttons[0] = 1; f.buttons[7] = 1;
    st = eval(&m, &f);
    CHECK(st.cshift && st.buttons == (1u << N64_CDOWN));
    ini_free(ini);
}

/* DebugMessage is declared in main.h for the rest of the program; padmap.c does not use it */
int main(void)
{
    test_shape();
    test_axes();
    test_default_two_sticks();
    test_cface_shift();
    test_tiny_default();
    test_dpad_swap();
    test_soft_tilt();
    test_reset_default();
    test_ini_roundtrip();
    test_migration();
    printf("%d checks, %d failed\n", l_checks, l_fail);
    return l_fail ? 1 : 0;
}
