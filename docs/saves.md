# Saving, loading and auto-save

suxty sends the core's command and does not track it, except through the state file itself.

## Save and load

- **Save** (menu: acts on the slot of "In Slot" and asks before overwriting a filled one; hotkey: at once): `M64CMD_STATE_SAVE`, the menu closes, toast "Saving slot N...".
  suxty watches the state directory with **inotify** (`IN_CLOSE_WRITE | IN_MOVED_TO` for that file name), polled once per frame (no thread). The core closing the file turns the toast
  into "Saved"; if that has not happened within 5 s it becomes "Save not written" (an empty file counts as not written, too). The events come from the VFS, so this is the same on vfat
  (the spruce SD card) and ext4 / btrfs. Only if inotify cannot be set up does suxty fall back to the file's time and size. A save is refused up front when the state directory is not
  writable or the disk is nearly full (less than 24 MB free).
- **Load**: `M64CMD_STATE_LOAD`, the menu closes, the core does the rest. The Save States carousel (Left / Right or L / R) shows each slot's number and the file's time ("Empty" if none; no thumbnails).
- **PJ64 states.** A slot that exists only as a Project64 state (`<ROM header name>.pjN[.zip]`) is tagged "PJ64" and loaded by path. Saving writes the Mupen64Plus `.stN` and leaves the `.pj` file alone.
- **Auto card.** The Auto card is informational: its Load is greyed (the launcher loads it, see [launchers.md](launchers.md)); Delete works on it and removes its backups.
- **Reset**: No / Yes (soft, `M64CMD_RESET 0`, like the console's reset button) / "Hard (Power Cycle)" (`M64CMD_RESET 1`). **Quit**: No / Yes, `M64CMD_STOP`; says "(Auto-Save Enabled)" when that is on.
- Slot files are named like the core's (`SaveFilenameFormat`; an existing legacy `<good name>.stN` is used first).

## Auto-save on quit (opt-in)

`[states] autosave_on_quit = false` by default; menu: Settings > Auto-Save. When on, Quit first writes the separate auto slot `<state name>.auto.st` (never a numbered slot: spruce's
`M64P_AUTOLOAD` loads slot 0), waits for the inotify event, then stops. If it could not be written the game keeps running (toast "Save not written"; Quit again to retry).
suxty does not load it: resume through the front-end with `--savestate`.

### Safety net

The core rewrites a state in place, so a write that fails or is cut short (battery, a pulled card) would lose the previous auto-save. So:

1. **Quit** with auto-save on: if `<name>.auto.st` exists it is first renamed to `<name>.auto.st.prev` (`.prev2`, `.prev3` if earlier backups are still there; when all three are taken the oldest goes), then the core writes `<name>.auto.st`. If the write fails in the session, the newest backup is put back.
2. **At game start**, before the launcher's load, suxty checks `<name>.auto.st`:
   - **valid** (the gzip stream decompresses completely with a good CRC and length, it starts with the core's `M64+SAVE` magic and a `1.x` version, and has exactly the size the core writes, 16 793 412 bytes for the current version): its backups are deleted;
   - **damaged or missing, and a backup validates**: the newest valid backup is renamed back, the other backups are deleted, and a toast says "Auto-save was damaged; previous one restored" ("... was missing; ..." when the file was gone);
   - **damaged and no backup validates**: nothing is touched, the log says so, and a toast "Auto-save is damaged" appears.

   The check decompresses the 16.8 MB state once per game start, only when an auto file exists (about 40 ms on a desktop, about 0.4 s on the RG35XX SP). It has its own small inflate, so suxty needs SDL2 only.
3. suxty gets no "load succeeded" callback, so "the new file is good" is judged by its contents. If the launcher's command line has `--savestate <file>` (read from `/proc/self/cmdline`) the log mentions it, but nothing depends on it.
4. Only this game's files (`<name>.auto.st` and `<name>.auto.st.prev*`) are touched. Delete on the Auto card removes the auto-save and all backups.

Limits: the core leaves its state-file lock held if writing fails, which would hang the next load; suxty refuses to start a save when it can see that coming but cannot cover a card that fails mid-write.
A save the core never starts (for instance a directory at the slot's path) is reported as "Save not written" after 5 s.

## FOIR Options

Settings > **FOIR Options** appears only when `mupen64plus-rdp-foir` is the video plugin. suxty finds it through a one-way interface foir exports (`FoirSuxtyGetInterface`, ABI 1; see foir's README, "suxty interface"),
so the options (label, kind, value, whether a change applies at once) come from foir's own table. Currently:

- **Applies at once:** Widescreen, Smoothing, Interlace Hack, Frame Skip.
- **Applies at the Next Launch** (under a heading): VSync, Worker Threads, NEON Kernels, Specialised Kernels, Fast Combiner, Relaxed Precision.
- Storage follows **Apply To**: All Games writes `[foir]` in `suxty.cfg`, This Game in the game's file, with the `[Video-Foir]` key names (`Widescreen = true`, `FrameSkip = Auto`, `Threads = None`).
  At game start, before foir opens the ROM, suxty hands every stored value to foir, so next-launch options are applied then.
- **Save as FOIR Default** (asks first) writes the values shown into `[Video-Foir]` of `mupen64plus.cfg` through foir. It is the only way suxty causes that file to be written; foir then uses the values without suxty too.
- foir is detected by one scan of the loaded objects at game start and again when the menu opens; no polling.

## Volume

Settings > Volume (0-100, `M64CORE_AUDIO_VOLUME`) applies at once and is stored per scope (`[audio] volume`); X goes back to the audio plugin's own default. The row is hidden if the core or audio plugin cannot report a volume.
Fast-forward changes audio speed with the core's speed factor; `fast_forward_speed = 0` (limiter off) can make `audio-sdl` skip.
