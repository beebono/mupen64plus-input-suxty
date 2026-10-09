# mupen64plus-ui-suxty

A **gamepad-only, in-game front-end** for Mupen64Plus on retro handhelds.
Your launcher (EmulationStation, muOS, Knulli, …) starts suxty with a ROM
path. suxty runs the game and gives you a clean pause menu for save states,
controller remapping and per-game settings, with no keyboard, mouse or config
files to edit.

> **Status:** pre-alpha. **M1 (boot & pause), M2 (save states), M3 (controller remap UI) and M4 (per-game profiles) are
> implemented**: suxty runs a ROM, owns the window/GL context via a VidExt override, and opens a gamepad-driven pause menu
> (Resume / Save States / Reset / Controls / Video / Audio / Settings / Quit) over a dimmed last frame. M2 adds the
> save-state carousel, quick-save / quick-load / slot / fast-forward / quit hotkeys that are hidden from the game, toasts,
> and auto-save on quit with `--resume`. M3 adds on-device N64 button mapping with a capture prompt, presets, a modifier
> layer, analog deadzone / range, controller selection for players 1-4 and hotkey assignment with lock-out protection; all
> of it takes effect immediately, with the stock `mupen64plus-input-sdl`. M4 adds a **global layer and a per-game layer**
> (`<configdir>/suxty/games/<rom-md5>.cfg`) for video-plugin / audio / emulation options, controller mappings, hotkeys and
> suxty's own options, with "Apply to: All games / This game" and "Reset to global" in the menu, and makes save slots that
> only exist as **Project64 states** (`.pjN`) show up, and load, in the carousel.

---

## Scope

suxty **is**:
- a Mupen64Plus front-end application, the same kind of program as
  `mupen64plus-ui-console`
- an in-game overlay menu that you drive entirely with a gamepad

suxty **is not**:
- a ROM browser or launcher; your distro's launcher already does that
- a Mupen64Plus *plugin* (front-ends load the core and the plugins)

## Target platforms

| Distro | Display stack |
|---|---|
| ROCKNIX / JELOS | SDL2 KMSDRM, GLES (Panfrost / Mali). *Current ROCKNIX nightlies (verified on an RK3566 device) run Sway, and SDL2 uses the **Wayland** driver (`SDL_VIDEODRIVER=wayland`, set by `/etc/profile`)*; suxty works with whichever driver SDL picks |
| Knulli / Batocera | SDL2 KMSDRM, GLES (H700 and others) |
| muOS / ArkOS | SDL2 KMSDRM, GLES; must tolerate older ArkOS SDL2 builds |

Desktop Linux builds are possible for development, but they are not a target.

---

## Usage

```sh
suxty /path/to/game.z64
suxty --resume /path/to/game.z64     # load the auto-save state, if one exists
```

Point your launcher's N64 entry at `suxty %ROM%`.

Useful options (see `suxty --help`; most mirror `mupen64plus-ui-console`):

```sh
suxty --configdir DIR --datadir DIR --plugindir DIR   # core/plugin/config locations
suxty --gfx mupen64plus-video-GLideN64.so --rsp mupen64plus-rsp-hle.so \
      --audio mupen64plus-audio-sdl.so --input mupen64plus-input-sdl.so game.z64
suxty --set Video-General[ScreenWidth]=1024 game.z64   # any core/plugin config parameter
suxty --resume game.z64                                # continue from the auto-save written when you last quit
```

Per-game settings need no command-line option: suxty looks the ROM's MD5 up and layers
`<configdir>/suxty/games/<md5>.cfg` over the global configuration (see *Profiles & per-game settings*). A command-line
`--gfx/--audio/--input/--rsp/--set/--emumode/...` always wins over the game file.

`--resume` loads the dedicated auto-save slot (`<state name>.auto.st`, written when you quit with
auto-save on). If there is none yet the game just starts normally.

### Testing without a gamepad

The menu can be driven by a script, which is how it is tested on a rig where nobody
can press buttons:

```sh
suxty --debug-shot-prefix /tmp/shot \
      --debug-script "wait300,shot,menu,wait10,shot,down,wait5,shot,b,wait30,quit" game.z64
```

