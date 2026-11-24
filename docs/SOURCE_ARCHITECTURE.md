# Source architecture

> Part of the public source tree (adapted from the development project's documentation). References to "Phase NNN"
> mark when a behaviour was introduced in the project's development history; the phase reports themselves are not
> part of this tree.

How the native WoX sources are organised, where new code belongs, and which boundaries are
deliberate. Rules for formatting and comments are in [CODING_STYLE.md](CODING_STYLE.md); each function's documentation comment names the original routine (address) it reproduces.

This public version keeps the overview sections; the detailed per-unit notes of the development documentation
are summarised in the source comments.

## Directory tree

```
include/xeen/        public headers of every engine module (one per module); addon/, bugfix/ for those families
src/
  core/              the BIOS tick model (timer.c), CC archives, map records and the resource service (resources.c)
  world/             DS map queries and movement (movement.c + tables), navigation, map entry, post-action
                     continuations, objects and monsters (entities.c), the time tick, the exploration command
                     table (caller.c), the maze map (minimap.c), bash, the monster update after a party action
                     (monster_update.c, original 2A701)
  events/            event record decoding (event_records.c), the interpreter (event_exec.c), dispatch, event text,
                     event resources, event windows
  combat/            combat rules and the combat turn (combat.c), ranged scheduling, projectiles
  characters/        the character record queries and layout (character_record.c), party restore, items and wares,
                     treasure, levels and training costs (town.c)
  game/              the game session: the core game.c and 38 units by responsibility (lifecycle,
                     resources, events, input, scrolls, exploration commands, Rest, the menus, the
                     town services, Cast, combat, ...), and their private header game_internal.h
                     (see below)
  ui/                the party border (interface.c), text windows and their frame tiles, the modal stack,
                     menu file helpers (main_menu.c) and the input queue (input_poll.c)
  graphics/          the rasterizer and its colour modes, page presentation and the window table, the 3D view
                     (scene.c, scene_construct.c) with their original tables, sprites
  audio/             XEEN.DAT side: the voice/effect model (audio.c) and the engine glue (audio_engine.c);
                     beside them the reused XEEN.EXE audio modules (music, synth, MT-32, SC-55, SB Pro, CD,
                     VOC; GPL-3.0-or-later) and the vendored stb_vorbis.c
  player/            Phase 308: the XEEN.EXE movie player (the Darkside and Clouds intros, the three endings;
                     migrated from the separate reconstruction, GPL-3.0-or-later): the scene modules, runtime.c
                     (frames, clocks, the audio sink calls), player.c (xeen_movie_play, include/xeen/movie.h) and
                     player_host.c (the game's window, input and archives behind the former platform calls)
  save/              save files: read, restore, pair build, map archives
  platform/          window, keyboard and mouse backends (platform.c), the F10 screen, the WOX configuration
  app/               executables: the headless/windowed host explore.c (wox; xeen_explore before Phase 309) with its command line
                     (explore_options.c), the movie host (host_movie.c, Phase 308), DOS file access (host_files.c) and evidence writers
                     (host_evidence.c), and the line-mode developer
                     console console.c (xeen_native)
  addon/             owner-approved optional add-ons (off by default), one directory per add-on
  bugfix/            owner-approved bug-fix family (off by default)
  data/              the program data, generated locally from the user's XEEN.DAT (data/README.md) and
                     turned into C at build time
  test_support/      Phase 284: the engine functions only the tests call, one file per module they came from
                     (<module>_test_support.c), built into the xeen_test_support library: every test program
                     links it, the game (wox) and the console (xeen_native) do not. Their declarations
                     stay in the module's header, marked "Test support"
  icon/              window icon generator (build tooling)
```

## Dependency boundaries

The layers below are the intended direction of dependencies, the rule for new code. They are not
machine-checked; the existing code follows them through its headers, except that `game/` reaches into every
layer by design.

| Layer | Modules | May use |
|---|---|---|
| Data and primitives | `core/`, `data/`, `include/xeen/byte_order.h` | the C library only |
| World state | `world/movement.c` (the DS image and its accessors) | core |
| Rules | `world/`, `events/`, `combat/`, `characters/`, `save/` | core, world state, each other through `include/xeen/*.h` |
| Presentation | `graphics/`, `ui/`, `audio/` | core, world state; never the rules' internals |
| Session | `game/` | everything above; it is the only place that sequences them |
| Hosts | `app/`, `platform/` | the session's public API (`xeen/game.h`) and the presentation output |
| Optional | `addon/`, `bugfix/` | hooks the session exposes; normal play is unchanged when they are off |

Every module's public interface is its header in `include/xeen/`. Exported names (`xeen_*`) are an ABI: the
development project's research tools compile against them.
Helpers that a module does not export stay `static`.

The libraries in `CMakeLists.txt` follow the layers: `xeen_movement` (world state), `xeen_resources`,
`xeen_events`, `xeen_event_resources`, `xeen_event_ui`, `xeen_game` (the session with every rule, presentation
and add-on module), `xeen_platform`, `xeen_audio`, `xeen_wox_config`, and the executables.

## Where new code belongs

- A new original routine: the subsystem directory of its responsibility, with its address in the function's
  documentation comment (the function map picks it up).
- A new reader of DS data: the module that owns the data, and its layout in the extractor (`tools/data/`).
- Host or platform behaviour: `platform/` or `app/`, never `game/`.
- An add-on: its own directory in `src/addon/` with its header in `include/xeen/addon/`.
- A function only the tests call (no shipped program reaches it): `src/test_support/<module>_test_support.c`, declared in the module's header under a "Test support"
  marker. A test-only function that needs a module's private symbol stays in the
  module when moving it would export the module's private core (Phase 284: the interpreter's `execute`, combat's
  turn order, ...); a small helper may be shared through the module's private `*_internal.h` (not installed).
