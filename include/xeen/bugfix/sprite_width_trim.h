#ifndef XEEN_BUGFIX_SPRITE_WIDTH_TRIM_H
#define XEEN_BUGFIX_SPRITE_WIDTH_TRIM_H
/* Bug fix sprite_width_trim (Phase 237; owner-approved, NOT original behavior, off by default).
 * The original's scaled frame path without 2000h (4B028..4B1CB) has no width clip: a row whose operations
 * run past the cell's declared width also draws those source columns (tower.vga, the Reaper castle zoom:
 * one pixel more on five rows of its right edge). With this bug fix on, a row is trimmed to the declared
 * width. The flag is defined in src/graphics/raster.c (compiled standalone by tools) and set by the
 * registry (src/bugfix/bugfixes.c). Evidence: docs/phases/PHASE237_CLASS_A_RASTER_CORRECTION_AND_GATE_RELIABILITY.md. */
extern int xeen_bugfix_sprite_width_trim_on;
#endif