Tokens run one per tick (a presented game frame, or a menu frame while the menu is open):
`waitN`, `menu` (toggle), `shot` (PNG of the composited back buffer, including toasts), `quit`
(like the menu's Quit: auto-saves first), `kill` (stop at once), `stat` (log frame counter and clock),
and the buttons `up down left right a b x y l r start select`. Buttons can be combined for one tick
(`select+r`, i.e. a hotkey combo) or held (`hold120:select+y`). `--debug-job-delay MS` makes save/load
jobs look slower so the "Saving..." page can be photographed. `--debug-open-menu-after N` is a
shorthand for `wait N, menu`. `--keyboard` additionally lets arrows / Z / X / Enter /
Right-Shift drive the menu on a desktop. `tools/vpad.c` creates a scripted *virtual
gamepad* through uinput for end-to-end tests of the real controller path,
`tools/testgfx/` is a tiny video plugin for exercising the VidExt override on a desktop (it raises the RDP
interrupt, so games keep running), and `tools/testinput/` is an input plugin that logs everything it can see
of the controller, to prove that hotkey presses do not reach the game. With `TESTINPUT_WRAP=/path/to/mupen64plus-input-sdl.so`
it instead loads the *stock* input plugin and logs the N64 controller state that plugin produces
(`TESTINPUT: N64[0] buttons=0x0080 (A) stick=(0,0)`), which is how the remap is tested end to end.
`restart` quits like the menu's "Restart game" (auto-saves, exits, starts again with `--resume`; `--debug-script-restart "..."` is
the script the restarted run uses, and its screenshots get the prefix `<prefix>-restarted`), and `pjsave:N` / `pjzip:N` make the core write a Project64
state (uncompressed / zip) to slot N's `.pjN` file, to test the PJ64 slots. `tools/vpad.c` can also create a pad with sticks and triggers (`VPAD_AXES=1`; tokens `lx ly rx ry lt rt` take
-32768..32767) and a second one with another name (`VPAD_NAME=`). The remap UI's capture prompt can be answered from a
script too: `cap:button:3`, `cap:hat:0:up`, `cap:axis:2+` stand in for pressing that control.
`make -C projects/unix check` runs the unit tests of the mapping model (`tools/test_padmap.c`) and of the config layers
(`tools/test_cfg.c`: game-over-global lookups, scope writes, reset, save/reload), both under ASan/UBSan.

---

## Architecture

```
            ┌─────────────────────── suxty ────────────────────────┐
 launcher ─▶│ args / config ─▶ load core + plugins ─▶ M64CMD_EXECUTE│
            │                                                      │
            │ VidExt override (CoreOverrideVidExt)                 │
            │  ├─ owns the SDL2 window + GLES context               │
            │  └─ SwapBuffers: game frame ─▶ [overlay] ─▶ present  │
            │                                                      │
            │ Menu: SDL2 own-drawn immediate-mode widgets          │
            │  └─ actions via M64CMD_PAUSE / STATE_* / ROM_GET_*   │
            └──────────────────────────────────────────────────────┘
```

- **Core and plugin loading** works like ui-console: suxty loads
  `libmupen64plus`, then the video, audio, input and RSP plugins, then runs the ROM.
- **VidExt override.** suxty calls `CoreOverrideVidExt()`
  (`mupen64plus-core/src/api/m64p_frontend.h`) so it owns the window, the GL
  context and the buffer swap. That lets it draw the menu *over* the game
  with any video plugin. It pairs especially well with
  [mupen64plus-rdp-foir](../mupen64plus-rdp-foir), which presents a single texture.
- **Save states and jobs.** The core only runs state jobs from its interrupt handler, i.e. while the
  emulator runs (and a save finishes later on its worker thread), so a save/load is a *job* that
  outlives the call that queued it. The menu closes and resumes the core; the frame hook completes the
  job (shows a toast, or re-opens the menu on the carousel with "Saving slot N..." until a menu-initiated
  save is done). A load is done at `M64CORE_STATE_LOADCOMPLETE`. For a save, that state callback is not
  enough: the Mupen64Plus 2.5.9 core that ROCKNIX ships sends `SAVECOMPLETE` as soon as the save is
  *queued*, newer cores when it is *written*. Both have the writer thread announce the outcome in a status
  message ("Saved state to: <file>"), so suxty waits for that message naming its own file. The auto-save
  is written to `<file>.tmp` and renamed on success, so a failed write never destroys the previous one.
- **Hotkeys are hidden from the game.** The input plugin and suxty read the same controller, so
  suxty overrides the four SDL joystick read functions the input plugin uses (`SDL_JoystickUpdate`,
  `GetButton`, `GetAxis`, `GetHat`; exported from the executable, forwarded with `dlsym(RTLD_NEXT)`).
  While the hotkey button is held, or the menu is open, the game sees a neutral controller, and a button
  pressed in that time stays hidden until it is released. It works with stock `mupen64plus-input-sdl`,
  needs no root and no change to the plugin. See `src/inputfilter.h`.
