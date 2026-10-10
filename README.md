# mupen64plus-input-suxty

A **gamepad-only, in-game menu** for Mupen64Plus on retro handhelds, built as an ordinary Mupen64Plus **input plugin**
(`mupen64plus-input-suxty.so`). You keep your launcher and your front-end (stock `mupen64plus` / `mupen64plus-ui-console`, or
the patched copy a CFW ships), you choose the video, RSP and audio plugins as you always did, and you name this one as the input
plugin. In the game, a button combo opens a menu with save states, reset, controller remapping, a few options and quit,
with no keyboard, mouse or config file to edit.

> **Status:** pre-alpha. The plugin is verified on x86_64 (headless, stock ui-console), on the **RG35XX SP** (spruce's patched
> mupen64plus, foir and Rice) and on the **RK3566 / ROCKNIX** handheld (stock mupen64plus 2.5.9, foir and Rice).
> (Earlier versions were a stand-alone front-end program, `mupen64plus-ui-suxty`; see *History*.)

## What it does

- **Input.** suxty reads the pad through SDL and answers the core's `GetKeys()` itself, through a remappable N64 mapping
  (per pad model, per game; a D-pad / stick swap, a C-Face shift and a Soft Tilt hold button, deadzone / range). Hotkeys and the menu combo are hidden from the game.
- **In-game menu** (Select + Start by default, over a dimmed last frame, pad only): a grid with Save / Load for the chosen slot, Resume, Reset (soft / hard),
  Settings (Controls, FOIR Options with foir, Hotkeys, Volume, options, Apply To, Save Game Profile) and Quit.
- **Hotkeys** (hold Select): R quick-save, L quick-load, Right / Left next / previous slot, Y fast-forward, B (hold) quit.
- **Per-game settings** in `<configdir>/suxty/games/<rom-md5>.cfg`, with an "Apply To: All Games / This Game" switch.

suxty is **not** a launcher, a ROM browser or a front-end: the front-end owns launching, resuming, signals and SIGTERM.

## Install and launch

Build or copy `mupen64plus-input-suxty.so` next to the other plugins, or anywhere, and pass it with `--input`.
It needs only SDL2 (the one the front-end already uses) and the core; nothing else is installed or changed.

**Any front-end** (stock ui-console):

```sh
mupen64plus --gfx mupen64plus-rdp-foir.so --rsp mupen64plus-rsp-hle.so --audio mupen64plus-audio-sdl.so \
            --input /path/to/mupen64plus-input-suxty.so  game.z64
```

**ROCKNIX** (stock `/usr/local/bin/mupen64plus`, which loads plugins from `/usr/local/lib/mupen64plus`; the image is read-only, so keep the
plugin on `/storage`):

```sh
mupen64plus --plugindir /usr/local/lib/mupen64plus --gfx /storage/plugins/mupen64plus-rdp-foir.so \
            --input /storage/plugins/mupen64plus-input-suxty.so  --audio mupen64plus-audio-sdl.so --rsp mupen64plus-rsp-hle.so  game.z64
```

ROCKNIX's launch script (`/usr/bin/start_mupen64plus.sh`, on the read-only image) ends its plugin choices with `--input mupen64plus-input-sdl.so`;
run a copy of that script from `/storage` with `--input /storage/plugins/mupen64plus-input-suxty.so` instead. Its video, RSP and audio choices stay as they are.

**spruce** (RG35XX SP, `/mnt/SDCARD/Emu/N64/mupen64plus/`): copy the plugin into that directory and add `--input /mnt/SDCARD/Emu/N64/mupen64plus/mupen64plus-input-suxty.so`
to `ARGS` in `build_mupen_args` of `spruce/scripts/emu/lib/mupen_functions.sh` (the script passes no `--input`, so the default `mupen64plus-input-sdl.so` is used today), or set
`InputPlugin` in the `[UI-Console]` section of `$HOME/.config/mupen64plus/mupen64plus.cfg`. spruce's own N64 hotkeys (the `CoreEvents` `--set`s) are handled by the core, not by the input plugin; they were not exercised in the tests. While a game runs spruce puts the
kernel D-pad into analog mode (`nds_pwrkey`), so the stickless pad's D-pad arrives as stick axes (also not exercised: the device tests ran with the pad in its normal mode): the menu follows either, and the N64 mapping is yours to set in Controls. suxty's configuration is
`<configdir>/suxty/suxty.cfg` beside `mupen64plus.cfg` (spruce: `$HOME/.config/mupen64plus/`).

