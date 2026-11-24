# Running the game: command line and `wox.cfg`

> Part of the public source tree (adapted from the development project's documentation). References to "Phase NNN"
> mark when a behaviour was introduced in the project's development history; the phase reports themselves are not
> part of this tree.

The player-facing startup contract of `wox` (Windows: `wox.exe`; named `xeen_explore` before Phase 309) since Phase 270. Test-only
options (`--headless`, `--commands`, `--evidence`, ...) are listed by `wox --help`.

## Quick start

1. Put `wox` (and its music libraries: on Windows `xeen_sbpro.dll`, `xeen_sc55.dll`, `xeen_mt32.dll`; on Linux
   `libxeen_sbpro.so.1`, `libxeen_sc55.so.1`, `libmt32emu.so.2`) anywhere. Since Phase 335 SDL is linked into `wox`:
   no `SDL2.dll`. The folders come from `tools/windows/package.sh` and `tools/linux/package.sh`.
2. Start it **from the game data directory** (the folder holding `XEEN.CC` and `DARK.CC`; `INTRO.CC` is optional),
   or name that folder with `--data-dir DIR` or `[files] data_dir` in `wox.cfg`.
3. The first window run creates `wox.cfg` beside the executable with the defaults below (or run `setup` first: the
   settings tool beside `wox`, [below](#the-setup-tool-setup-phase-331)). The game's save slots go
   into the working directory (Phase 287: `[files] save_dir = auto`; `save_dir = DIR` or `--save-dir DIR` chooses
   another).
4. The window opens at the main menu (Load, New game, ...).

## Command line (player options)

| Option | Meaning |
|---|---|
| `--data-dir DIR` | The game data directory. Overrides `[files] data_dir`. A relative `DIR` is relative to the working directory. A missing directory is an error (no other place is tried). |
| `--fullscreen` | Start fullscreen (overrides `[display] fullscreen`). Without it the game starts in a window; Alt+Enter toggles. |
| `--menu` | Start at the main menu. This is already the default in a window; the option is kept for scripts. |
| `--no-menu` | Window: skip the menu and load the saves `--cloud-save FILE` / `--dark-save FILE` (default `DIR/XEEN01.WOX`, `DIR/DARK01.WOX` in the data directory). |
| `--save-dir DIR` | Where the game writes its save slots. Overrides `[files] save_dir`; created when missing. |
| `--wox-config FILE` | Use `FILE` instead of `wox.cfg` beside the executable (it must exist). |
| `--no-audio` | No audio engine. |
| `--addon NAME...`, `--bugfix NAME...` | Enable optional add-ons / bug fixes (not original behaviour), in addition to `wox.cfg`. |
| `--help` | Print the usage and exit. |
| `--version` | Print the version number (for example `1.0.0`) and a newline on stdout and exit with status 0 (Phase 338). Nothing else runs: no `wox.cfg`, `wox.log`, save or other file is read or written, no data, display or audio is needed, and the other arguments are ignored. |

**Renamed in Phase 336:** `--data` is `--data-dir` (rejected with that hint).

**Removed in Phase 270** (each is rejected with a hint): `--windowed` (a window is the default), `--scale`,
`--aspect` and `--authentic-cursor` (now `wox.cfg` `[display]`), `--resources` (renamed `--data`, `--data-dir` since Phase 336), `--cd-test`.

## `wox.cfg`

Found beside the executable (or `--wox-config FILE`); it never depends on the data directory. Created with the
defaults on the first window run when absent and never overwritten. Since Phase 335 (owner request), on Windows a
question first recommends running `setup.exe` and asks whether to create the default `wox.cfg` and start (No ends
the game, status 0, without creating it; **Run setup** starts `setup.exe` beside the game and ends wox); elsewhere the advice goes to stderr and `wox.log`. Lines are `key = value` under `[section]`;
`#`/`;` start comments, values may be quoted, unknown or duplicate keys and unknown values are errors.
**Phase 336 (owner decision):** every key is spelled with `_` and read exactly as listed (lower case; no `-` spelling),
every switch is `on`/`off` (`auto` or an empty value = off; `true`/`false` are refused), and the renamed keys are
refused under their former names (no aliases): `[audio] music` is `music_override`; `[files]` `data`, `save-dir`,
`XEEN_CFG_location`, `mt32-roms`, `sc55-roms`, `cd-tracks` are `data_dir`, `save_dir`, `xeen_cfg_dir`,
`mt32_roms_dir`, `sc55_roms_dir`, `cd_tracks_dir`; the former XEEN.EXE player's `no-music`, `windowed`, retired movie
keys and `music = none` are gone. A `wox.cfg` written before Phase 336 stops the start (`wox.cfg:LINE: unknown setting:
NAME`): delete it (the next window start creates a new one) or run `setup`.
**Relative paths in `wox.cfg` are relative to `wox.cfg`'s own directory.**

| Section | Key | Default | Values |
|---|---|---|---|
| `[display]` | `scale` | `6` in a created file (3 before Phase 335); `3` without the line | `1`..`10` (up to 6 before Phase 333): the 320x200 picture times this, in the screen's own pixels (on Windows the game is DPI aware since Phase 333, so the display scaling setting does not enlarge it) (`auto` = 3). Phase 335: the most the window uses: when it does not fit the screen (Windows: the work area less the frame; X11: the screen less 80 lines for the title bar and panels; 240 lines a scale with `aspect`), the window opens at the largest scale that does, noted in `wox.log` |
| `[display]` | `aspect` | `off` | `on`/`off` (`auto` = off): `on` shows 4:3 as on a CRT |
| `[display]` | `authentic_cursor` | `off` | `on`/`off` (`auto` = off): `on` moves the cursor only when the game shows a frame, as the original |
| `[display]` | `locked_scale` | `off` | `on`/`off` (`auto` = off; a host option, not original, Phase 332): `on` keeps the picture at `scale` (with `aspect`) in a larger window or in fullscreen, centred with black borders, and the window cannot be made smaller than that; a screen smaller than the locked size (fullscreen) still shows the largest scale that fits. `off`: the largest whole scale that fits the window |
| `[display]` | `fullscreen` | `off` | `on`/`off` (`auto` = off) |
| `[files]` | `data_dir` | `auto` | `auto` = the working directory at launch (`.`); otherwise a directory (relative to `wox.cfg`; write `./auto` for a folder named `auto`) |
| `[files]` | `save_dir` | `auto` | Phase 287: `auto` = the working directory at launch (`.`); otherwise an existing writable directory (relative to `wox.cfg`), never created: a missing or read-only one stops the start (exit status 1) |
| `[files]` | `xeen_cfg_dir` | `auto` | Phase 287: the directory holding the game's `XEEN.CFG` (as `XEEN.CFG` or `xeen.cfg`): `auto` = the data directory (`--data-dir` / `[files] data_dir`), never the save directory nor a `GAME*.GOG` image; otherwise an existing directory (relative to `wox.cfg`; a missing one stops the start, exit status 1). Every reader (the game's configuration, `music_override = auto`, `speech = auto`) takes this one file, and the game writes it back there (Other Options D, an ending). Phase 336: `XEEN_CFG_location` before |
| `[files]` | `mt32_roms_dir`, `sc55_roms_dir`, `cd_tracks_dir` | `auto` | ROM and CD-rip directories: `auto` = `roms/mt32`, `roms/sc55` and `music` beside `wox.cfg` (Phase 331: no environment override, and `cd_tracks_dir = auto` no longer searches the data directory); otherwise a directory relative to `wox.cfg` |
| `[audio]` | `music_override` | `system` | `auto` (the game's XEEN.CFG selection; any other value overrides it), `system`, `mt32`, `sc55`, `sbpro`, `sb`, `pas`, `adlib`, `wave`, `tap10`, `off` (Phase 286; below) |
| `[audio]` | `speech` | `auto` | `auto`, `on`, `off` (Phase 286; below) |
| `[audio]` (other keys), `[addons]`, `[bugfix]` | ... | | unchanged; see the comments in the generated file |

### `[audio] music_override` and `speech` (Phase 286)

`music_override` (named `music` before Phase 336) names the original DOS music driver (every one from INTRO.CC) and
what plays it. `speech` decides the spoken voices. `auto` reads the game's XEEN.CFG (the DOS installer's choice); every
other value of either key overrides XEEN.CFG. A missing `music_override` key keeps `system`, as before Phase 286; a
missing `speech` key is `auto`.

| `music_override =` | DOS card (XEEN.CFG music) | Driver | Plays on |
|---|---|---|---|
| `auto` | the installer's choice | as XEEN.CFG says | the row below for that driver; PC speaker, None or no XEEN.CFG: no music (reported) |
| `mt32` | Roland LAPC-1 / MT-32 (4) | rolmus | Munt (`mt32_roms_dir`) |
| `sc55` | Roland Canvas / GS (5) | canmus | Nuked-SC55 (`sc55_roms_dir`) |
| `sbpro` | Sound Blaster Pro (3) | promus | two OPL2 chips, stereo |
| `sb` | Sound Blaster (2) | blastmus | one OPL2, mono |
| `pas` | Pro-Audio Spectrum (6) | specmus (the Sound Blaster driver) | one OPL2, mono |
| `adlib` | AdLib (1) | admus | one OPL2, mono |
| `wave` | Wave Blaster (8) | wavemus | Nuked-SC55 (`sc55_roms_dir`) |
| `tap10` | Roland TAP-10 (9) | tapmus | Nuked-SC55 (`sc55_roms_dir`) |
| `system` | (native) | rolmus | the system MIDI device (`midi_device`, `midi_port`) |
| `off` | None (0) | nullmus | nothing |

The PC speaker's music driver is not available. A missing ROM directory or INTRO.CC means no music, with the reason
reported; no other driver stands in.

| `speech =` | Spoken voices | Cutscene subtitles, dialogue text |
|---|---|---|
| `auto` | as XEEN.CFG's sound card: None plays none; the PC speaker's are not played; the others play | as the original (shown when the card plays no voice) |
| `on` | always (the Sound Blaster driver's timing stands in for None and the PC speaker) | as the original |
| `off` | none (the CD speech too) | always shown; the control panel's Speech stays off |

```ini
[audio]
# the DOS installer's choices (XEEN.CFG)
music_override = auto
speech = auto
```

```ini
[audio]
# the AdLib arrangement, no voices but every line's text
music_override = adlib
speech = off
```

A minimal file with the Phase 270 keys (and Phase 287's `[files]` keys, in the generated file's order):

```ini
[display]
scale = 3
aspect = off
authentic_cursor = off
locked_scale = off

[files]
data_dir = auto
save_dir = auto
xeen_cfg_dir = auto
```

### The `[files]` directories: `data_dir`, `save_dir`, `xeen_cfg_dir` (Phase 287)

The generated `wox.cfg` lists `[files]` as `data_dir`, `save_dir`, `xeen_cfg_dir`, then `mt32_roms_dir`, `sc55_roms_dir`,
`cd_tracks_dir`. The order is documentation only: the parser accepts the keys in any order, and older files without the
new keys keep working (each missing key is `auto`).

| Key | `auto` (the default) | An explicit value | What uses it |
|---|---|---|---|
| `data_dir` | the working directory at launch | a directory (relative to `wox.cfg`); `--data-dir DIR` overrides both | the game's archives `XEEN.CC`, `DARK.CC`, `INTRO.CC` (from `GAME1.GOG` or `GAME2.GOG` in it when one holds all three; below); the default saves of `--no-menu` |
| `save_dir` | the working directory at launch (a window); none in a headless test run | an existing writable directory (relative to `wox.cfg`), never created; `--save-dir DIR` overrides both | the save slots (`XEENnn.WOX`, `DARKnn.WOX`), the add-ons' `ADDONnn.WOX`; the slot list also shows the data directory's saves |
| `xeen_cfg_dir` | the `data_dir` directory (after `--data-dir`) | an existing directory holding `XEEN.CFG` (relative to `wox.cfg`) | the game's configuration (the installer's choices), `music_override = auto`, `speech = auto`; the game writes `XEEN.CFG` back there |
| `[audio] music_override = auto` | — | any other `music_override` value overrides XEEN.CFG's music word | XEEN.CFG offset 4 (the music driver) |
| `[audio] speech = auto` | — | `on` / `off` override XEEN.CFG's sound word | XEEN.CFG offset 0 (the digital driver) |

Resolution, in this order: the data directory; `xeen_cfg_dir`; `XEEN.CFG` read from it; `music_override = auto` and
`speech = auto` take its words (an explicit `music_override` / `speech` wins). The save directory is resolved on its own and
never decides which `XEEN.CFG` is read.

| Situation | Data | Saves | XEEN.CFG |
|---|---|---|---|
| Everything `auto`, started in the game folder | the game folder | the game folder | the game folder's |
| Everything `auto`, started elsewhere (e.g. `cd C:\ && "D:\Might and Magic 4-5\wox.exe"`) | `C:\`: no archives there, so the start stops and names `.\XEEN.CC` | — | — |
| `data_dir = "D:\Might and Magic 4-5"`, the rest `auto`, started elsewhere | the game folder | the working directory | the game folder's |
| As above with `save_dir = "D:\Might and Magic 4-5\saves"` | the game folder | that folder (it must exist) | the game folder's |

A Windows installation with spaces in its path, its saves in their own folder, and `XEEN.CFG` beside the game (the
example GOG folder `D:\Might and Magic 4-5` holds `XEEN.CC`, `DARK.CC`, `INTRO.CC`, `XEEN.CFG` and the disc images
`GAME1.GOG`, `GAME2.GOG`):

```ini
[files]
data_dir = "D:\Might and Magic 4-5"
save_dir = "D:\Might and Magic 4-5\saves"
xeen_cfg_dir = auto
```

Saves elsewhere, a separate `XEEN.CFG` (relative paths are relative to `wox.cfg`'s folder):

```ini
[files]
data_dir = "D:\Might and Magic 4-5"
save_dir = my saves
xeen_cfg_dir = "D:\Games\xeen config"

[audio]
music_override = auto
speech = auto
```

An invalid value stops the game before anything opens (`wox.cfg:LINE: reason`, exit status 2).

### `[addons] please_wait_skip` (Phase 310)

The original game shows a small framed **"Please Wait..."** box over the bottom of the screen while a map loads after
leaving a shop, a town gate or the inn, taking stairs, crossing an outdoor map edge, Town Portal, Lloyd's Beacon or
Mr Wizard (and during a new game's set-up); a fall shows **"Oops..."** in the same box. The original has no timer for
it: the box is visible from the screen refresh after it is drawn until the load finishes (on period hardware the
disk made that noticeable). The native load takes a few milliseconds, so the game holds the box on screen for
**200 ms** (a project decision, not original timing; "Please Wait..." and "Oops..." alike), then the load runs. A menu
Load or a control-panel Load shows no box (as the original).

```ini
[addons]
# off (the default): the original box is shown
please_wait_skip = off
```

`please_wait_skip = on` (or `--addon please_wait_skip`) never shows the "Please Wait..." box. Nothing else changes:
the map load, the game state, saves, sound and input are identical, and the 200 ms hold is skipped with the box; the
fall's "Oops..." is always shown (and held 200 ms) whatever the setting. Like
every add-on it is not original behaviour, it is off by default, and `--addon` adds to `wox.cfg` (an `off` in
`wox.cfg` does not turn off a command-line `--addon`). Headless test runs apply only `--addon`.

### `[addons] wizard_eye_plus` (Phase 318)

With the Wizard Eye spell on, the original minimap (upper right) shows the whole map around the party, places never
visited included. A click on the minimap (the `=` automap) turns the normal minimap back on, which in the original
hides the cells the party has not seen again: indoors behind the minimap's fog tile, outdoors as the empty backdrop.
With this add-on those cells stay visible through a checkerboard, so the map reads as "known by magic, not yet
visited":

```ini
[addons]
wizard_eye_plus = off
# the style for each kind of map: 1px, 2px or black (the default)
wizard_eye_plus.indoor = black
wizard_eye_plus.outdoor = black
wizard_eye_plus.sky = black
```

| Style | Unseen cells |
|---|---|
| `1px` | every other pixel shows what is there, the others the original's fog (or backdrop) |
| `2px` | the same in 2x2 blocks |
| `black` | every other pixel shows what is there, the others are black |

- **Sky maps** take `.sky`, whether indoor or outdoor: every map where the party can fall, that is, step onto a
  `sky.srf` cell (or a `cloud.srf` cell without Levitate). From the original state archives
  (`src/addon/wizard_eye_plus/make_table.py`): Clouds maps 25–27 (the cloud maps; slot 4's map 26) and 200–204,
  Darkside's Skyroad (maps 89–112), the Elemental Planes (25–28), Olympus and its sewer (37–38), Castle Alamar
  (49–51), the Dungeon of Death level 124, Dragon Clouds (128), the Clouds of the Ancients (129) and their alternate
  mazes. Sky walled in from the walkable floor does not count (Ellinger's, the Western, Southern, Eastern, Northern,
  Darkstone and Dragon Towers, Castles Kalindra and Blackfang), nor operation 52's scripted pits (the Troll Holes'
  holes on Darkside's outdoor maps, the towers' trapdoors). **Outdoor maps** (the rest of the surface) take
  `.outdoor`; every other map `.indoor`.
- Only the cells hidden by the click change; seen cells, the minimap's frame and the party's arrow are the
  original's. Without Wizard Eye, or with the automap off (the original then already shows everything), nothing
  changes.
- Display only: the minimap is drawn by the game exactly as the original; the add-on shows its version on a copy of
  the displayed page, cell by cell, wherever that minimap is on screen: it stays while turning and while a window or
  the control panel is open (a cell a window covers shows the window). The game state, saves, sound, input and the
  game's own frames (captures, traces) are unchanged.
- `--addon wizard_eye_plus` turns it on as well; the three styles are read from `wox.cfg` (an explicit
  `--wox-config` too, also headless). A value other than `1px`, `2px` or `black` stops the start (`wox.cfg:LINE:
  wizard_eye_plus.indoor must be 1px, 2px or black: VALUE`, exit status 2).

## Precedence and resolution

- **Data directory:** `--data-dir DIR` > an explicit `[files] data_dir` > `auto` (= `.`, the working directory).
- **Save directory (Phase 287):** `--save-dir DIR` > an explicit `[files] save_dir` > `auto` (= `.`, the working
  directory; before Phase 287 a `saves` folder beside the executable: existing saves there are not moved, so name it
  with `save_dir` to keep using it). A headless test run has no save directory with `auto` (read-only).
- **XEEN.CFG (Phase 287):** the data directory is resolved first, then `xeen_cfg_dir` (`auto` = that data
  directory), then `XEEN.CFG` is read from it, then `music_override = auto` and `speech = auto` take its words; an explicit
  `music_override`/`speech` value overrides them. The save directory plays no part (before Phase 287 a window read the save
  directory's `XEEN.CFG` first). A missing `XEEN.CFG` is reported and the game starts with an empty configuration.
  A headless test run without an explicit location keeps the earlier developer model (the save directory's file,
  else the data directory's; writes into the save directory).
- **Display:** `wox.cfg` `[display]` only; `--fullscreen` overrides `fullscreen`.
- **Start:** `--menu`/`--no-menu` (the last one given) > the default (a window: the main menu; a headless test run:
  the saves).
- **Headless test runs** (`--headless`) read no `wox.cfg` unless `--wox-config FILE` names one; then its `[display]`
  and `[files] data_dir` apply (its audio settings only with `--audio-capture`; its add-ons never).

## CD images: `GAME1.GOG`, `GAME2.GOG` (Phase 287)

The game reads its three archives straight from a CD image in the data directory, read-only, member by member (the
image is neither extracted nor loaded whole; memory use is that of the three archives, as with loose files). The
choice, in this order, is made for the three archives together, never mixed:

1. `GAME1.GOG`, when it is a supported image holding `XEEN.CC`, `DARK.CC` and `INTRO.CC` in one directory (the root,
   else `GAME`), each readable and a CC archive the game can read;
2. else `GAME2.GOG`, by the same rules;
3. else the loose `XEEN.CC`, `DARK.CC` and the optional `INTRO.CC` of the data directory, as before.

Supported: ISO 9660 with 2048-byte sectors, and raw CD images of 2352-byte mode-1 sectors (the GOG release's
`GAME1.GOG`/`GAME2.GOG` and a BIN of the original CD are raw mode 1, with the archives in `GAME`). Names match in any
case, with or without the `;1` version. Refused, with the reason: other sector layouts (mode 2, 2336 bytes), other
block sizes, a truncated or malformed image, a member outside the image, duplicate names, multi-extent members, a
member that is not a CC archive. A refused image is named on stderr (`note: archive image not used: ...GAME1.GOG: reason`) and the
next source is tried; when nothing is usable the start stops and lists what it tried:

```text
cannot read D:\Might and Magic 4-5/XEEN.CC (the game data directory: --data-dir DIR or wox.cfg [files] data_dir)
game archives tried: GAME1.GOG (not usable: above), GAME2.GOG (absent), then the loose files
```

Only `XEEN.CC`, `DARK.CC` and `INTRO.CC` come from the image. `XEEN.CFG`, the saves, `wox.cfg`, the CD voice rips
(`music` beside the data directory) and the ROM directories are still read from their directories. On the GOG
installation used in development, the loose CC files beside the images are other builds than the images' (and the original CD's) ones;
with the images present the CD's archives are used.

## Floppy and CD-ROM archives

The game is reconstructed from the CD-ROM release. The floppy release's `XEEN.CC`/`DARK.CC` lack some of its
members (for example the World of Xeen title animation `world0.int`..`world2.int`, which the CD's `DARK.CC` holds).
When the game stops because a member it needs is not in a CC file, the stop's message names it (Phase 271; since
Phase 288 that sentence is the message itself, followed by the technical line):

```text
Cannot read world0.int from D:\Might and Magic 4-5/DARK.CC (possible floppy/CD-ROM version mismatch)
Technical details: native stop at 16662 status=1 (next=16662) stop=MENU_STOP_START_SPRITES category=resource basis=rule:sprite-load
```

The same line follows the other failures that a missing CC member causes (a rejected start, a failed session start
or Load, the original's fatal driver exit, an audio driver that cannot be loaded). Use the CD-ROM release's files.

## When the game stops (Phase 288)

Where the native engine cannot go on, the game stops and reports two lines (on stderr, in `wox.log`, and on Windows in
the error dialog):

1. a sentence for the player: the missing file's name when one was found missing; else what the stop is known to be
   (for example `The game could not load the graphics it needs here, so it cannot continue.`, `The game could not
   display this screen, so it cannot continue.`, `This game operation is not yet supported by the native engine.`);
   a stop whose cause has not been reviewed says `The game reached a point the native engine cannot continue from.`
   and anything else `The game stopped because an unexpected operation could not be completed. See the technical
   details for support.`
2. `Technical details: native stop at ADDRESS status=S (next=N) stop=NAMES category=C basis=B`: the original
   address of the step, the engine's status, the next boundary, the stop's symbolic names and how its category was
   established. Quote this line when reporting a problem.

The sentences come from a catalogue of every registered stop that the development project generates; they do not
change what the game does.

Since Phase 289 more stops say where the game stopped, for example `The game could not move the party across the edge
of the map, so it cannot continue.` (the step onto the next map failed), or that an event uses something no event of
the released game uses (`This event gives the party a kind of thing that no event of the released game gives, and the
native engine does not support it.`). `A required game service is unavailable, so the game cannot continue.`
(`category=host`) comes only from a program that does not provide a service the game needs, such as a test harness;
the shipped program always provides it.

## Messages, `wox.log` and errors (Phase 272)

**Windows:** `wox.exe` is a GUI-subsystem program: started from Explorer, a shortcut or `cmd`, it opens
no console window. Its messages go to `wox.log`; a redirection still receives them
(`wox.exe --help > help.txt`, `2> err.txt`). `--help` therefore prints nothing visible without a
redirection, but its usage text is also written to `wox.log`. `--version` writes no log, so on Windows read it
through a redirection or a pipe: `wox.exe --version > version.txt`, `wox.exe --version | more` (cmd) or
`.\wox.exe --version | Write-Output` (PowerShell); the version is also in the executable's Properties > Details.

**`wox.log`** (every window session; Linux too, besides the terminal; a `--headless` test run never writes it):

- Location: beside the executable (the directory of the default `wox.cfg`, also when `--wox-config` names another
  file; independent of `--data-dir`). When that directory is not writable: `%LOCALAPPDATA%\WoX\wox.log`
  (Windows) or `$XDG_STATE_HOME/wox/wox.log`, else `~/.local/state/wox/wox.log`. The second line of each session
  names the file in use. The first line names the game's version (Phase 338).
- Appended, never truncated (delete it to start over). Each entry is one line,
  `YYYY-MM-DD HH:MM:SS.mmm LEVEL text` (local time, milliseconds; `INFO`, `WARN` or `ERROR`):

  ```text
  2026-11-16 20:54:47.396 INFO  session start: WoX (1.0.0)
  2026-11-16 20:54:47.396 INFO  log file: C:\Games\wox\wox.log
  2026-11-16 20:54:47.396 INFO  command line: C:\Games\wox\wox.exe --data-dir D:\nowhere
  2026-11-16 20:54:47.396 ERROR game data directory not found: D:\nowhere (from --data-dir)
  2026-11-16 20:54:47.396 ERROR fatal error: exit status 1
  2026-11-16 20:54:47.398 INFO  session end: exit status 1
  ```

- The game's own messages keep their wording (the same text as on stderr). Lines that other parts write to stdout
  or stderr (the MIDI, MT-32, SC-55 and SB Pro back ends) appear as `[stdout] LINE` / `[stderr] LINE`; a
  `[stderr]` line that reports a failure (`unavailable`, `cannot`, `failed`, `missing`, ...) is `WARN`, others `INFO`.
- If no log can be written, the game still runs; a fatal error is still shown (dialog, stderr).

**Fatal errors** (a window session ending with a nonzero status: a missing data directory or CC file, an unknown
option, a `wox.cfg` error, no display, an unsupported game continuation, the original's fatal driver exit):

- Windows: one standard error dialog (error icon, **OK** only) with the error and the log's path. When the game
  window already shows a picture, the dialog belongs to it and the window keeps showing that last picture behind
  it (it is repainted, e.g. after Alt+Tab; nothing is cleared, the game does not advance, the audio is paused).
  **OK** closes the game, with the same exit status. Before a window exists, the dialog appears alone.
- Linux: no dialog; the message on stderr (as before) and in `wox.log`.
- No dialog for a normal end: the window closed, Ctrl+Q, the game's own "Exit to DOS", the XEEN.EXE hand-off at the
  ending (status 3, logged), `--help`, and never in a `--headless` run.

**`--diagnostic-state`** (developer/test control, off by default; not a `wox.cfg` setting): prints the state-summary
records (`program-start accepted=0 map=... boundary=... volleyend=...`, one per host event such as `main-menu`,
`menu-quit`, `host-shutdown`, `combat-state`, `stopped`, with their `combat-rng-sequence=` lines and the
`game-over frames=` summary) on stdout, exactly as every run printed them before Phase 272. In a window session
they also appear in `wox.log` as `INFO  [stdout] RECORD` (the record after `[stdout] ` is the exact payload). The
development test suites pass it with every headless run.

## Environment variables

The developer capture/trace variables `XEEN_RASTER_TRACE`, `XEEN_RASTER_SKIP_TRACE`, `XEEN_EFFECT_FRAMES`,
`XEEN_SKY_CHECK` and the `*_CAPTURE`/`*_LOG` variables of damage_numbers, monster_difficulty,
spell_short_description, mouse_scroll, town_icons and character_details were removed in Phase 270
and have no effect. Phase 331 (owner decision) removed the installation overrides `XEEN_CD_TRACKS`, `XEEN_MT32_ROMS`
and `XEEN_SC55_ROMS`: they have no effect; name the directories in `wox.cfg` `[files]`. The library paths and the
test/evidence variables are unchanged.

## The setup tool (`setup`, Phase 331)

`setup` (Windows `setup.exe`) sits beside `wox` and its DLLs and edits the two configuration files: `wox.cfg` beside
it and the game's `XEEN.CFG`. It is a host tool, not part of the original game and not the original
`INSTALL.EXE`. Windows: native controls, the standard folder dialog, no extra DLL. Linux: GTK 3 (a build dependency
of `setup` only; without the GTK 3 development files CMake skips the target with a message).

**First run** (no `wox.cfg` beside `setup`, or one with an error and the user agrees to recreate it; or the settings
window's **Initial setup...**, Phase 335, after a question that the current `wox.cfg` will be overwritten: the
settings window hides meanwhile, and closing the first run leaves everything as it was):

1. "GOG version" or "Other version" (its note: "You will have to pick the data, save and XEEN.CFG locations
   yourself."). "Recommended" is shown under GOG when `GAME1.GOG` is in `setup`'s folder. GOG
   sets `save_dir` and `xeen_cfg_dir` to `cloud_saves\WORLD` (`cloud_saves/WORLD` on Linux), creates that
   folder and, when it holds no `XEEN.CFG`, the default one (below).
2. "Vanilla version" (every add-on and bug fix off) or "Enhanced version" (all on except `damage_numbers.stack`;
   the three `wizard_eye_plus` styles `black`; its note names Identify and the World map).
3. `wox.cfg` is written from the defaults with these choices and `music_override = auto`; closing the window before the second
   choice writes nothing.

**Settings window** (then, and on every later start), titled `WoX - Setup (1.0.0)` with the setup tool's
own version (Phase 338; the first-run screens and the questions keep their titles):

| Frame | Field | wox.cfg / XEEN.CFG |
|---|---|---|
| XEEN.CFG | XEEN.CFG location (Browse...) | `[files] xeen_cfg_dir`; `auto` shows the Data location's text and is written back while they are equal. A folder without `XEEN.CFG` offers the default one; Create default... also replaces an unusable one (empty or longer than 30 bytes) after a question (Phase 335) |
| XEEN.CFG | Speech: on / off | XEEN.CFG word 0: None (0), PC speaker (1) and 3, 4 (no driver) or unknown values are off |
| XEEN.CFG | Music (Roland Sound Canvas marked **recommended**, Phase 335): No music, AdLib, Sound Blaster, Sound Blaster Pro, Roland MT-32 / LAPC-1, Roland Sound Canvas / GS (SC-55), Pro-Audio Spectrum, Wave Blaster, Roland TAP-10, System MIDI | XEEN.CFG word 4 (None, the PC speaker and unknown values show No music); System MIDI is `music_override = system` |
| Game files | Data, Save, MT-32 roms, SC-55 roms, CD tracks location (Browse...) | `data_dir`, `save_dir` (auto shown as `.`), `mt32_roms_dir` (`roms/mt32`; `roms\mt32` on Windows), `sc55_roms_dir` (`roms/sc55`; `roms\sc55` on Windows), `cd_tracks_dir` (`music`); the default text (on Windows with either separator) is written back as `auto`; Browse... opens at the field's folder, or its nearest existing parent; a chosen folder inside `setup`'s folder is written relative to it |
| Display | Fullscreen, Lock scale, Window scale 1..10, CRT aspect ("Recommended to set window scale to 5 or 10") | `fullscreen`, `locked_scale`, `scale`, `aspect` |
| Enhancements | Addons..., Bugfixes... (each flag with its wox.cfg key; the three Wizard Eye+ styles 1px / 2px / black) | `[addons]`, `[bugfix]`; their OK keeps the change until Save |

Speech and Music are enabled only while a usable `XEEN.CFG` is loaded: 1 to 30 bytes (Phase 335; before it 29 or 30).
The game's start-up (XEEN.DAT 47C1F) zero-fills its 29-byte image and reads at most 29 bytes without checking the
count, so a shorter file (GOG's is 28 bytes) is valid; setup writes a changed one back at its own length (at least 8
bytes, through the music port). Choosing MT-32 or an SC-55
card without the ROMs shows a warning (Save is not blocked), for example "SC-55 ROM directory (D:\\Games\\Xeen\\roms\\sc55) not found - wave plays on Nuked-SC55. The game will not start with this music until the ROMs are there." The system named is the chosen card's (`sc55`, `wave`, `tap10`; `mt32` plays on Munt, whose two ROM files are named when the folder exists but one is missing) (Phase 333A).

**Download** (Phase 335, left of that warning and shown with it) fetches the ROMs from archive.org and puts them in
the MT-32 or SC-55 roms location, creating it: MT-32 `MT-32_v1.07_legacy_ROM_files.zip` (its `MT32_CONTROL.ROM` and
`MT32_PCM.ROM`); SC-55 `Nuked-SC55-CLAP-ROM-files.zip` (the five `.bin` files of
`Nuked-SC55-Resources/ROMs/SC-55-v1.00/`, Nuked-SC55's `mk1-v1.00` set). Every member is checked (CRC-32, size, the
expected count) before anything is written; a ROM file already there is kept. Windows downloads with WinHTTP into
memory (no file is left); Linux runs `curl` (or `wget`) into a temporary file that is deleted afterwards. A failure
shows its reason.

**Save** writes `wox.cfg` in place: comments, blank lines, line endings and every key `setup` does not manage stay
as they were, and only the values that changed are rewritten (a missing key is added to its section). The new text
is checked with the game's own parser before it replaces the file, and `[audio] music_override = auto` (or `system`) and
`speech = auto` are set so that XEEN.CFG's choices take effect. A missing save or XEEN.CFG folder is created on
request. Then `XEEN.CFG` (same file name and length) gets only the changed fields:

- Speech off: word 0 = 0 (the port kept). Speech on when it was off: Sound Blaster Pro (7) at port 220h.
- A changed Music: its word and the port `INSTALL.EXE`'s detection gives that card (AdLib and Sound Blaster 388h,
  Sound Blaster Pro 220h, Pro-Audio Spectrum, Canvas, MT-32 and Wave Blaster 330h; TAP-10 340h, a project choice:
  the installer never detects it). No music: word 0. System MIDI: XEEN.CFG's music is left alone.

**Cancel** (or closing the window) writes nothing; it asks first only when something Save would write was changed
(Phase 335).

**The default XEEN.CFG** is the fresh genuine file's 29 bytes with speech and music Sound Blaster Pro (7 and 3, both at
220h), IRQ 7, DMA 1, no ending seen and the intro not played. Its fields, from INSTALL.EXE's code (Phase 331):

| Offset | Field |
|---|---|
| 00, 02 | speech driver, its port |
| 04, 06 | music driver, its port |
| 08 | speech IRQ (`/SI#`) |
| 0A..11 | the Clouds ending's mark |
| 12 | an installer flag (role not established; never changed by `setup`) |
| 14 | `/V1`: adjust the Sound Blaster volumes (XEEN.DAT's promus mixer flag) |
| 16, 17 | the Darkside and World endings seen |
| 18 | speech DMA channel (`/SD#`) |
| 1A | the intro played |
| 1C | the CD drive (`/CDx`: letter - `A`) |
