# Coding style for the native C11 sources

> Adopted in Phase 219. It applies to the authored engine sources under `src/` and `include/xeen/`. The fidelity rules are
> summarised in [DEVELOPING.md](DEVELOPING.md); this file only covers how the code is written.

## Formatting

- **Formatter:** clang-format **18.1.8**, configuration in [`.clang-format`](../.clang-format). Paths it does not
  format are listed in [`.clang-format-ignore`](../.clang-format-ignore): the vendored `src/audio/stb_vorbis.c`,
  the hand-laid data tables (`src/*_tables.inc`, `scene_visibility.inc`, `text_frame.inc`,
  `window_table.inc`), the generated `src/data/` sources and the add-on and bug-fix families (`src/addon/`,
  `src/bugfix/` and their headers), which keep their own scope.
- The pinned binary is the PyPI wheel `clang-format==18.1.8` (manylinux x86-64, SHA-256
  `2de122b8…21e5`). Format a file with `clang-format -i FILE`; check with `clang-format --dry-run -Werror FILE`.
- Four-space indentation, no tabs, 100 columns (trailing comments may run longer; comments are not reflowed).
- Function braces on their own line; control-statement braces on the same line. Every control body has braces
  (`InsertBraces`). One statement per line.
- `#include` order is never rearranged by the formatter (`SortIncludes: Never`).

## Naming

- Functions and variables: descriptive `snake_case`. Constants and macros: `UPPER_SNAKE_CASE`.
- Exported functions keep the `xeen_` prefix and their subsystem (`xeen_combat_…`, `xeen_item_…`). Their names are
  an interface: the historical evidence runners compile against `include/xeen/`, so an exported name is only renamed
  with a compatibility plan.
- Internal (`static`) functions are named for what they do, not for the original address they reproduce:
  `apply_terrain_damage`, not `tail_39413`. The original address goes in the documentation comment.
- Short names are fine for a tight scope (`i`, `k`, `n`, `m` for the `xeen_movement` state, `g` for the game,
  `c` for combat); a value that lives across a long function gets a real name.
- Register names (`ax`, `si`, `cx`) only where the code models that register: the event executor's AX, a
  service's AX output, the RNG's register mix, the raster drivers. A dialog's answer is `answer`; a distance step
  the original keeps in SI is `step`, with the register named in the comment.
- Avoid abbreviations that read like assembly labels: the game TU's town-location code is `location_*`, not `loc_*`.

## Documentation comments

Every function has a comment directly above its definition (or its prototype, for an exported function declared in
a header) that says what it is responsible for:

```c
/** Reads a 16-bit little-endian value from an unaligned byte buffer. */
static uint16_t read_u16_le(const uint8_t *data);

/**
 * Generates one random item of LEVEL into OUT (category, id, enchantment).
 *
 * Original 305E8. Consumes the RNG through DRAW exactly as the original's
 * draws, in order; returns the item category.
 */
```

- Say what the function does, its inputs and outputs, side effects on shared state, return values and failure
  behaviour when they are not obvious. Do not narrate the implementation line by line.
- **Keep the original's evidence:** the original address(es) the function reproduces (analysis placement
  `seg*16+off`: the original program loaded at linear address 10000h), deliberately retained quirks, fixed-width arithmetic, binary layouts and
  ordering constraints. These are maintenance facts, not history.
- **Drop the history:** which phase found something, discarded hypotheses, debugging notes and commented-out code
  belong in the change history, not in the source.
- Never write the word "scenario" in engine sources (a CTest asserts that the engine has no scenario code path).

## Code

- C11, fixed-width integer types wherever the original's representation matters. Keep the original's widths,
  signedness, truncations and RNG call order; a rewrite that is "mathematically the same" is not acceptable unless
  the C semantics are shown to be identical.