- **Pause menu.** Opening the menu sends `M64CMD_PAUSE`. The last game frame
  stays on screen, dimmed, behind the menu.
  *How it works (M1):* `M64CMD_EXECUTE` runs the emulator on suxty's main thread, and the
  video plugin presents frames through `VidExt_GL_SwapBuffers`, i.e. through suxty. Each
  swap polls the pad *state*; when the combo fires, suxty reads back the frame about to
  be shown, pauses the core and runs the menu as a nested loop inside that swap call
  (own SDL event pumping, own redraw/present at ~60 Hz), then resumes. No second
  thread is needed, and nothing is consumed from the SDL event queue, so the input
  plugin and suxty do not fight over the pad. The menu draws in a *second GL context on
  the same window*, so the video plugin's GL state is never touched. See the comment
  block in `src/session.h`.
- **Controller remap.** The stock input plugin reads its `Input-SDL-ControlN` config once and cannot
  express a modifier layer, so suxty does not make it reload anything. The same SDL joystick override layer that hides
  hotkeys now *is* the controller the plugin reads: the plugin is configured (in memory, before it starts) for a
  fixed virtual pad, and suxty answers its `SDL_JoystickGetButton/Axis` calls with the N64 state computed from
  the current mapping and the real, filtered pad. See *Controller remap* below and `src/remap.h`.
- **Per-game layers.** `cfg.c` looks every setting up game-then-global (`cfg_get*`) and writes to the layer of the menu's scope
  (`cfg_set*`); `remap.c` keeps both layers of every pad mapping and of the player choices; `profile.c` puts the game's core/plugin
  values into the core's config memory at launch, and wraps saving so the global files stay global (see *Profiles & per-game settings*);
  `menu_prof.c` has the Video / Audio / Emulation / Game profile pages. Nothing here needs another thread: it all runs in the
  places M1-M3 already used.
- **Widgets.** A small own-drawn set (list, toggle, slider, choice, scrolling lists, status line, slot carousel, toasts,
  button-capture prompt) with D-pad focus navigation. It uses SDL2 and GLES only, with no GUI toolkit
  dependency. It scales to common handheld resolutions (640×480, 720×720,
  1280×720, …).

---

## Controls

| Input | Action |
|---|---|
| **Select + Start** *(configurable)* | Open or close the menu |
| D-pad / left stick | Move focus; Left/Right scrolls the save-slot carousel, changes sliders and choices |
| **A** | Confirm (on a mapping row: start the capture prompt) |
| **B** | Back / close |
| **X** | On a mapping or hotkey row: clear it. In *This game* scope, on a row with a green bar: reset it to the global value |
| L / R | Scroll the save-slot carousel |

**Select + Start always opens the menu**, even if you set a different menu combo (the *recovery combo*, see *Hotkey
assignment* below).

In-game hotkeys, all configurable combos built on the **hotkey button** (default **Select**, written
`hotkey` in a combo). While the hotkey button is held the game sees a neutral controller, so nothing
leaks to the game:

| Default combo | Action |
|---|---|
| Hotkey + R | quick-save to the current slot ("Saved slot 3") |
| Hotkey + L | quick-load the current slot |
| Hotkey + Right / Left | next / previous slot (the toast shows its last-saved time) |
| Hotkey + Y | fast-forward (hold, or toggle with `fast_forward_mode = toggle`); a badge shows the speed |
| Hotkey + B (hold 1 s) | quit (auto-saves first if auto-save is on) |

## Features

