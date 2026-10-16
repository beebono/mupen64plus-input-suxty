# Launchers and setup

suxty is loaded like any input plugin; nothing else is installed or changed. Copy `mupen64plus-input-suxty.so` next to the other
plugins, or anywhere, and pass it with `--input`. It needs only SDL2 and the core.

## Stock mupen64plus / ui-console

```sh
mupen64plus --gfx mupen64plus-rdp-foir.so --rsp mupen64plus-rsp-hle.so --audio mupen64plus-audio-sdl.so \
            --input /path/to/mupen64plus-input-suxty.so  game.z64
```

## ROCKNIX

The stock `/usr/local/bin/mupen64plus` loads plugins from `/usr/local/lib/mupen64plus`. The image is read-only, so keep the plugin on `/storage`:

```sh
mupen64plus --plugindir /usr/local/lib/mupen64plus --gfx /storage/plugins/mupen64plus-rdp-foir.so \
            --input /storage/plugins/mupen64plus-input-suxty.so --audio mupen64plus-audio-sdl.so --rsp mupen64plus-rsp-hle.so  game.z64
```

ROCKNIX's launch script (`/usr/bin/start_mupen64plus.sh`, on the read-only image) ends its plugin choices with `--input mupen64plus-input-sdl.so`.
Run a copy of that script from `/storage` with `--input /storage/plugins/mupen64plus-input-suxty.so` instead; its video, RSP and audio choices stay as they are.

## spruce (RG35XX SP)

The standalone mupen64plus lives in `/mnt/SDCARD/Emu/N64/mupen64plus/`.

- Copy the plugin into that directory and add `--input /mnt/SDCARD/Emu/N64/mupen64plus/mupen64plus-input-suxty.so` to `ARGS` in
  `build_mupen_args` of `spruce/scripts/emu/lib/mupen_functions.sh` (the script passes no `--input`, so `mupen64plus-input-sdl.so` is the default today).
  Alternatively set `InputPlugin` in the `[UI-Console]` section of `$HOME/.config/mupen64plus/mupen64plus.cfg`.
- suxty's configuration is `<configdir>/suxty/suxty.cfg` beside `mupen64plus.cfg` (spruce: `$HOME/.config/mupen64plus/`).
- spruce's own N64 hotkeys (the `CoreEvents` `--set`s) are handled by the core, not by the input plugin; they were not exercised in the tests.
- **Game Switcher and "Emulator menu" button.** spruce's button handler finds the emulator with `pgrep -f "\./mupen64plus"`, so the launcher must start it as `./mupen64plus`
  from the directory that holds it (`cd /mnt/SDCARD/Emu/N64/mupen64plus && ./mupen64plus ...`); an absolute path does not match and the buttons then do nothing.
  Then, with `handle_signals` on (the default, see [configuration.md](configuration.md)):
  - the **Emulator menu** button sends `killall -USR2 mupen64plus`: suxty opens its menu, and a second press closes it. (Without suxty nothing in the stock core or foir handles `SIGUSR2`, and it ends the process.)
  - the **Game Switcher** sends `killall -USR1 mupen64plus`, and `killall -15 mupen64plus` two seconds later. spruce's core would save to the *current* slot, but its autoload only ever loads slot 0,
    so a game left in another slot came back stale and a manual slot-0 save was overwritten. suxty replaces the core's handler (at the first frame): `SIGUSR1` closes the menu if it is open, writes
    `<state name>.auto.st` the way Quit does (`.prev` backup, the core's own writer) and stops the emulator as soon as the core has begun the write; the core finishes it while it shuts down. It does this whether or not
    Auto-Save is on, and never writes a numbered slot. On the RG35XX SP the exit takes about 1.2 s (measured in [saves.md](saves.md#signals-and-resuming)), inside spruce's two seconds.
  - **`M64P_AUTOLOAD=1`** (exported by spruce's launcher for a Game Switcher resume) means "resume from the auto-save". suxty removes the variable at start-up so the core does not load slot 0, checks `<state name>.auto.st`
    like any game start, and loads it on the first frame. With no usable auto-save the game starts fresh (slot 0 is not a fallback). If the launcher also passes `--savestate`, that file wins and the variable is ignored.
  - A launcher that does not export `M64P_AUTOLOAD` (a normal launch from the game list) starts the game fresh, as before.
- While a game runs spruce puts the kernel D-pad into analog mode (`nds_pwrkey`), so the stickless pad's D-pad arrives as stick axes. The menu
  follows either, and the N64 mapping is yours to set in Controls. This was not exercised: the device tests ran with the pad in its normal mode.
- If the marked A button should confirm in the menu (a launcher whose "A" is the other face button), set the file-only option `[input] swap_ab = true`
  in `suxty.cfg` (see [configuration.md](configuration.md)).

## Resuming a game

Apart from `M64P_AUTOLOAD` (above), suxty never loads a state by itself. To continue where you quit (auto-save on, see [saves.md](saves.md)), have the launcher start the game with the state file:

```sh
mupen64plus ... --savestate "<savedir>/<state name>.auto.st" game.z64
```

## Where things are written

The config directory is the front-end's `--configdir` (default `~/.config/mupen64plus`); states follow the core's `SaveStatePath`, by default
`${XDG_DATA_HOME}/mupen64plus/save`. When testing on ROCKNIX, `HOME=/storage`, so a private `--configdir` alone still saves states into ROCKNIX's own
directory: set `XDG_DATA_HOME`, `XDG_CACHE_HOME` and `HOME` (or `Core[SaveStatePath]`) to a private directory too.
suxty never writes `mupen64plus.cfg`, except for the explicit "Save as Video Default" and "Save as Audio Default" action (see [saves.md](saves.md)).