**Save states, resume.** suxty never loads anything by itself. To continue where you quit (see *Save and load*), start the game through
your front-end with the state file, e.g. `mupen64plus ... --savestate "<savedir>/<state name>.auto.st" game.z64`.

**Where things are written.** The config directory is the front-end's `--configdir` (default `~/.config/mupen64plus`); states follow the
core's `SaveStatePath` / `${XDG_DATA_HOME}/mupen64plus/save`. When you test, note that on ROCKNIX `HOME=/storage`, so a private `--configdir` alone
still saves states into ROCKNIX's own directory: set `XDG_DATA_HOME`, `XDG_CACHE_HOME` and `HOME` (or `Core[SaveStatePath]`) to a private
directory too.

## Using the menu

| Input | Action |
|---|---|
| **Select + Start** *(configurable)* | Open or close the menu |
| D-pad / left stick | Move focus (in the main menu's grid Left/Right also move between the two columns); Left/Right changes sliders, choices and the slot of "In Slot", and moves along the Save States carousel when it has the focus |
| **A** | Confirm (on a mapping row: start the capture prompt; on "In Slot": open Save States) |
| **B** | Back / close (on Test Controller it is a button to test: hold it 3 s to leave) |
| **X** | On a mapping or hotkey row: clear it. In *This Game* scope, on a row with a green bar: reset it to the global value |
| L / R | Scroll the save-slot carousel |

The main menu is a grid (the window title is the game's name):

```
 Save    | Load           Save and Load act on the slot in "In Slot"; Save asks before overwriting a filled slot
 In Slot 3  (Left/Right: slot, A: the Save States page)
 Resume  | Reset          Reset (soft / hard) and Quit always ask first
 Settings| Quit
```

**Settings** lists: Controls, FOIR Options (only with foir), Hotkeys, Volume (hidden if the core cannot report it), Auto-Save, Confirm Quit, Fast-Forward, FF Speed, Dim,
Notices, Apply To (All Games / This Game: where the Controls, FOIR Options and Settings pages save) and Save Game Profile (writes the settings in force as this game's own and
shows a notice; a Reset Game Profile row appears once the game has any). The list has the same rows in the same order in both scopes: Dim and Notices are global only, so in *This Game*
scope they stay in place, dimmed, and the status line says "Global Only" when one has the focus. Text is drawn in two bitmap faces (8x16 for items, a crisp 12x24 for the hints and status lines),
panels come in four standard widths, every panel keeps a vertical margin of h/24 above and below (a page with more rows shows fewer and scrolls, with a slim scroll bar at its right edge), and the status line
of such a page always has two lines, so a long message wraps instead of making the panel jump. A toast made while the menu is open ("Game profile saved") is shown in that status line (for at least 2.5 s,
then the page's own text is back); outside the menu toasts float over the game as before.

**Wording.** Labels are in title case (Chicago style): page titles, rows and options, values ("On" / "Off", "Hold" / "Toggle", "Auto", "None"), dialog buttons, footer hints ("A Select   B Back"), section headings ("↓ Applies at the Next Launch ↓")
and carousel cards; articles, short conjunctions and short prepositions stay lowercase unless first or last ("Reset to Defaults", "Apply To"), and both parts of a hyphenated word are capitalised ("Auto-Save", "Fast-Forward").
Names and acronyms stay as they are (FOIR, PJ64, N64, D-pad, C-Face, FF, L1 / R1, St) and so does the game's title. Sentences (status-line help, notices, prompts such as "Reset the game?", instruction footers such as "Hold the combo, then release" and
the parenthetical notes under a question, such as "(and its backups)") are in sentence case. The fast-forward badge stays `>> MAX`.

**Select + Start always opens the menu**, even if you set a different menu combo (the *recovery combo*). Hotkeys are combos built on the
**hotkey button** (default Select, written `hotkey`); while it is held the game sees a neutral controller:

| Default combo | Action |
|---|---|
| Hotkey + R | quick-save to the current slot |
| Hotkey + L | quick-load the current slot |
| Hotkey + Right / Left | next / previous slot (the toast shows its last-saved time) |
| Hotkey + Y | fast-forward (hold, or toggle with `fast_forward_mode = toggle`); a badge shows the speed |
| Hotkey + B | quit (saves the auto slot first when that is on); with *Confirm Quit* (default) press it twice within 2 s, a notice follows the first press |

### Save and load (fire and forget)

suxty sends the core's command and does not track it.

- **Save** (menu: Save acts on the slot of "In Slot" and asks before overwriting a filled one; hotkey: at once): `M64CMD_STATE_SAVE` for the slot, the menu closes, toast "Saving slot N...".
  Just before the command suxty puts an **inotify** watch on the state directory (`IN_CLOSE_WRITE | IN_MOVED_TO`, for that file name) and
  polls it once per frame (no thread). The core's worker closes the file when it is done, and that event turns the toast into **"Saved"**; if it has not
  come within 5 s the toast becomes **"Save not written"** (a file that ends up empty counts as not written, too). The events come from the VFS, so this
  is the same on vfat (the spruce SD card) and on ext4 / btrfs, and it is exact where file times are not (vfat: 2 s). Only if inotify cannot be set up
  does suxty fall back to the file's time and size (changed, and unchanged for 300 ms; it logs "no inotify here"). A save is refused up front when the state
  directory is not writable or the disk is nearly full.
- **Load**: `M64CMD_STATE_LOAD`, the menu closes, the core does the rest. The carousel shows each slot's number and the file's time ("Empty"
  if none; no thumbnails). A slot that exists only as a **Project64 state** (`<ROM header name>.pjN[.zip]`) is shown tagged "PJ64" and loaded by path;
  saving writes the Mupen64Plus `.stN` and leaves the `.pj` file alone. The **Auto** card is for information: its Load is greyed (the launcher loads it with `--savestate`, see below) and Delete works on it (and removes its backups).
  The Save States page is a carousel (Left/Right or L/R) over one row of Save / Load / Delete; a Project64 slot says "PJ64" in the dialogs.
- **Reset** (No / Yes = soft, and a full-width "Hard (Power Cycle)"): soft is `M64CMD_RESET 0`, like the console's reset button; hard is `1`. **Quit** (No / Yes, "(Auto-Save Enabled)" when that is on): `M64CMD_STOP`.
- **Auto-save on quit is opt-in** (`[states] autosave_on_quit = false` by default; Settings: "Auto-Save"). When on, Quit first saves the
  separate auto slot file `<state name>.auto.st` (never a numbered slot: spruce's `M64P_AUTOLOAD` loads slot 0), waits for the inotify event
  (the file closed by the core), then stops. If it could not be written the game keeps running (toast "Save not written"; Quit again to retry).
  suxty does not load it: resume with the front-end (`--savestate`, above).

### Auto-save safety net

The core writes a state in place (it truncates the file and rewrites it), so a write that fails or is cut short (battery, a pulled card) would lose the
previous auto-save. So:

1. **Quit** with auto-save on: if `<name>.auto.st` exists, suxty first **renames** it to `<name>.auto.st.prev`, then asks the core to write `<name>.auto.st` in place.
   If a `.prev` is already there (an earlier backup that no game start has cleared yet), `.prev2` and `.prev3` are used, and when all three are taken the oldest goes.
   If the write fails inside the session (5 s without the file being closed), the newest backup is put back, so a failed quit leaves the old auto-save as it was.
2. **At game start** (before the launcher's load, which the core runs once the emulator is going) suxty looks at `<name>.auto.st`:
   - **valid** (the gzip stream decompresses completely with a good CRC and length, it starts with the core's `M64+SAVE` magic and a `1.x` version, and it is exactly
     the size the core writes for that version, 16 793 412 bytes for the current one) -> the new auto-save is good, so **its backups are deleted**;
   - **damaged or missing, and a backup validates** -> the newest valid backup is renamed back to `<name>.auto.st`, the other backups are deleted, and a toast says
     "Auto-save was damaged; previous one restored" ("... was missing; ..." when the file was gone). It is logged as well;
   - damaged and no backup validates -> nothing is touched, the log says so and a toast "Auto-save is damaged" appears.
   The check decompresses the 16.8 MB state once per game start, only when an auto file exists (about 40 ms on a desktop, about 0.4 s on the RG35XX SP; the log says how long it took). It has its own small inflate, so suxty still
   needs SDL2 only.
3. suxty gets no "load succeeded" callback, so "the new file is good" is judged by its contents, not by the load. If the launcher's command line has `--savestate <file>`
   (read from `/proc/self/cmdline`), the log mentions it, but nothing depends on it.
4. Only this game's files (`<name>.auto.st` and `<name>.auto.st.prev*`) are ever touched. **Delete** on the Auto card removes the auto-save and all its backups.

### FOIR Options (foir only)

Settings > **FOIR Options** is shown only when **mupen64plus-rdp-foir** is the video plugin. suxty finds it through a one-way interface that foir exports
(`FoirSuxtyGetInterface`, see foir's README, "suxty interface"): the options come from foir's own table (label, kind, value, and whether a change applies
at once), so the page follows foir's versions. With any other video plugin the page is simply not there.

- **Applies at once:** Widescreen, Smoothing (bilinear filter), Interlace Hack, Frame Skip.
- **Applies at the Next Launch** (listed under a "v Applies at the Next Launch v" heading): VSync, Worker Threads, NEON Kernels, Specialised Kernels, Fast Combiner, Relaxed Precision.
- Where a change is stored follows "Apply To": *All Games* writes `[foir]` in `suxty.cfg`, *This Game* writes `[foir]` in the game's file (keys are the
  `[Video-Foir]` names: `Widescreen = true`, `FrameSkip = Auto`, `Threads = None`). **`mupen64plus.cfg` is never touched** (the front-end saves it).
  At game start, before foir opens the ROM, suxty hands every stored value to foir, so the next-launch options are applied then too.
- **Save as FOIR Default** (asks Yes / No first, since it writes your global `mupen64plus.cfg`) writes the values shown into `[Video-Foir]` (through foir), the only way suxty ever writes that file; foir then uses
  them without suxty too.
- foir found? One scan of the loaded objects (`dl_iterate_phdr` + `dlopen(RTLD_NOLOAD)`) at game start and again when the menu opens; no polling.

### Volume

Settings > **Volume** (0-100, `M64CORE_AUDIO_VOLUME`) is applied at once and stored per scope (`[audio] volume`); X goes back to the audio plugin's own default.
The row is hidden if the core / audio plugin cannot report a volume.

### Controls (controller remap)

Pause menu > **Controls**. Everything applies at once and is saved.

- **Player / Controller.** Choose the player (1-4) and which pad drives it: *Auto* (pads in connection order; keyboards and other devices that merely show up as joysticks are skipped), *None*,
  or a pad by name. A pad that is plugged in for a player that had none when the game started is used from the next launch (the core is told which players exist once).
- **Buttons.** All 14 N64 buttons and the four directions of the analog stick, each with one physical source: a button, one direction of a hat, or one half
  of an axis, named generically (South / East / West / North, DPad Up ..., L1 / R1 / L2 / R2, Start / Select, L-Stick Left ...; "Btn 12" for an unmapped extra). **A** on a row
  opens the **capture prompt** (press the control; 5 s without input cancels), and the status line then says "South → A (was B)": the control, what it drives now, and what it drove before; **X** clears the row. Two more rows are hold buttons:
  **C-Face Shift** (while held, the face buttons act as C-Down / C-Right / C-Left / C-Up and do nothing else) and **Soft Tilt** (while held, the stick output, after deadzone / range and
  including a D-pad that drives the stick, is scaled by `[input] soft_tilt_scale`, default 0.5). A control moves when it is captured for another row.
- **D-pad Acts As: D-pad / Stick.** A software swap of the four stick channels with the four D-pad channels in the mapping, per pad model (and per game). The Buttons page always shows
  what the game gets, so with the swap on the "DPad Up" row lists what the stick has. A pad without a stick starts with its D-pad as the stick.
- **Reset to Default Mapping** (asks first; keeps deadzone and range): left stick = stick, D-pad = D-pad, right stick = C; a pad with one stick gets a C-Face Shift button (a spare trigger or stick click), one with none has its D-pad as the stick.
- **Stick Deadzone / Range** in percent of each half axis (default 12 % and 100 %); **Test Controller** shows what the game would get: the D-pad and C arrows (↑ ↓ ← →) and, on a row of their own, the N64 buttons (A B Z St L R, the N64's own names), then the stick, C-Face and Soft Tilt. Every arrow and button is always shown in its own fixed place, dim while up and lit while pressed, so nothing shifts as inputs change. The page has no footer: its bar says "Hold B 3 Sec. to Go Back" and fills while B is held (3 s).
- The mapping belongs to the **pad model** (its SDL name), so it follows the pad when players are swapped.

Settings > **Hotkeys** assigns the hotkey button, the menu combo, quick-save / -load, slots, fast-forward and quit (**A** starts a capture, 7 s idle cancels: hold the buttons together, let go; **X** clears; **Reset All**).
Lock-out protection: Select + Start (`recovery_combo`) always opens the menu; a new menu combo or hotkey button must be proven by using it within 10 s or the old one comes back; overlapping combos
are refused. Settings also has the menu look (Dim, Notices), Auto-Save, Confirm Quit, Fast-Forward mode and FF Speed. Hints are always on. `swap_ab` (the marked A button confirms in the
menu, for launchers such as spruce) is a file-only option under `[input]`.

### Per-game settings

Every setting has a *global* value (`suxty.cfg`) and may have a *game* value in `<configdir>/suxty/games/<rom-md5>.cfg` (the ROM's MD5, so a `.z64`, `.v64` and `.n64` dump share a file). The game wins;
"Reset to Global" (X on a green row, or *Settings > Reset Game Profile*) deletes the entry. **Save Game Profile** (Settings) copies every per-game-capable setting in force (auto-save, Confirm Quit,
hotkey combos, fast-forward, Soft Tilt, volume, controller choice and mappings, foir's options; not the hotkey button and the menu combo) into the game's file. In the lists a green bar marks a row the game has its own value for
(in *This Game* scope), an olive bar one it has while you look at the global value; the status line says which when the row has the focus. What can be per game: those settings. The menu's look, Swap A/B, nav deadzone and the recovery combo are global only. The core's and the other plugins' options are not suxty's business: use your config / front-end.

## Configuration

```
mupen64plus.cfg          # the core's and the plugins' settings: owned by the front-end, never written by suxty
suxty/
  suxty.cfg              # menu, hotkeys, auto-save, controller mappings, [foir] and [audio] (the global layer)
  games/<rom-md5>.cfg    # per-game overrides, layered over the globals; deleted when it has no entry left
```

```ini
[general]
version = 3
[menu]
menu_combo = select+start        # buttons joined with '+'; the word "hotkey" stands for hotkey_button
dim_percent = 65
show_toasts = true
toast_ms = 1800
recovery_combo = select+start    # ALWAYS opens the menu too ("none" disables, at your own risk)
[input]
swap_ab = false                  # hidden (file only): swap confirm / back in the menu; a launcher sets it where the marked A button should confirm
soft_tilt_scale = 0.5            # stick strength while the Soft Tilt button is held (0.0-1.0); no menu row
nav_deadzone = 50
block_game_input = true          # hide hotkey/menu buttons from the game (recommended)
[hotkeys]
hotkey_button = select           # while held, the game sees a neutral controller
quick_save = hotkey+r            # empty = disabled
quick_load = hotkey+l
next_slot = hotkey+right
prev_slot = hotkey+left
fast_forward = hotkey+y
quit = hotkey+b
fast_forward_mode = hold         # or toggle
fast_forward_speed = 300         # percent (50-1000), 0 = unlimited
confirm_quit = true              # the quit combo must be pressed twice within 2 s (a notice after the first)
[states]
autosave_on_quit = false         # opt-in: Quit saves <state name>.auto.st first
[audio]
volume = 70                      # optional; absent = whatever the audio plugin starts with
[foir]                           # optional; the foir options by their [Video-Foir] names
Widescreen = true
FrameSkip = Auto
[players]                        # which pad drives which N64 player: auto | none | <pad name>
p1 = auto
[pad:retrogame_joypad]           # the mapping of one pad model (written by Controls in the menu)
deadzone = 12
dpad_swap = false                # true: the D-pad channels and the stick channels are exchanged ("D-pad Acts As: Stick" on a pad whose D-pad is on the D-pad rows)
cface = button:7                 # the C-Face shift hold button: none | button:N | hat:N:up|down|left|right | axis:N+|-
cface_c_up = button:3            # ... while it is held these controls act as C-Up / -Down / -Left / -Right (the face buttons)
softtilt = none                  # the Soft Tilt hold button
a = button:0                     # one key per N64 input: a b z start l r dpad_* c_* stick_*
```

**Upgrading.** Version 3 (the menu refinement pass) migrates old files when they load, in `suxty.cfg` and in the game files alike, and writes them back clean:
`[hotkeys] quit_hold_ms` becomes `confirm_quit` (0 = *Instant* = false, any other time = true; an explicit `confirm_quit` wins) and is removed; `[menu] show_hints` is removed (hints are always on);
in the `[pad:...]` sections a `modifier` whose layer mapped the C buttons (`mod_c_*`, the old "Face buttons = C") becomes `cface` / `cface_c_*` (the C-Face shift), any other modifier layer (`modifier`, the rest of `mod_*`)
is dropped, and the `preset` name is dropped. `swap_ab` keeps working as a file-only key. Unknown keys are kept.

**Upgrading from the front-end build.** Old files load without error: the keys and sections that no longer mean anything (`[input] remap_input`, `[plugins]`, `[cfg:...]`, `[UI-Suxty]`)
are dropped on load, unknown keys are kept, and `autosave_on_quit` goes back to its new default (off), because it now means something different.

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

Tokens run one per tick (a presented game frame, or a menu frame while the menu is open): `waitN`, `menu`, `shot`, `quit` (like the menu's Quit), `kill`, `stat`, the buttons
`up down left right a b x y l r start select`, combos (`select+r`), `holdN:select+y`, `cap:button:3` / `cap:hat:0:up` / `cap:axis:2+` (answer the remap capture prompt), `pjsave:N` / `pjzip:N`
(write a Project64 state with the core), `holdN@K:b` (a hold that also takes a shot on tick K), `probe:a+b+cup+shift+tilt+sx60+sy-80` / `probe:off` (what the Test controller page shows, for layout checks). Example (x86, headless, stock ui-console):

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy SUXTY_DEBUG_SHOT_PREFIX=/tmp/shot \
SUXTY_DEBUG_SCRIPT="wait650,menu,wait3,down,down,down,down,a,wait3,a,wait3,shot,quit" \
mupen64plus --configdir /tmp/cfg --gfx mupen64plus-rdp-foir.so --input ./mupen64plus-input-suxty.so ... game.z64
```

## How it works

- **Call order** (ui-console): `M64CMD_ROM_OPEN`, `PluginStartup` of every plugin, `CoreAttachPlugin` x4 (gfx, audio, input = `InitiateControllers`, rsp), `M64CMD_EXECUTE`
  (the plugins' `RomOpen`, then the emulation thread = the thread that called EXECUTE), `RomClosed`, detach, `PluginShutdown`. suxty installs its video extension from
  `PluginStartup` (before any video plugin calls `VidExt_*`), and loads the game's settings in `InitiateControllers` (the ROM is open then, the video plugin has not opened it).
- **Video hook.** `CoreOverrideVidExt()` makes suxty own the SDL2 window, the GL context and the buffer swap. The game renders in its own context; the menu draws in a **second GL context on
  the same window**, so the video plugin's GL state is never touched. Every swap polls the pad state; when the combo fires, suxty reads back the frame about to be shown, pauses the core
  (`M64CMD_PAUSE`) and runs the menu as a nested loop inside that swap call (own event pumping and presenting at ~60 Hz), then resumes. No second thread, no locking: see `src/session.h`.
  Toasts and the fast-forward badge are drawn over the finished frame from the overlay context just before the swap.
- **Input.** `GetKeys()` runs on the same thread: `SDL_JoystickUpdate`, then the hotkey filter (`src/inputfilter.c`: the hotkey button, or the open menu, make the pad neutral for the game, and a button pressed
  in that time stays hidden until released, so not even one frame leaks), then the N64 mapping (`src/padmap.c`, `src/remap.c`). No SDL function is intercepted.
- **FOIR Options page.** `src/foir_if.c`: discovery, option layering, and the page's logic; `src/menu_av.c`: the FOIR Options pages. `src/foir_suxty.h` is a copy of foir's header (ABI version 1; another ABI counts as "no foir").
- **Saves.** `src/states.c`: slot file names; `src/filewatch.c`: the inotify watch (with a stat fallback); `src/autosave.c`: auto-save validation, backups and the start check; `src/session.c`: ties them to the core commands.
- **Widgets** (`src/ui.c`, `src/gl.c`, `src/uinav.c`): a small own-drawn immediate-mode set with grid-aware D-pad focus navigation (`uinav.c` and the text formatting of `uitext.c` are pure functions with their own tests), SDL2 and GLES only, scaling from 320x240 to 1280x720 and beyond; the 12x24 hint face and the arrow glyphs come from `tools/gen_font.py`.
- The shared object exports only the plugin API (`-fvisibility=hidden`, `-Bsymbolic`, `--exclude-libs,ALL`), so it can neither interpose nor be interposed.

## Building

```sh
cd projects/unix
make all                 # -> mupen64plus-input-suxty.so (needs SDL2 headers and the Mupen64Plus core API headers, ../../../mupen64plus-core/src/api is found automatically)
make check               # unit tests (ASan/UBSan): the N64 mapping model (D-pad swap, C-Face shift, Soft Tilt), the config layers and the old-config migration, the menu grid navigation, the menu text formatting (status-line wrapping, Test controller cells, capture message), the double-press quit logic, foir discovery and option layering against a fake foir,
                         # the auto-save validation / backup rotation / start check and the inotify watcher (needs zlib headers, for the test only)
make install PREFIX=...  # to $(PREFIX)/lib/mupen64plus (or PLUGINDIR=)
```

Cross-building for aarch64: `tools/build-aarch64.sh [dir with the device's libSDL2-2.0.so.0]`, or pass `CROSS_COMPILE=aarch64-linux-gnu- POSTFIX=-aarch64 SDL_CFLAGS=... SDL_LDLIBS=...`.
The stock Ubuntu cross compiler emits glibc 2.38 symbols; for older devices (spruce: glibc 2.35) use the workspace's `toolchains/rg35xxsp/build.sh suxty`, whose output (glibc <= 2.17 symbols) runs on both handhelds.
The only library dependency is SDL2; GL / GLES entry points are resolved at run time through `SDL_GL_GetProcAddress`.

## Known gaps

- **Rumble and other paks:** only the core's own Controller Pak (`PLUGIN_MEMPAK`) is offered, no Rumble Pak / Transfer Pak, and there is no keyboard / mouse input for the game.
- The game-input filter only covers pads that suxty reads (all SDL joysticks); keyboard input is never filtered. With `block_game_input = false` the menu waits for the buttons to be released instead and the
  game sees the hotkeys.
- Menu and hotkey combos are only evaluated once per presented frame (a press shorter than a frame can be missed; a game that stops presenting frames cannot be quit with the hotkey).
- Only OpenGL / GLES video plugins work (no Vulkan render mode). Rice was checked on ROCKNIX and spruce, GLideN64 on the earlier spike; Rice on ROCKNIX logs "GL error while capturing the game frame" once
  (the menu still opens and draws).
- **Saves:** the core leaves its state-file lock held if writing fails, which would hang the next load; suxty refuses to start a save when the state directory is not writable or has less than 24 MB free, but cannot cover a card that fails mid-write.
  A save that the core never starts (for instance a directory at the slot's path) is reported as "Save not written" after 5 s. The auto-save backup is judged at the next game start (see *Auto-save safety net*), not by the launcher's load.
- A video plugin other than foir gets no FOIR Options page, and the core's / other plugins' options are not editable here.
- Per-game mappings are overridden per pad model as a whole; there is no per-key marker. Each N64 input has one source; there is no octagonal gate or sensitivity curve.
- Fast-forward with audio: the core's speed factor changes audio speed, `fast_forward_speed = 0` (limiter off) can make `audio-sdl` skip.

## History

M1-M4 were a stand-alone front-end (`suxty game.z64`) that loaded the core itself, with per-game plugin / RSP / core profiles, "Restart game", `--resume`, signal handling and an interposition layer over the SDL
joystick functions. It is now this input plugin; those parts are gone (the front-end does them), and the git history has the old program.

## License

**GPLv2 or later**, matching the Mupen64Plus core.