### Save-state carousel *(M2, implemented)*
- Pause menu > **Save States**: a horizontal ring of cards (slots 0-9 and **Auto**) showing each slot's
  number and the time it was last saved (the file's modification time; "Empty" if there is none). There are
  **no thumbnails**. The slot the carousel rests on becomes the current slot used by the quick-save/-load
  hotkeys.
- **Project64 states.** The core can also load Project64 saves, which it looks for next to the `.stN` files as
  `<ROM header name>.pjN.zip` / `.pjN` (the core's slot load tries `.stN` first, then `.pjN.zip`, then `.pjN`). A slot that has
  one is **not shown empty**: the card shows the time of the *newest* of its files and a yellow **PJ64** tag when that one is the Project64 state.
  Load by path when the PJ64 file is the newest (so a `.pj` you copied in later than an old `.st` really loads), the core's own slot load
  otherwise; hotkey quick-load does the same and the toast says "Loaded slot 3 (PJ64)". **Save** always writes the Mupen64Plus-format `.stN`
  (the core can only save slots in that format), which then is the newest and wins; the confirmation says "(Project64 state; saved as .st)"
  and the `.pj` file is left alone. **Delete** removes the shown (newest) file only. Verified with PJ64 states written by the core itself
  (uncompressed and zip) on desktop and on the ROCKNIX 2.5.9 core; states made by Project64 are the same format but were not available to test.
- **Save** (asks before overwriting), **Load** and **Delete** (asks first) per slot, using
  `M64CMD_STATE_SET_SLOT`, `M64CMD_STATE_SAVE` and `M64CMD_STATE_LOAD`. Slot files are the core's own
  (`<SaveStatePath>/<name>.st<N>`, same naming as the core, so they interoperate with `mupen64plus`
  and `ui-console`); suxty only stats and unlinks them. A save shows "Saving slot N..." and then a status
  line ("Saved slot 3" / "Saving slot 3 failed"); a save is refused up front if the state directory is not
  writable or the disk is nearly full (see *Known gaps*).
- **Auto-save on quit** (Settings, default on) and **`--resume`**, using the dedicated Auto slot
  (load and delete only; it is written when you quit).
- Toasts for hotkey actions and results ("Saved slot 3", "Loaded slot 1", "Slot 4  (empty)",
  "Fast-forward on") are drawn by suxty's overlay over the game frame.

### Controller remap *(M3, implemented)*
Pause menu > **Controls**. Everything applies at once, while the game is paused, and is saved.

- **Player / Controller.** Choose the player (1-4) and which pad drives it: *Auto* (pads in connection order,
  keyboards and other devices that merely show up as joysticks are skipped), *None*, or a pad by name. Swapping pads
  between players is live for players that had a pad when the game started; plugging in a pad for a player that had
  none needs the next launch (the input plugin only opens controllers at start-up).
- **Buttons.** All 14 N64 buttons (A B Z Start L R, D-pad, C buttons) and the four directions of the analog stick, each
  with one physical source: a button, one direction of a hat, or one half of an axis (a trigger is "Axis 2+", the right
  stick up is "Axis 3-"; a half axis can be a button, a button can be a stick direction). **A** on a row opens the
  **capture prompt**: press the control or move the stick/trigger; it times out after 6 s with no input (that is the
  cancel), and controls already held when it opens are ignored until released. **X** clears the row. A control drives one
  input per layer, so capturing one that is used elsewhere moves it ("moved from B"). Capturing "Stick Left" as an axis
  also fills in "Stick Right" with the other half of the axis if that was free.
- **Presets** (Controls > Presets, asks first, keeps your deadzone and range):
  *Default* picks the right one for the pad; *Right stick = C* (left stick moves, right stick is C, Z on the left trigger);
  *Face buttons = C* (hold a modifier, a spare trigger or stick click, and A/B/X/Y become C-Down/C-Right/C-Left/C-Up);
  *D-pad = stick* (the D-pad is the analog stick, the left stick is the N64 D-pad, for pads with no or an unwanted left
  stick); *Sticks swapped*. Pads without a stick get the D-pad as the stick and the modifier layer for C; the preset
  says so when a pad has nothing left to use as a modifier.
- **Modifier layer** (Controls > Modifier layer): pick a *modifier button*, then map any N64 input *while it is held*.
  A mapped input replaces the normal one for as long as the modifier is down, and the normal inputs that use the same
  physical controls are silenced meanwhile (so A does not also press N64 A while it is C-Down).
- **Stick deadzone / range.** In percent of the travel of each half axis (default 12 % and 100 %). Applied by suxty, so
  the sliders work live.
- **Test controller** shows what the game would get from the pad right now (buttons, C, D-pad, stick -80..80).
- The mapping belongs to the **pad model** (its SDL name), not to a player slot, so it follows the pad when players are
  swapped. Pads with an SDL game controller mapping get layouts from it; others use the generic layout guess (buttons 0-7 =
  A B X Y L R Select Start, hat 0 = D-pad) for the *menu*, and you map them by hand.

### Hotkey assignment *(M3, implemented)*
Settings > **Hotkeys**: the hotkey button, the menu combo, quick-save, quick-load, next/previous slot, fast-forward and
quit. **A** starts a capture: hold the buttons together, let go; no input for 8 s cancels. **X** clears (not the
hotkey button and the menu combo). Settings also has the fast-forward mode (hold/toggle), fast-forward speed and quit-hold
time as plain choices.

Lock-out protection, so a bad hotkey setting can never leave you without a menu:
- **Select + Start (`recovery_combo`) always opens the menu**, whatever the menu combo is. The UI cannot change or
  switch it off (only a hand edit of suxty.cfg can).
- A **menu combo (or hotkey button) change must be proven**: the new combo is applied tentatively, and you have 10 s to
  press it. Using it keeps it; B, the timeout, or opening the menu any other way (for example with the recovery
  combo) puts the old one back.
- Menu combos need at least two buttons; other combos must be "hotkey button + something"; a combo may not be equal to,
  contain or be part of another one (an identical one is *moved* from the other action; the menu and recovery combos are
  never taken); changing the hotkey button is refused if it would make a combo overlap another.
- *Reset to defaults* restores all hotkeys and the menu combo.

### Controller storage and the stock plugin (M3)
- suxty keeps the mappings in `suxty.cfg` (`[players]`, `[pad:<pad name>]`, see below).
- It also writes the standard `Input-SDL-Control1..4` sections of `mupen64plus.cfg` (`mode = 0`, `device`, `name`,
  `plugged`, `AnalogDeadzone`, `AnalogPeak`, `A Button = "button(0)"`, `Z Trig = "axis(2+)"`, `DPad U = "hat(0 Up)"`,
  `X Axis = "axis(0-,0+)"`, ...) with the real physical bindings of the *normal* layer, so the same file works with
  the stock plugin outside suxty (the modifier layer cannot be expressed there and is not exported).
- *How remaps take effect.* The input plugin reads its config only once, in `InitiateControllers()`, which the core runs
  inside `CoreAttachPlugin()`. Just before that, suxty puts a fixed virtual pad layout into the plugin's *in-memory*
  sections (`button(0)`..`button(13)` = the 14 N64 buttons, `axis(0-,0+)` / `axis(1-,1+)` = the stick, deadzone 0, peak
  32767) and restores the real bindings right after. During the game the plugin's `SDL_JoystickGet*()` calls for the
  player's joystick go to suxty's override layer (`src/inputfilter.c`), which returns the N64 state computed by
  `src/padmap.c` from the *current* mapping and the real pad (with the M2 hotkey/menu filtering applied first).
  So a mapping, preset, deadzone or "which pad is player 1" change is picked up on the next poll; nothing is reloaded
  and the plugin is not modified. The alternatives were: apply on the next launch (no live feedback, and the modifier
  layer is impossible), or calling the plugin's `InitiateControllers` again mid-game (its internal state and
  the core's controller table would go out of step).
  If the plugin never reads the pad through those calls, suxty logs a warning and shows a notice after 5 s;
  `[input] remap_input = false` turns the layer off and leaves the plugin's own config alone.

### Profiles & per-game settings *(M4, implemented)*

**Two layers.** Every setting has a *global* value (`suxty.cfg`, `mupen64plus.cfg`) and may have a *game* value in
`<configdir>/suxty/games/<rom-md5>.cfg` (the ROM's MD5 from `M64CMD_ROM_GET_SETTINGS`, so a `.z64`, `.v64` and `.n64` dump of
the same game share one file). The game's value wins; deleting the entry ("Reset to global") makes the game follow the global
value again. A game file only holds what you changed; nothing is copied.

**In the menu.** The main page has **Apply to: All games / This game**. It decides where the changes made on the *Controls*,
*Video*, *Audio* and *Settings* pages (and their sub-pages) are written, and each of those pages shows the scope in its title
("Video: Foir (game)"). In *This game* a row you changed gets a **green bar**; in *All games* a row the current game overrides gets
a dim bar and the status line tells you the game's value. **X** on a green row is **Reset to global**; *Settings > Game profile*
has "Reset all to global" (it deletes the whole game file) and "Restart game to apply". The menu's look (dim, hints, notices),
swap A/B and the recovery combo are global only, so *Settings* in This game scope lists just the per-game ones.

| What | Where it is saved for a game | Takes effect |
|---|---|---|
| **Video plugin options**, every parameter of the loaded video plugin's own config section (foir's `FastCombiner`, `RelaxedPrecision`, `FrameSkip`, `InterlaceHack`, `Threads`, `Specialize`, `NEON`, `VSync`, `Filter`, `Widescreen`; Rice's, GLideN64's, ... whatever it has) | `[cfg:Video-Foir]` ... | **next launch**: plugins read their options once; the menu then offers **Restart game to apply** (below) |
| **Audio**: *Volume* (0-100, suxty's own option, also applied to the audio plugin) and every parameter of the audio plugin's section (`Audio-SDL`: resampler, buffer sizes, `VOLUME_DEFAULT`, ...) | `[audio] volume`, `[cfg:Audio-SDL]` | volume: **at once**; plugin options: next launch |
| **Emulation**: CPU core (interpreter / cached / dynarec), `DisableExtraMem`, `CountPerOp` | `[cfg:Core]` | next launch |
| **Controller mapping**: which pad drives which player, and each pad's N64 mapping, presets, modifier layer, deadzone, range | `[players]`, `[pad:<pad name>]` | **at once** (M3's live remap) |
| **Hotkeys** (hotkey button, menu combo, quick-save/-load, slots, fast-forward, quit) and fast-forward mode / speed / quit hold | `[hotkeys]`, `[menu] menu_combo` | **at once** |
| **suxty options**: auto-save on quit | `[states]` | **at once** |
| **Plugins** for the game: `video` / `audio` / `input` / `rsp` file names (hand-edited; the command line wins) | `[plugins]` | next launch |

**Generic plugin options.** The Video / Audio / Emulation pages are built at run time from the core's config API
(`ConfigListParameters` of the section that belongs to the loaded plugin, found from its file name: `mupen64plus-rdp-foir.so` ->
`Video-Foir`, `...-video-GLideN64.so` -> `Video-GLideN64`, `...-audio-sdl.so` -> `Audio-SDL`), so any plugin's parameters can be
set per game with no code for that plugin. Booleans are toggles, numbers are steppers (coarse for big values), a few known
parameters have named choices (foir `FrameSkip` / `Threads`, the CPU core, `Audio-SDL` `RESAMPLE`), and the plugin's own help text is
the status line. Free strings (paths) are not shown. Only `Video-*`, `Audio-*`, `Rsp-*` and a short list of `Core` keys can be
overridden; `Input-*` belongs to the remap layer.

**When the plugins see a game value, and "Restart game".** The video and audio plugins read their sections in `RomOpen` /
`InitiateGFX` (when the emulator starts), the RSP likewise, and the input plugin inside `CoreAttachPlugin()`. suxty puts the
game's `[cfg:...]` values into the core's *in-memory* config after the plugins have started (so their defaults exist) and before
attach, which is the point where `mupen64plus-ui-console` applies `--set`; the command line then still wins. A plugin that
reads its options only once cannot see a later change, so editing one from the menu says **"Applies after a restart"**, and the page
shows **Restart game to apply** (only when an edited value really differs from what the plugin started with). That button
auto-saves, shuts the whole front-end down (plugins unloaded, config written) and `exec`s itself again with `--resume`, so you land
where you were with the new options read from scratch (about two seconds; the scope selector goes back to *All games*). If the
auto-save cannot be written, the restart is cancelled and the game keeps running. The restart resets the signal mask first:
the cxd4 RSP plugin leaves SIGSEGV blocked and the next run would otherwise die in its start-up probe.

**How the global files stay global.** Per-game values only ever live in the game file and in the core's *memory*. For every value suxty
changed in memory it remembers the global one; the front-end never calls `ConfigSaveFile` / `ConfigSaveSection` directly but through
`profile_save_file()` / `profile_save_section()`, which put the global values back, save, and put the game's values back. So the
`mupen64plus.cfg` written at exit (or by the save before the game starts, or after a global edit in the menu) has global
values only, a `kill -9` mid-game leaves the file as the last clean save wrote it, and the final save at exit also scrubs anything a
plugin may have written during the game. A *global* edit of a value the game overrides updates the remembered global, not the live
value. `Input-SDL-ControlN` is written in the standard form from the **global** player choices and mappings (a game's
`[players]` / `[pad:...]` never reach it). A value you pass with `--set` / `--emumode` wins over the game's and is persisted as before (ui-console
behaviour), the game's other values are not. With `--nosaveoptions` `mupen64plus.cfg` is never written.

