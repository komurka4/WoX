#ifndef XEEN_ADDON_WORLD_MAP_H
#define XEEN_ADDON_WORLD_MAP_H
#include <stdint.h>
#include "xeen/input_poll.h"
#include "xeen/resources.h"
/* Owner-approved add-on world_map (Phase 226; NOT original XEEN.DAT behavior,
 * off unless --addon world_map or wox.cfg [addons] world_map = on): a
 * full-screen map of the whole current world's surface, opened from the M map.
 *
 *   - The M map (1DA2D) shows a World Map button right of its window's
 *     bottom-right corner (a generated stone icon in main.icn's style, "World
 *     Map" under it, the W in the key colour). W or a click on the button
 *     shows it pressed for one pass, then opens the world map; in the
 *     original both are a map answer that only redraws the map (W) or closes
 *     it (a click outside the window).
 *   - The world map: the 6 x 4 grid of 16 x 16 outdoor maps (maps 1..24) of
 *     the current world (Clouds or Darkside; Darkside's maps 25..28 and
 *     89..112 are left out: owner decision), every sector drawn by the game's
 *     own M map pass (xeen_scene_map, original outdoor.til frames), so a cell
 *     is shown exactly when the original M map shows it (its stepped-on bit
 *     or an always-seen surface); unseen cells stay the window's background.
 *     The party's arrow (global.icn, blinking with the map) when it is on
 *     the grid; Phase 247 (owner): in a town, castle, tower or dungeon the
 *     arrow stands at that place's entrance (the generated anchors.inc; the
 *     X/Y line shows the entrance cell), on the Darkside sky at the cell
 *     beneath; the map shown is the entrance's world (Darkstone Tower, the
 *     Dragon Tower and the Dungeon of Death stand on the Clouds side), built
 *     from the game's copy of that world's state when it is the other one.
 *     A map with no known entrance: the current world, centred, no arrow.
 *   - Zoom: 100% (the original's 10 x 8 cell, default), 50% (5 x 4) and the
 *     whole world (288 x 160); reduced images take each block's mean colour,
 *     mapped to the nearest mm4.pal entry. Mouse wheel (zoom at the
 *     pointer), PgUp/PgDn, + (or =) and -.
 *   - Pan: the arrow keys (4 cells at 100%) and a mouse drag.
 *   - Esc closes the world map and the M map at once (owner decision), as
 *     the original's Esc closes the M map.
 *
 * Host-only: the M map's modal runs unchanged (its passes, its 1B503 scene,
 * its timer and blink); the add-on takes the answers listed above at the
 * map's poll and draws on a copy of the displayed page (the button, or the
 * whole world map screen). The game's DS, framebuffer and saves are never
 * written; the world image is built from a private copy of the map records:
 * the game's state archive, with the loaded outdoor maps' records taken
 * from DS (their seen bits are newer). It depends on no other add-on and on
 * no bug fix (with mouse_scroll on as well, the wheel zooms the world map
 * while it is open and scrolls the lists otherwise).
 *
 * Phase 247: add-on quest_journal (xeen/addon/quest_journal.h) opens the
 * world map from the Quests dialog with a red ring on a target, the zoom
 * chosen to show the party and the target, its label on the bottom line;
 * Esc there returns to the Quests dialog.
 *
 * Code: src/addon/world_map/ (world_map.c the module, map_modal.inc the M
 * map's part included by the game, archives.inc a world's archives for the
 * game-side callers, anchors.inc the generated entrances, icon.inc +
 * make_icon.py the button). */

/* Nonzero while the add-on is on (xeen_world_map_start). */
extern int xeen_world_map_on;

struct xeen_movement;
/* The host's wheel: one pending notch (+1 up, -1 down, 0 none) and its
 * logical point (xeen_platform_take_wheel); NULL: no wheel (headless). */
typedef int (*xeen_world_map_take)(void *user, uint16_t *x, uint16_t *y);
/* Turns the add-on on. With TAKE, *PROVIDER (copied) is wrapped: while the
 * world map is open a notch becomes its zoom key; otherwise notches pass to
 * the wrapped provider's keyboard and those it leaves are dropped. 0 (off)
 * when the images cannot be allocated. */
int xeen_world_map_start(xeen_input_provider *provider, xeen_world_map_take take, void *take_user);
void xeen_world_map_stop(void);

/* Hooks of the M map modal (map_modal.inc). */
/* A pass of the M map was drawn (BLINK its arrow phase; ASSETS the world's
 * CC, the button's font). */
void xeen_world_map_pass(int blink, const xeen_archive *assets);
/* The M map closed: the world map too, nothing shown. */
void xeen_world_map_closed(void);
int xeen_world_map_is_open(void);
/* W or a click on the button: the button is drawn pressed for one map pass
 * (as the game's buttons animate a press); the world map opens at the next
 * answer of the map's poll. */
void xeen_world_map_press(void);
int xeen_world_map_is_pressed(void);
/* The button's rectangle (the icon and its label) holds the point. */
int xeen_world_map_button_hit(uint16_t x, uint16_t y);
/* Who opened the world map (xeen_world_map_owner: NONE while it is closed). */
enum { XEEN_WORLD_MAP_OWNER_NONE, XEEN_WORLD_MAP_OWNER_MAP, XEEN_WORLD_MAP_OWNER_QUESTS };
/* A place on the world map: WORLD (0 Clouds, 1 Darkside), world cell X 0..95
 * (west to east), Y 0..63 (from the south), DIR the facing 0..3 (N E S W). */
typedef struct {
    int world, x, y, dir;
} xeen_world_map_place;
/* The party's place for M's game: outdoors its cell, on the Darkside sky the
 * cell beneath, inside a map with a known entrance that entrance (with the
 * party's facing). 0 when it has none. */
int xeen_world_map_locate(const struct xeen_movement *m, xeen_world_map_place *party);
/* Opens the world map of WORLD for OWNER: STATE that world's state archive,
 * ASSETS its CC, LIVE its running game when it is the current world (the
 * loaded outdoor maps' newer records; NULL for the other world). PARTY (may
 * be NULL) is drawn when it is in WORLD; MARK (may be NULL) gets the red
 * ring, and the zoom/centre then show the party and the mark; LABEL (4E38C
 * text, may be NULL) is the bottom line of OWNER_QUESTS. 0 when it cannot be
 * built (the caller stays as it is). */
int xeen_world_map_open_world(int owner, int world, const xeen_archive *state,
                              const xeen_archive *assets, const struct xeen_movement *live,
                              const xeen_world_map_place *party, const xeen_world_map_place *mark,
                              const char *label);
int xeen_world_map_owner(void);
/* An answer of the owner's poll while the world map is open: KEY (0:
 * none), the sampled pointer, CLICK this answer was a click, HELD a button
 * is still down. Returns 2 for Esc (the world map closed: the M map closes
 * too, the Quests dialog stays), otherwise 1 (stay open). */
int xeen_world_map_input(uint16_t key, uint16_t px, uint16_t py, int click, int held);
/* The page to show instead of PAGE: PAGE itself while nothing is shown,
 * otherwise a copy with the button, or the world map screen (from the M map
 * while it is shown, from the Quests dialog while it is open). */
const uint8_t *xeen_world_map_page(const uint8_t *page);
#endif
