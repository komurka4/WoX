#ifndef XEEN_BUGFIXES_H
#define XEEN_BUGFIXES_H
#include "xeen/bugfix/left_clip_colors.h"
#include "xeen/bugfix/quest_up_down.h"
#include "xeen/bugfix/spell_description.h"
#include "xeen/bugfix/sprite_width_trim.h"
/* Owner-approved bug fixes: NOT original XEEN.DAT behavior. Each
 * corrects something the original game gets wrong, is off by default and is
 * enabled only by the command line (--bugfix NAME..., several names,
 * repeatable) or wox.cfg ([bugfix] NAME = on). With every bug fix off the
 * game is the original.
 *
 * This is the shared registry (src/bugfix/bugfixes.c): the names and the
 * enabled flags. Each bug fix is its own module, independent of the add-ons
 * (include/xeen/addons.h) and of the other bug fixes:
 *   spell_description  corrected spell descriptions
 *                 xeen/bugfix/spell_description.h, src/bugfix/spell_description/
 *   left_clip_colors   the 3D view's left-clip copy reads its own pixels (Phase 237 class A)
 *                 xeen/bugfix/left_clip_colors.h; its guard is in src/graphics/raster.c
 *   sprite_width_trim  scaled rows trimmed to the cell's declared width (Phase 237 class B)
 *                 xeen/bugfix/sprite_width_trim.h; its guard is in src/graphics/raster.c
 *   quest_up_down  U and D scroll the Quests dialog as its Up/Down labels say (Phase 269)
 *                 xeen/bugfix/quest_up_down.h, src/bugfix/quest_up_down/; its hook is in src/game/game_quests.c
 * The original code calls them only through one-line hooks guarded by the
 * enabled flag. */
#define XEEN_BUGFIX_NAMES "spell_description left_clip_colors sprite_width_trim quest_up_down"
/* A name of XEEN_BUGFIX_NAMES -> sets its flag; 0 for an unknown name. */
int xeen_bugfixes_enable(const char *name);
#endif