**Hotkey lock-out protection still holds per game.** The recovery combo (Select+Start) is global and always opens the menu; a game's own
menu combo / hotkey button change must be proven within 10 s like a global one (and is reverted to "no game entry" if it is not);
changing the *global* menu combo or hotkey button is refused while the current game overrides it (the change could not be proven here).
Overlap checks run inside the layer being edited.

**Not done:** a bundled per-game settings database (the roadmap's "later, as needed"); per-game *plugin choice* is file-only
(`[plugins]`); video options that a plugin reads on every frame are not special-cased (they would still say "next launch").

### Deliberately out of scope
- CPU governor or clock control. That belongs to the distro.
- Save-state thumbnails.
- ROM browsing and box art.

---

## Configuration layout

suxty follows the Mupen64Plus config directory, which is usually
`~/.config/mupen64plus/`:

```
mupen64plus.cfg          # core + plugin settings: the GLOBAL values only (never a game's)
suxty/
  suxty.cfg              # menu, hotkeys, auto-save options, controller mappings (the global layer, see below)
  games/
    <rom-md5>.cfg        # per-game overrides, layered over the globals; deleted when it has no entry left
```

A game file uses the same sections and keys as `suxty.cfg`, plus:

```ini
[cfg:Video-Foir]                 # core / plugin parameters of that section (Video-*, Audio-*, Rsp-*, and a few Core keys)
Threads = -1
Specialize = False
FrameSkip = auto
[cfg:Core]
R4300Emulator = 1                # 0 interpreter, 1 cached interpreter, 2 dynarec
[audio]
volume = 60                      # 0-100, applied to the audio plugin at start and when changed
[hotkeys]
quick_save = hotkey+x
fast_forward_speed = 200
[states]
autosave_on_quit = false
[players]
p1 = suxty vpad
[pad:suxty vpad]                 # a full mapping (the menu writes all keys; hand-edited files may override single keys)
a = button:1
b = button:0
[plugins]                        # hand-edited; used when the command line names no plugin
video = mupen64plus-video-rice.so
```

Keys that cannot be per game (`dim_percent`, `show_hints`, `show_toasts`, `toast_ms`, `recovery_combo`, `swap_ab`, `nav_deadzone`,
`block_game_input`, `remap_input`) are ignored in a game file.

### suxty.cfg (M2 and M3 options)

```ini
[menu]
menu_combo = select+start        # buttons joined with '+'; the word "hotkey" stands for hotkey_button
dim_percent = 65
show_hints = true
show_toasts = true
toast_ms = 1800
recovery_combo = select+start    # ALWAYS opens the menu too (lock-out protection; "none" disables, at your own risk)
[input]
swap_ab = false
nav_deadzone = 50
block_game_input = true          # hide hotkey/menu buttons from the game (recommended)
remap_input = true               # false: leave the input plugin's own configuration alone (no remap layer)
[hotkeys]
hotkey_button = select           # while held, the game sees a neutral controller
quick_save = hotkey+r            # empty = disabled
quick_load = hotkey+l
next_slot = hotkey+right
prev_slot = hotkey+left
fast_forward = hotkey+y
quit = hotkey+b
fast_forward_mode = hold         # or toggle
fast_forward_speed = 300         # percent (50-1000), 0 = unlimited (speed limiter off)
quit_hold_ms = 1000
[states]
autosave_on_quit = true
[audio]                          # M4: optional; the core's volume (0-100), absent = whatever the audio plugin starts with
volume = 70
[players]                        # M3: which pad drives which N64 player
p1 = auto                        # auto | none | <pad name as the input plugin calls it>
p2 = auto
p3 = auto
p4 = auto
[pad:retrogame_joypad]           # M3: the mapping of one pad model (written by Controls in the menu)
preset = default
deadzone = 12                    # % of a half axis
peak = 100
modifier = button:7              # none | button:N | hat:N:up|down|left|right | axis:N+|-
a = button:0                     # one key per N64 input: a b z start l r dpad_up dpad_down dpad_left dpad_right
z = button:6                     #   c_up c_down c_left c_right stick_left stick_right stick_up stick_down
stick_left = axis:0-             # axis:N- / axis:N+ is one half of axis N, from its rest value
mod_c_down = button:0            # mod_<key>: the same input while the modifier is held
```

Fast-forward sets the core's speed factor (or turns its speed limiter off for `0`) and also turns
vsync off, so it can go faster than the display refresh rate; the real limit is the device.

---

## Roadmap

1. **M1 – Boot & pause** *(done)*: load the core and plugins, override VidExt, and draw the
   menu overlay with Resume, Reset and Quit.
2. **M2 – Save states** *(done)*: the slot carousel, quick-save / quick-load / slot / fast-forward /
   quit hotkeys hidden from the game, toasts, and auto-save on quit with `--resume`.
3. **M3 – Remap UI** *(done)*: button capture, presets, modifier layer, deadzone/range, controller selection,
   hotkey assignment with lock-out protection, all live with the stock input plugin.
4. **M4 – Per-game profiles** *(done)*: global + per-game layers keyed by the ROM's MD5, per-game video / audio /
   emulation plugin options (generic, from the plugins' own config sections), controller mappings, hotkeys and suxty
   options, "Apply to" scope and "Reset to global", restart-to-apply, global files protected; Project64 `.pjN` slots in
   the carousel.
5. **Later**: a bundled per-game settings database; thumbnails stay out of scope.

---

## Building

```sh
cd projects/unix
make all
```

Cross-building for aarch64 handhelds: `tools/build-aarch64.sh [dir with libSDL2-2.0.so.0]`
(or pass `CROSS_COMPILE=aarch64-linux-gnu- POSTFIX=-aarch64 SDL_CFLAGS=... SDL_LDLIBS=...`
to `make`).

The Makefile links with `-Wl,--dynamic-list=suxty.exports` so the executable exports the SDL joystick
overrides that hide hotkey input from the game (see *Architecture*); keep that when writing your own build
rules, or set `block_game_input = false`.

`make check` builds and runs the unit tests (ASan/UBSan). Requirements:

- a C11 compiler (the code is built as `-std=gnu11`)
- SDL2 development headers and library (that is the **only** library dependency: the GL / GLES
  entry points are resolved at run time through `SDL_GL_GetProcAddress`, so no GLES or EGL
  headers or libraries are needed to build)
- the Mupen64Plus core API headers (`../mupen64plus-core/src/api` is found automatically)

## Known gaps (as of M4)

- The input filter only covers input plugins that read the pad through `SDL_JoystickGetButton/Axis/Hat`
  after `SDL_JoystickUpdate` (all stock `mupen64plus-input-sdl` builds do). A plugin that uses the SDL
  game-controller API or evdev directly is not filtered, and keyboard input is never filtered. With
  `block_game_input = false` the menu falls back to the M1 behaviour of waiting for the buttons to be
  released, and hotkey buttons are seen by the game.
- Menu and hotkey combos are only evaluated once per presented frame, so a press shorter than a frame
  can be missed; a game that stops presenting frames cannot be quit with the hotkey.
- Save slots are written in the Mupen64Plus format (`.stN`) only; a slot that exists as a Project64 state (`.pjN` / `.pjN.zip`) is shown
  (tagged PJ64) and loaded, but saving over it writes a `.stN` next to it and never touches the `.pj` file. Project64 states from the real
  Project64 were not available to test (the ones tested were written by the core itself); the core's own loader decides what it accepts
  (zip and uncompressed). A `.pj` whose ROM-name part differs from the core's header-name spelling is not found.
- The core (not suxty) leaves its state-file lock held if writing a state file fails, which would hang
  the next load. suxty therefore checks the state directory is writable and has room (>= 24 MB) before
  every save, but cannot cover a card that fails mid-write.
- Time stamps are the file mtimes in the local time zone; a device without a battery-backed clock shows
  whatever its clock said.
- Toasts and the fast-forward badge are drawn by switching to the private overlay context twice per
  frame while one is visible; this worked on every video plugin tried, but it is not free on every GPU.
- Fast-forward with audio: the core's speed factor changes audio speed, `fast_forward_speed = 0` (limiter
  off) can make `audio-sdl` skip.
- Remap: each N64 input has one source per layer (no "A or B both give Z"), the modifier layer is not part of the
  standard `Input-SDL-Control` export, and there is no octagonal stick gate or sensitivity curve (deadzone and range only).
- Remap: the input plugin opens its controllers at start-up, so a pad plugged in for a player that had none needs the
  next launch (swapping between pads that were present works live; an unplugged pad leaves its player neutral until it
  comes back). A pad whose name changes between launches gets the default mapping again.
- Remap: capture and the hotkey combos work in terms of the pad's SDL game controller logical buttons for hotkeys (a
  control without a standard name cannot be a hotkey) and in raw button/hat/axis indices for N64 inputs. A capture can
  only be cancelled by waiting for the 6 s timeout (any control may be the one you want to bind, including B).
