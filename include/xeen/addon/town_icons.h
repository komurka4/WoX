#ifndef XEEN_ADDON_TOWN_ICONS_H
#define XEEN_ADDON_TOWN_ICONS_H
#include <stddef.h>
#include <stdint.h>
/* Owner-approved add-on town_icons (Phase 207; NOT original XEEN.DAT
 * behavior, off unless --addon town_icons or wox.cfg [addons] town_icons =
 * true): small icons for the town places on the M map and on the minimap.
 *
 *   blacksmith  anvil             tavern (also the inn)  mug
 *   temple      gold cross        guild                  purple hat
 *   training    sword             bank                   gold coin
 *   mirror      gold-framed oval  tent                   Clouds red/white, Darkside teal/brown
 *   pyramid     Castleview's (the visible one; not Shangri-La's, owner decision)
 *   tower       grey stone tower  Phase 309: Castleview (12,10), Ellinger's Tower (owner
 *               decision; Phase 207 had no icon for its Reaper)
 *
 * The cells (table.inc) come from the original resources (make_table.py):
 * event op 17 kinds 0..5 and 12, op 21 type 0 (the mirrors), the tent
 * objects; 14 town maps of both worlds. Phase 309 (owner): Castleview's
 * tower cell and the Skyroad's tents (Darkside outdoor maps 89..112; the four
 * on map 107, Skyroad E3, also listed in the coordinates of the 8 maps around
 * it, where an outdoor view shows them across its edges).
 *
 * An icon shows exactly where the view itself draws its cell as seen (owner
 * decision A: the game's own seen record): on the M map (1DA2D) a cell whose
 * 1C680 lookup is not 8888h with F492 set (the stepped-on bit, set only with
 * a Cartographer, or a surface that is always seen; outdoors 1DB8E's F492);
 * on the minimap (1E467, outdoors 1E4E3,
 * only while it is on: ED9F or EDA0) the same, or Wizard Eye (EDA0), which
 * the original ignores while the = automap ED9F is on (owner decision: no
 * extra icons then). No icon on the party's own cell (its arrow stays
 * visible).
 *
 * Host-only drawing: the hooks after the map and minimap passes record the
 * icons of that pass (lookups on a private copy of the game state) and the
 * pixels under each; the host draws an icon on a copy of the displayed page
 * only while those pixels are still there (dialogs, other screens, a stale
 * page: no icon). The game's DS, framebuffer and saves are never written. It
 * depends on no other add-on and no bug fix.
 *
 * Code: src/addon/town_icons/ (overlay.c, icons.inc the pixel art,
 * table.inc the cells, make_table.py its generator). */

/* Nonzero while the add-on is on. */
extern int xeen_town_icons_on;

struct xeen_movement;
int xeen_town_icons_start(void);
void xeen_town_icons_stop(void);
/* Hooks: the minimap pass (1E467) and the M map pass (1DA2D, VIEW_X/VIEW_Y
 * the origin it was drawn with) have drawn into FRAMEBUFFER (320x200). */
void xeen_town_icons_minimap(const struct xeen_movement *,const uint8_t *framebuffer,size_t size);
void xeen_town_icons_map(const struct xeen_movement *,int view_x,int view_y,const uint8_t *framebuffer,size_t size);
/* The page to show instead of PAGE: PAGE itself while no icon is shown,
 * otherwise a copy with the icons. */
const uint8_t *xeen_town_icons_page(const uint8_t *page);
#endif