- Binary layouts: a field whose meaning the original's readers establish is named once, in the owning module's
  header, with its unit and evidence (`xeen/character_record.h`: the character record's fields and the condition
  indexes; `xeen/party.h`: `XEEN_PARTY_RECORDS`, `XEEN_CHARACTER_BYTES`). Unknown bytes stay hexadecimal; no
  name is chosen from appearance, and no accessor function wraps a plain byte read. A name gives the offset
  only: a field the original reads both signed and unsigned (HP, SP) keeps each reader's own conversion. Words
  and dwords are read and written with `xeen/byte_order.h`, not hand-written byte pairs. The layouts are documented in
  `include/xeen/character_record.h`.
- DS variables: an offset that every original reader uses as one variable is named in `xeen/ds_globals.h`. An address with more than one role keeps its number at the
  sites of the other role. For example, ED8C is the party count as a byte, but `ds + 0xed8c` is the `maze.pty`
  block. F604 is the mouse x word in the click tests but an item category index in the item code.
- A DS far pointer is named by its offset word; its segment word is written `NAME + 2u`
  (`XEEN_DS_BUTTON_X_TABLE + 2u`).
- An index the original uses into several of its own tables is an `enum` whose names come from the original's
  data (the spell numbers from `spells.xen`, `xeen/spells.h`; the location kinds from each kind's resources and
  handler). A value without an established meaning keeps its number at its site (location kind 7).
- A selection that many original routines repeat may be one `static inline` accessor beside the names it reads,
  with the original's index width (`xeen_active_party_slot`: the combat party in combat, the position
  elsewhere). A plain byte read gets no accessor.
- An internal function's result codes are named (an `enum` beside the function) only when every value is
  established: the original's own return values, plus native-only values marked as such. The integer
  representation and the signature stay (`TAKE_GIVE_*` in `event_exec.c`). A resumable state byte's values are
  named the same way, after a census of every store (`LOOP_PENDING_*`, `PICK_*`).
- A dispatcher over original keys names its keys (an `enum` of character constants and scan words, each with its
  handler address) and keeps the original's test order; one helper per original handler, one exit for the shared
  bookkeeping (`dispatch_command` in `combat.c`). A stop address stays a literal in its own `fail(r, ADDR)` /
  `stop(...)` call at the guard: `stops.py` registers stops from those call sites, so never pass the address
  through a helper's parameter. A host service site that stops when not served is one line:
  `if (r != 1) return r ? dispatch_internal(c) : stop(c, DISPATCH_STOP_NOTHING_HERE);` (`event_dispatch.c`; a
  literal `0xADDRu` before Phase 260). The same holds for a `boundary = 0xADDRu` stop next to its `*_UNSUPPORTED`
  status: an extracted gate keeps one such pair per case (`accept_operation`), never a shared helper.
- **Named stop sites (Phase 260).** A stop's address may be spelled as a named constant that says what could not
  be done: an `enum` in the owning module, one definition per line, `MODULE_STOP_WHAT = 0xADDR, /* what failed */`
  (the name contains `_STOP_`; `game_arena.c`'s `ARENA_STOP_*`). The name is used at the guard exactly where the
  literal was (`return arena_stop(g, ARENA_STOP_TRAVEL_REFUSAL);`), and a callee that produces a boundary for its
  caller returns it by name (`return ARENA_STOP_OPPONENT_RECORD;`: the `return` idiom, which registers that
  producing site, invisible as a literal). `stops.py` resolves the names before matching, so the registry stays keyed
  by address and records each location's `name`; `stops.py check` fails on a name defined twice, used without a
  definition, or defined but unused; `stops.py identity BASE.json` proves that every base site keeps its address
  and owner. Name a site by the step that failed (a text, a map load, a roll), never `STOP_36414`; the value keeps
  the original address, so every trace reports the same boundary.
- **Variable stop sites, call sites and waits (Phase 262).** A `_STOP_` name may also stand in a variable stop
  site's selector or default (`gate ? CUT_STOP_GATE_MAP_RELOAD : CUT_STOP_REAPER_MAP_RELOAD`,
  `boundary ? boundary : TREASURE_STOP_FINISH`) or be a parameter site's argument
  (`cast_volley(..., VOLLEY_STOP_MEGA_VOLTS)`): `stops_variable.py` expands it, so the entry keeps its key. Every
  use of a `_STOP_` name must be a registered stop where it stands (`stops.py check`): a stops.json location on that
  line, a stops_variable.json site of that function, or a parameter site's address at a call of its function in the
  same statement. A name without `_STOP_` inside a selector changes the entry's key: keep that operand literal
  (`id == 44u`). An original call that the audio model or a trace records by its return address (a voice query, a
  CD status, a voice start) is a file-local `*_CALL` name; a wait site that a resume or the host compares is a
  `*_BOUNDARY` name in `game_internal.h`; the host asks `xeen_game_site_kind()` (game.h) what kind of wait the game
  reports instead of comparing addresses. A window many screens share is named by its place
  (`XEEN_WINDOW_RIGHT_COLUMN`), a window with one role by that role.
