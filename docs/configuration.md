# Configuration

Everything can be changed from the menu; the files below are what it writes. suxty never writes the core's `mupen64plus.cfg` (the front-end owns it),
apart from the explicit "Save as Video Default" / "Save as Audio Default" actions.

```
mupen64plus.cfg          # the core's and the plugins' settings: owned by the front-end
suxty/
  suxty.cfg              # global layer: menu, hotkeys, auto-save, controller mappings, [foir], [audio]
  games/<rom-md5>.cfg    # per-game overrides, layered over the globals; deleted when it has no entry left
```

## Layers and per-game settings

Every setting has a global value (`suxty.cfg`) and may have a game value in `games/<rom-md5>.cfg` (the ROM's MD5, so a `.z64`, `.v64` and `.n64` dump share a file).
The game wins. The game file uses the same sections and keys as `suxty.cfg`.

- The menu's **Apply To** (All Games / This Game) chooses where the Controls, Audio Options, Video Options and Settings pages write.
- In the lists a green bar marks a row the game has its own value for (in This Game scope), an olive bar one it has while you look at the global value; the status line says which.
- **X** on a green row, or **Settings > Reset Game Profile**, deletes the game entry (this game follows the global value again).
- **Save Game Profile** copies every per-game-capable setting in force (auto-save, Confirm Quit, hotkey combos, fast-forward, Soft Tilt, volume, controller choice and mappings, foir's and altru's options) into the game's file. It does not include the hotkey button or the menu combo.
- Global only: Dim, Notices (`dim_percent`, `show_toasts`, `toast_ms`), `recovery_combo`, `swap_ab`, `nav_deadzone`, `block_game_input`, `handle_signals`.
- Per-game mappings override a pad model as a whole; there is no per-key marker.

## suxty.cfg

```ini
[general]
version = 3
handle_signals = true            # file only: SIGUSR1 / SIGUSR2 and M64P_AUTOLOAD as spruce uses them (launchers.md); false = leave them to the core
[menu]
menu_combo = select+start        # buttons joined with '+'; the word "hotkey" stands for hotkey_button
dim_percent = 65                 # 0-90
show_toasts = true
toast_ms = 1800
recovery_combo = select+start    # ALWAYS opens the menu too ("none" disables, at your own risk)
[input]
swap_ab = false                  # file only: swap confirm / back in the menu
soft_tilt_scale = 0.5            # stick strength while the Soft Tilt button is held (0.0-1.0); no menu row
nav_deadzone = 50                # % of stick travel that counts as a D-pad press in the menu
block_game_input = true          # hide hotkey / menu buttons from the game (recommended)
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
confirm_quit = true              # the quit combo must be pressed twice within 2 s
[states]
autosave_on_quit = false         # opt-in: Quit saves <state name>.auto.st first
[audio]
volume = 70                      # optional; absent = whatever the audio plugin starts with
FF_MODE = Muted                  # optional, with altru: its options by their [Audio-Altru] names (Volume is `volume`)
[foir]                           # optional; foir's options by their [Video-Foir] names
Widescreen = true
FrameSkip = Auto
[players]                        # which pad drives which N64 player: auto | none | <pad name>
p1 = auto
[pad:<sdl pad name>]             # the mapping of one pad model (written by Controls)
deadzone = 12
dpad_swap = false                # true: D-pad and stick channels are exchanged ("D-pad Acts As: Stick")
cface = button:7                 # the C-Face shift hold button: none | button:N | hat:N:up|down|left|right | axis:N+|-
cface_c_up = button:3            # while it is held these controls act as C-Up / -Down / -Left / -Right
softtilt = none                  # the Soft Tilt hold button
a = button:0                     # one key per N64 input: a b z start l r dpad_* c_* stick_*
```

`handle_signals` is read once when the plugin starts. With `true` (the default) suxty takes `SIGUSR1` (write the auto-save, then quit), `SIGUSR2` (open / close the menu) and the
`M64P_AUTOLOAD` environment variable (resume from the auto-save) for itself; see [launchers.md](launchers.md#spruce-rg35xx-sp) and [saves.md](saves.md#signals-and-resuming). With `false` suxty installs no handler and leaves
the variable alone, so whatever the core does with them applies (stock core: `SIGUSR1` / `SIGUSR2` end the process; spruce's core: `SIGUSR1` saves the current slot and quits, `M64P_AUTOLOAD` loads slot 0).

The menu's FF Speed row offers 150, 200, 300, 400, 500 percent and Unlimited; other values written by hand are shown as they are.

## Controls and hotkeys

- The mapping belongs to the **pad model** (its SDL name), so it follows the pad when players are swapped. Each N64 input has one physical source: a button, one direction of a hat, or one half of an axis.
- **Stick Deadzone** / **Stick Range** default to 12 % and 100 % (menu ranges 0-40 and 50-100). **Reset to Default Mapping** keeps them: left stick = stick, D-pad = D-pad, right stick = C; a pad with one stick gets a C-Face Shift button, one with none has its D-pad as the stick.
- Control capture gives up after 5 s without input; a hotkey capture after 7 s idle (hold the buttons together, then let go).
- **Lock-out protection:** `recovery_combo` (Select + Start) always opens the menu; a new menu combo or hotkey button must be proven by using it within 10 s or the old one comes back; overlapping combos are refused.
- With `block_game_input = false` the menu waits for the buttons to be released instead and the game sees the hotkeys.
- The game-input filter only covers SDL joysticks; keyboard input is never filtered.

## Upgrading

Version 3 migrates old files when they load, in `suxty.cfg` and the game files alike, and writes them back clean:

- `[hotkeys] quit_hold_ms` becomes `confirm_quit` (0 = false, any other time = true; an explicit `confirm_quit` wins) and is removed.
- `[menu] show_hints` is removed (hints are always on).
- In `[pad:...]`, a `modifier` whose layer mapped the C buttons (`mod_c_*`) becomes `cface` / `cface_c_*`; any other modifier layer and the `preset` name are dropped.
- Keys and sections of the old stand-alone front-end (`[input] remap_input`, `[plugins]`, `[cfg:...]`, `[UI-Suxty]`) are dropped, and `autosave_on_quit` goes back to its default (off).
- Unknown keys are kept.
