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
- While a game runs spruce puts the kernel D-pad into analog mode (`nds_pwrkey`), so the stickless pad's D-pad arrives as stick axes. The menu
  follows either, and the N64 mapping is yours to set in Controls. This was not exercised: the device tests ran with the pad in its normal mode.
- If the marked A button should confirm in the menu (a launcher whose "A" is the other face button), set the file-only option `[input] swap_ab = true`
  in `suxty.cfg` (see [configuration.md](configuration.md)).

## Resuming a game

suxty never loads a state by itself. To continue where you quit (auto-save on, see [saves.md](saves.md)), have the launcher start the game with the state file:

```sh
mupen64plus ... --savestate "<savedir>/<state name>.auto.st" game.z64
```

## Where things are written

The config directory is the front-end's `--configdir` (default `~/.config/mupen64plus`); states follow the core's `SaveStatePath`, by default
`${XDG_DATA_HOME}/mupen64plus/save`. When testing on ROCKNIX, `HOME=/storage`, so a private `--configdir` alone still saves states into ROCKNIX's own
directory: set `XDG_DATA_HOME`, `XDG_CACHE_HOME` and `HOME` (or `Core[SaveStatePath]`) to a private directory too.
suxty never writes `mupen64plus.cfg`, except for the explicit "Save as FOIR Default" action (see [saves.md](saves.md)).
