/* vpad: a scripted virtual gamepad (Linux uinput) for testing suxty on a rig where nobody can press buttons.
 *
 *   vpad "<ms>:<button>:<0|1>,<ms>:<axis>:<value>,..."   (times are from the moment the device exists)
 *   buttons: a b x y l r select start up down left right
 *   axes (only with VPAD_AXES=1): lx ly rx ry (-32768..32767), lt rt (triggers, -32768 = released .. 32767)
 *
 * The device exposes BTN_SOUTH/EAST/NORTH/WEST/TL/TR/SELECT/START plus a D-pad hat, so SDL numbers the
 * buttons 0..7 as A B X Y L R Select Start - exactly suxty's fallback layout for unmapped pads.
 * VPAD_NAME=<name> changes the device name (default "suxty vpad"), to run two of them.
 * With VPAD_AXES=1 it also has the axes ABS_X/Y (left stick), ABS_RX/RY (right stick) and ABS_Z/RZ (triggers),
 * which makes SDL build a game controller mapping for it (the "mapped pad" case; without them it is the
 * "unmapped pad" case).
 * Cross-build:  aarch64-linux-gnu-gcc -O2 -o vpad tools/vpad.c
 * GPLv2+, like the rest of the project.
 */
#include <errno.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int fd;

static void emit(int type, int code, int value)
{
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.code = code;
    ev.value = value;
    if (write(fd, &ev, sizeof(ev)) < 0)
        perror("write");
}

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

int main(int argc, char **argv)
{
    struct uinput_setup us;
    struct uinput_abs_setup as;
    static const struct { const char *name; int code; } keys[] = {
        { "a", BTN_SOUTH }, { "b", BTN_EAST }, { "x", BTN_NORTH }, { "y", BTN_WEST },
        { "l", BTN_TL }, { "r", BTN_TR }, { "select", BTN_SELECT }, { "start", BTN_START },
    };
    static const struct { const char *name; int code; } axes[] = {
        { "lx", ABS_X }, { "ly", ABS_Y }, { "rx", ABS_RX }, { "ry", ABS_RY }, { "lt", ABS_Z }, { "rt", ABS_RZ },
    };
    unsigned i;
    char *script, *tok, *save = NULL;
    long t0;
    int with_axes = getenv("VPAD_AXES") != NULL && getenv("VPAD_AXES")[0] == '1';

    if (argc < 2)
    {
        fprintf(stderr, "usage: %s \"ms:button:0|1,...\"\n", argv[0]);
        return 1;
    }
    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0)
    {
        perror("open /dev/uinput");
        return 2;
    }
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        ioctl(fd, UI_SET_KEYBIT, keys[i].code);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_ABSBIT, ABS_HAT0X);
    ioctl(fd, UI_SET_ABSBIT, ABS_HAT0Y);
    memset(&as, 0, sizeof(as));
    as.absinfo.minimum = -1;
    as.absinfo.maximum = 1;
    as.code = ABS_HAT0X;
    ioctl(fd, UI_ABS_SETUP, &as);
    as.code = ABS_HAT0Y;
    ioctl(fd, UI_ABS_SETUP, &as);
    if (with_axes)
    {
        for (i = 0; i < sizeof(axes) / sizeof(axes[0]); i++)
        {
            ioctl(fd, UI_SET_ABSBIT, axes[i].code);
            memset(&as, 0, sizeof(as));
            as.code = axes[i].code;
            as.absinfo.minimum = -32768;
            as.absinfo.maximum = 32767;
            as.absinfo.value = (axes[i].code == ABS_Z || axes[i].code == ABS_RZ) ? -32768 : 0;   /* triggers rest low */
            ioctl(fd, UI_ABS_SETUP, &as);
        }
    }
    memset(&us, 0, sizeof(us));
    us.id.bustype = BUS_USB;
    us.id.vendor = 0x1234;
    us.id.product = 0x5678;
    us.id.version = 1;
    snprintf(us.name, sizeof(us.name), "%s", getenv("VPAD_NAME") ? getenv("VPAD_NAME") : "suxty vpad");
    ioctl(fd, UI_DEV_SETUP, &us);
    if (ioctl(fd, UI_DEV_CREATE) < 0)
    {
        perror("UI_DEV_CREATE");
        return 3;
    }
    printf("vpad: device created\n");
    fflush(stdout);
    t0 = now_ms();

    script = strdup(argv[1]);
    for (tok = strtok_r(script, ",", &save); tok != NULL; tok = strtok_r(NULL, ",", &save))
    {
        long ms;
        char name[32];
        int val;
        if (sscanf(tok, "%ld:%31[a-z]:%d", &ms, name, &val) != 3)
        {
            fprintf(stderr, "bad token '%s'\n", tok);
            continue;
        }
        while (now_ms() - t0 < ms)
            usleep(2000);
        {
            int done = 0;
            for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
                if (strcmp(keys[i].name, name) == 0)
                {
                    emit(EV_KEY, keys[i].code, val);
                    done = 1;
                }
            for (i = 0; with_axes && !done && i < sizeof(axes) / sizeof(axes[0]); i++)
                if (strcmp(axes[i].name, name) == 0)
                {
                    emit(EV_ABS, axes[i].code, val);
                    done = 1;
                }
            if (!done)
            {
                if (strcmp(name, "up") == 0)    emit(EV_ABS, ABS_HAT0Y, val ? -1 : 0);
                else if (strcmp(name, "down") == 0)  emit(EV_ABS, ABS_HAT0Y, val ? 1 : 0);
                else if (strcmp(name, "left") == 0)  emit(EV_ABS, ABS_HAT0X, val ? -1 : 0);
                else if (strcmp(name, "right") == 0) emit(EV_ABS, ABS_HAT0X, val ? 1 : 0);
            }
            emit(EV_SYN, SYN_REPORT, 0);
        }
        printf("vpad: t=%ldms %s=%d\n", now_ms() - t0, name, val);
        fflush(stdout);
    }
    sleep(2);
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    return 0;
}