- **Stops handed back, and the generic stop (Phase 263).** A function that reports a stop to its caller through a
  `uint32_t stop` local or a `uint32_t *stop` out-parameter assigns a name: `*stop = TAKE_STOP_ITEM_REMOVE;`,
  `stop = w ? PAIR_IDS_STOP_READ_DARK : PAIR_IDS_STOP_READ_CLOUDS;` (the `producer` idiom: `stops.py` registers the
  producing site; a literal producer fails `check` with PRODUCER, a name assigned to any other lvalue fails the names
  rule). `game_stop(g, NAME)` (game.c; `shoot_stop` before Phase 263) is the lifecycle's own stop: it records the
  boundary, marks the command unsupported and stops the game; use it instead of spelling its three stores. A
  continuation or wait the host resumes is a `*_BOUNDARY` name, never a `_STOP_` one; a protocol result of a library
  belongs in that library's public enum (`XEEN_POLL_QUIT`, `XEEN_MONSTER_UPDATE_WATER_RECORD`), not game_internal.h.
- A dispatcher whose original routine ends at several shared tails makes each tail explicit: a case returns the
  tail's result (`take_give`'s give returns `TAKE_GIVE_DONE` for 2DC50 or `given_with_flash()` for 2DBB8) instead
  of a `break` into common code. Operand numbers shared by several original tables (the event variables of
  If/take/give/SetVar) get one `enum` (`VAR_*`), named only where every table that lists a number agrees.
- A stage extracted from a resumable step (one call of a poll owner, `poll_modal`) returns `xeen_status`, and
  `XEEN_OK` means "go on with the step"; any other value is what the step returns at that point
  (`modal_face_scene`, `present_who_will_form`, `start_modal_key_poll`). Use it only where the step itself never
  returns `XEEN_OK` there. A value the original read before a call that follows stays read there and is passed in
  (`animate_kept_face`'s frame counter, read before the 4EA1C sample).
- A fact a tool needs about an implementation (the take/give modes `world_exceptions.py` classifies) is an
  explicit contract file beside the code (`src/events/take_give_modes.json`), checked against the code's
  behaviour by a test and against the original by the tool; tools never parse a function's text.
- A value the original compares signed is converted once where it is loaded (`int mouse_x = (int16_t)...`), not
  cast at every use; wraps the original relies on (`(int16_t)(x + width)`) stay explicit.
- Straightforward procedural C. No framework or indirection layer to make functions shorter; a helper must have a
  clear responsibility and a name that says it.
- Behaviour-preserving edits (formatting, comments, static renames) should leave the compiled objects unchanged;
  compare them before and after (the development project uses an object-equivalence tool for this).
- Source-reading tests and tools must not depend on layout: match whitespace-free text or statements, not lines.