- Remap: keyboard and mouse bindings of the stock plugin are not kept for pads suxty manages (it writes joystick
  bindings only); with no usable gamepad at all suxty leaves the plugin's own (keyboard) configuration alone.
- The input plugin must read the pad through `SDL_JoystickGet*()` for the remap, as for the hotkey filter (all stock
  `mupen64plus-input-sdl` builds do).
- Per-game plugin options are read by the plugins when the emulator starts: a change needs "Restart game to apply" (exec of suxty with
  `--resume`; Linux only, needs `/proc/self/exe`). The restart goes through the auto-save, so a game whose state cannot be saved cannot
  be restarted; the menu's scope selector and an open Controls page do not survive it. If a plugin read a parameter other than in
  `RomOpen`/`InitiateGFX`/`InitiateAudio`/attach (for example in `PluginStartup`, before suxty can apply the game's value), that value would be one launch late;
  foir (verified on x86_64 and on the RK3566: `workers=` / `specialised variants` in its log follow the game file), GLideN64 (RK3566: a
  game's `ShowFPS = True` draws the counter, another game does not) and Audio-SDL's volume had their *effect* checked; for Rice (RK3566, `ShowFPS`
  showed no overlay in the title screen, the value does reach its config section) the effect is unverified.
- The generic option pages show a plugin's int / float parameters with coarse steps and no known range (a wrong value is the plugin's problem), hide free
  strings, and know the valid choices of only a handful of parameters. A parameter that is added, renamed or removed by a plugin update is simply
  listed or no longer applies (a game entry for a parameter the loaded plugin does not have is ignored).
- Mid-game, global edits to a plugin section rewrite that section of `mupen64plus.cfg` when you leave the page (not per click); a game
  override of the same section is put aside for that instant. A plugin that writes its own config while the game runs would persist its in-memory
  values (the game's) until suxty's final save at exit scrubs them; a hard kill in that very window is not covered.
- Controller mappings are overridden per pad model as a whole (the menu writes every key), the game's pad choice per player; there is no per-key
  "this one button only" marker, and "Reset to global" on the Controls page resets all of the game's controller entries.
- Per-game `[plugins]` choices and a per-game video resolution (`Video-General`) are file-only / not offered; the bundled per-game database is not there.

## License

**GPLv2 or later**, matching the Mupen64Plus core.
