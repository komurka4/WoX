#ifndef XEEN_ADDON_WIZARD_EYE_PLUS_H
#define XEEN_ADDON_WIZARD_EYE_PLUS_H
#include <stddef.h>
#include <stdint.h>
#include "xeen/presentation.h"
/* Owner-approved add-on wizard_eye_plus (Phase 318; NOT original XEEN.DAT behavior, off unless --addon
 * wizard_eye_plus or wox.cfg [addons] wizard_eye_plus = on): while Wizard Eye is on (EDA0) the original's minimap
 * shows every cell; a click on it (the = automap, ED9F) hides again the cells the party has not seen (1E467's fog,
 * outdoors 1E4E3's empty backdrop). With the add-on those cells show what Wizard Eye would show through a
 * checkerboard, by the map's kind (wox.cfg [addons], the default black for each):
 *   wizard_eye_plus.indoor   indoor maps          1px   every other pixel the content, the others the original's
 *   wizard_eye_plus.outdoor  outdoor maps         2px   the same in 2x2 blocks
 *   wizard_eye_plus.sky      sky maps (below)     black every other pixel the content, the others black (index 0)
 * Sky maps (sky_maps.inc, make_table.py): every map where the party can step onto a cell it falls through (sky.srf,
 * or cloud.srf without Levitate): Clouds' cloud maps, the Skyroad, the Elemental Planes, Olympus, Castle Alamar,
 * Dragon Clouds, the Clouds of the Ancients; outdoor or indoor, a sky map takes the sky style.
 *
 * The effect covers exactly the minimap pixels where the original (= on) and Wizard Eye's whole map differ, inside
 * the minimap's box (seen cells, the frame and the party's arrow are the original's). Without Wizard Eye, or with
 * the = automap off, nothing changes.
 *
 * Host-only drawing, as town_icons: the hooks around the minimap pass (exploration 1E467 and the combat pass)
 * record that pass's pixels and the effect's; Wizard Eye's whole map is drawn by the presentation's own sprite
 * renderer into the add-on's private page (no draw trace, no effect RNG, the game's framebuffer untouched). The last
 * four passes with the effect are kept (a page shown around a pass, e.g. while turning, can still be an earlier
 * pass's); a pass that draws no minimap (a window open: 1E467 skips itself) keeps them, one without the effect
 * (Wizard Eye or the automap off, nothing hidden) drops them. The host shows the effect on a copy of the displayed
 * page cell by cell: each of the 49 cells that still shows a kept pass's pixels (the newest such pass; a pass only
 * when at least four of its cells are on the page) gets that pass's effect; a covered cell keeps the page's pixels.
 * The game's DS, framebuffer, saves, RNG and traces are never written. It depends on no other add-on.
 *
 * Code: src/addon/wizard_eye_plus/ (wizard_eye_plus.c, sky_maps.inc and its generator make_table.py). */

/* The styles (wox.cfg values) and the map kinds that select one. */
enum { XEEN_WEP_BLACK, XEEN_WEP_1PX, XEEN_WEP_2PX, XEEN_WEP_STYLES };
enum { XEEN_WEP_INDOOR, XEEN_WEP_OUTDOOR, XEEN_WEP_SKY, XEEN_WEP_KINDS };
/* The minimap's box the effect works in: x 222..319, y 2..78 (the frame at (DFh, 3), the cells at (EDh, 0Ch), the
 * surfaces and edge walls half a cell beyond). */
#define XEEN_WEP_BOX_X 222
#define XEEN_WEP_BOX_Y 2
#define XEEN_WEP_BOX_W 98
#define XEEN_WEP_BOX_H 77

/* Nonzero while the add-on is on. */
extern int xeen_wizard_eye_plus_on;

struct xeen_movement;
/* Starts the add-on with the style of each map kind (STYLES[XEEN_WEP_INDOOR..SKY], XEEN_WEP_* values; NULL: black
 * everywhere). Returns 0 when its memory cannot be had (the add-on stays off). */
int xeen_wizard_eye_plus_start(const uint8_t styles[XEEN_WEP_KINDS]);
void xeen_wizard_eye_plus_stop(void);
/* Hooks around a minimap pass: BEGIN before its draws reach the framebuffer (the page under the minimap is kept when
 * the effect applies: Wizard Eye and the = automap on), END after them with DRAWN nonzero when a minimap was drawn
 * (0: no draws, or a refused or fatal draw). */
void xeen_wizard_eye_plus_minimap_begin(const struct xeen_movement *, const xeen_presentation *);
void xeen_wizard_eye_plus_minimap_end(const struct xeen_movement *, xeen_presentation *, int drawn);
/* The page to show instead of PAGE: PAGE itself while no effect is shown, otherwise a copy with it. */
const uint8_t *xeen_wizard_eye_plus_page(const uint8_t *page);

/* The parts, for the tests: whether WORLD's map MAP is a sky map; the map kind of the party's map in DS (a sky map,
 * else outdoor by F608, else indoor); and the effect over the box: OUT gets MASKED with every pixel that differs
 * from FULL replaced as STYLE says (each a 320x200 page; only the box is written). */
int xeen_wizard_eye_plus_sky_map(unsigned world, unsigned map);
unsigned xeen_wizard_eye_plus_kind(const uint8_t *ds);
void xeen_wizard_eye_plus_compose(const uint8_t *full, const uint8_t *masked, uint8_t *out, unsigned style);
#endif
