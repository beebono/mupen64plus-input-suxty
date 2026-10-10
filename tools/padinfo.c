/* padinfo [seconds]: print what SDL reports for every joystick, then (seconds > 0) log raw input changes, to read
 * button numbers
 * off a real pad. GPLv2+, like the rest of the project. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 0, i, n;
    SDL_Joystick *js[8] = { 0 };
    Uint8 hat[8][4];
    Uint8 btn[8][64];
    Sint16 ax[8][16];
    Uint32 end;

    setvbuf(stdout, NULL, _IOLBF, 0);
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) { printf("SDL_Init failed: %s\n", SDL_GetError()); return 1; }
    {
        SDL_version v; SDL_GetVersion(&v);
        printf("SDL %d.%d.%d (compiled against %d.%d.%d), SDL_GAMECONTROLLERCONFIG=%s\n", v.major, v.minor, v.patch,
               SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL, SDL_getenv("SDL_GAMECONTROLLERCONFIG") ? "set" : "unset");
    }
    n = SDL_NumJoysticks();
    printf("%d joystick(s)\n", n);
    memset(hat, 0, sizeof(hat)); memset(btn, 0, sizeof(btn)); memset(ax, 0, sizeof(ax));
    for (i = 0; i < n && i < 8; i++)
    {
        char guid[64];
        SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i), guid, sizeof(guid));
        js[i] = SDL_JoystickOpen(i);
        printf("[%d] name='%s' guid=%s buttons=%d axes=%d hats=%d balls=%d is_gamecontroller=%d\n", i, SDL_JoystickNameForIndex(i), guid,
               js[i] ? SDL_JoystickNumButtons(js[i]) : -1, js[i] ? SDL_JoystickNumAxes(js[i]) : -1,
               js[i] ? SDL_JoystickNumHats(js[i]) : -1, js[i] ? SDL_JoystickNumBalls(js[i]) : -1, SDL_IsGameController(i));
        if (SDL_IsGameController(i))
        {
            char *m = SDL_GameControllerMappingForIndex(i);
            printf("    mapping: %s\n", m ? m : "(null)");
            SDL_free(m);
        }
    }
    end = SDL_GetTicks() + (Uint32) secs * 1000u;
    while (secs > 0 && SDL_GetTicks() < end)
    {
        SDL_JoystickUpdate();
        for (i = 0; i < n && i < 8; i++)
        {
            int k;
            if (!js[i]) continue;
            for (k = 0; k < SDL_JoystickNumButtons(js[i]) && k < 64; k++)
            {
                Uint8 v = SDL_JoystickGetButton(js[i], k);
                if (v != btn[i][k]) { btn[i][k] = v; printf("%8u [%d] button %d %s\n", SDL_GetTicks(), i, k, v ? "down" : "up"); }
            }
            for (k = 0; k < SDL_JoystickNumHats(js[i]) && k < 4; k++)
            {
                Uint8 v = SDL_JoystickGetHat(js[i], k);
                if (v != hat[i][k]) { hat[i][k] = v; printf("%8u [%d] hat %d value %d\n", SDL_GetTicks(), i, k, v); }
            }
            for (k = 0; k < SDL_JoystickNumAxes(js[i]) && k < 16; k++)
            {
                Sint16 v = SDL_JoystickGetAxis(js[i], k);
                if (v / 4096 != ax[i][k] / 4096) { ax[i][k] = v; printf("%8u [%d] axis %d value %d\n", SDL_GetTicks(), i, k, v); }
            }
        }
        SDL_Delay(5);
    }
    SDL_Quit();
    return 0;
}
