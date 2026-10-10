# Development

## Layout and how it works

- **Call order** (ui-console): `M64CMD_ROM_OPEN`, `PluginStartup` of every plugin, `CoreAttachPlugin` x4 (gfx, audio, input = `InitiateControllers`, rsp), `M64CMD_EXECUTE`
  (the plugins' `RomOpen`, then the emulation thread = the thread that called EXECUTE), `RomClosed`, detach, `PluginShutdown`. suxty installs its video extension from `PluginStartup`
  (before any video plugin calls `VidExt_*`) and loads the game's settings in `InitiateControllers` (the ROM is open then; the video plugin has not opened it).
- **Video hook.** `CoreOverrideVidExt()` makes suxty own the SDL2 window, the GL context and the buffer swap. The game renders in its own context; the menu draws in a **second GL context on the same window**,
  so the video plugin's GL state is never touched. Every swap polls the pad state; when the combo fires, suxty reads back the frame about to be shown, pauses the core (`M64CMD_PAUSE`) and runs the menu as a nested
  loop inside that swap call (own event pumping, presenting at about 60 Hz), then resumes. No second thread, no locking (see `src/session.h`). Toasts and the fast-forward badge are drawn over the finished frame just before the swap.
- **Input.** `GetKeys()` runs on the same thread: `SDL_JoystickUpdate`, then the hotkey filter (`src/inputfilter.c`: the hotkey button or the open menu makes the pad neutral for the game, and a button pressed in that time stays hidden until released),
  then the N64 mapping (`src/padmap.c`, `src/remap.c`). No SDL function is intercepted.
- **Saves.** `src/states.c` slot file names; `src/filewatch.c` inotify watch (with a stat fallback); `src/autosave.c` validation, backups and the start check; `src/session.c` ties them to the core commands.
- **FOIR.** `src/foir_if.c` discovery, option layering and page logic; `src/menu_av.c` the FOIR Options pages; `src/foir_suxty.h` is a copy of foir's interface header (ABI 1; another ABI counts as "no foir").
- **UI.** `src/ui.c`, `src/gl.c`, `src/uinav.c`: a small own-drawn immediate-mode widget set with grid-aware D-pad focus navigation; SDL2 and GLES only, scaling from 320x240 to 1280x720 and beyond. `uinav.c` and `uitext.c` (text formatting) are pure functions with their own tests.
  Two bitmap faces (8x16 for items, 12x24 for hints and status lines) come from `tools/gen_font.py`; panels have four standard widths with a vertical margin of h/24, long pages scroll, and the status line of a scrolling page always has two lines so the panel does not jump.
  A toast made while the menu is open appears in the status line (at least 2.5 s); outside the menu toasts float over the game.
- The shared object exports only the plugin API (`-fvisibility=hidden`, `-Bsymbolic`, `--exclude-libs,ALL`), so it can neither interpose nor be interposed. GL / GLES entry points are resolved at run time through `SDL_GL_GetProcAddress`; SDL2 is the only library dependency.
- Menu and hotkey combos are evaluated once per presented frame, so a game that stops presenting frames cannot be quit with the hotkey.

## Building and tests

```sh
cd projects/unix
make all                 # -> mupen64plus-input-suxty.so
make check               # unit tests (ASan / UBSan), needs zlib headers for the test only
make install PREFIX=... # to $(PREFIX)/lib/mupen64plus (or PLUGINDIR=)
```

`make check` covers the N64 mapping model (D-pad swap, C-Face shift, Soft Tilt), the config layers and old-config migration, menu grid navigation, menu text formatting (status-line wrapping, Test Controller cells, capture message),
the double-press quit logic, foir discovery and option layering against a fake foir (`tools/fake_foir.c`), and the auto-save validation, backup rotation, start check and inotify watcher.

Cross builds: `tools/build-aarch64.sh [dir with libSDL2-2.0.so.0]`, or pass `CROSS_COMPILE=aarch64-linux-gnu- POSTFIX=-aarch64 SDL_CFLAGS=... SDL_LDLIBS=...`.

## Debug scripting (headless tests)

Environment variables, read once when the game starts:

| Variable | Meaning |
|---|---|
| `SUXTY_DEBUG_SCRIPT` | e.g. `wait650,menu,wait3,down,a,shot,quit` |
| `SUXTY_DEBUG_SHOT_PREFIX` | where `shot` writes PNGs (`prefix-1.png`, ...) |
| `SUXTY_DEBUG_OPEN_AFTER` | open the menu after N presented frames |
| `SUXTY_VERBOSE` / `SUXTY_LOG_KEYS` | verbose log / log every change of `GetKeys()` |
| `SUXTY_KEYBOARD` | also accept the keyboard in the menu (desktop) |
| `SUXTY_GL` | `es` / `compat` / `plugin`: force the GL context profile |

Tokens (comma or space separated) run one per tick (a presented game frame, or a menu frame while the menu is open): `waitN`, `menu`, `shot`, `quit` (like the menu's Quit), `kill`, `stat` (log frame counter and clock),
the buttons `up down left right a b x y l r start select`, combos (`select+r`), `holdN:select+y`, `holdN@K:b` (a hold that also takes a shot on tick K), `cap:button:3` / `cap:hat:0:up` / `cap:axis:2+` (answer the remap capture prompt),
`pjsave:N` / `pjzip:N` (write a Project64 state with the core), and `probe:a+b+cup+shift+tilt+sx60+sy-80` / `probe:off` (what the Test Controller page shows, for layout checks).

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy SUXTY_DEBUG_SHOT_PREFIX=/tmp/shot \
SUXTY_DEBUG_SCRIPT="wait650,menu,wait3,down,down,down,down,a,wait3,a,wait3,shot,quit" \
mupen64plus --configdir /tmp/cfg --gfx mupen64plus-rdp-foir.so --input ./mupen64plus-input-suxty.so ... game.z64
```

## Test tools (`tools/`)

Not built by the Makefile; the build line is in each file's header.

- `vpad.c`: scripted virtual gamepad (Linux uinput) for rigs where nobody can press buttons.
- `padinfo.c`: prints what SDL reports for every joystick and logs raw input changes; used to bring suxty up on a new handheld CFW.
- `testinput/testinput.c`: a stock-like input plugin that logs everything SDL shows it (the hotkey filter must leave no activity in the log while the hotkey is held); `TESTINPUT_WRAP` wraps the stock input plugin and logs the N64 state it produces.
- `testgfx/testgfx.c`: a tiny video plugin that draws a triangle and prints "TESTGFX: GL STATE CORRUPTED" if the menu ever disturbs the plugin's GL state.
- `gen_font.py`: regenerates `src/font_data.c` from DejaVu Sans Mono Bold.

## Wording rules

Labels are in title case (Chicago style): page titles, rows and options, values ("On" / "Off", "Hold" / "Toggle", "Auto", "None"), dialog buttons, footer hints ("A Select   B Back"), section headings ("↓ Applies at the Next Launch ↓")
and carousel cards. Articles, short conjunctions and short prepositions stay lowercase unless first or last ("Reset to Default Mapping", "Apply To"); both parts of a hyphenated word are capitalised ("Auto-Save", "Fast-Forward").
Names and acronyms stay as they are (FOIR, PJ64, N64, D-pad, C-Face, FF, L1 / R1, St), and so does the game's title. Sentences (status-line help, notices, prompts such as "Reset the game?", instruction footers such as
"Hold the combo, then release", and parenthetical notes under a question such as "(and its backups)") are in sentence case. The fast-forward badge stays `>> MAX`.

## Known gaps

- Only the core's Controller Pak is offered; no Rumble Pak / Transfer Pak, no keyboard / mouse input for the game.
- Only OpenGL / GLES video plugins (no Vulkan render mode). Rice was checked on ROCKNIX and spruce; on ROCKNIX it logs "GL error while capturing the game frame" once (the menu still opens and draws).
- Each N64 input has one source; there is no octagonal gate or sensitivity curve.
- The core's and other plugins' options are not editable in the menu.
