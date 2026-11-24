#ifndef XEEN_BUGFIX_LEFT_CLIP_COLORS_H
#define XEEN_BUGFIX_LEFT_CLIP_COLORS_H
/* Bug fix left_clip_colors (Phase 237; owner-approved, NOT original behavior, off by default).
 * The original's scaled 2000h clip-in copy straddle (4BBCE..4BBDA: SUB SI,2; MOV SI,[SI]) takes the
 * pixels of a copy that crosses the 3D view's left clip (column 8) from the back word's ABSOLUTE offset,
 * i.e. from an unrelated part of the sprite: stray colours at view columns 9..16 on monsters and objects
 * cut by the left edge. With this bug fix on, the copy reads from position - back like every other copy.
 * The flag is defined in src/graphics/raster.c (compiled standalone by tools) and set by the registry
 * (src/bugfix/bugfixes.c). Evidence: docs/phases/PHASE237_CLASS_A_RASTER_CORRECTION_AND_GATE_RELIABILITY.md. */
extern int xeen_bugfix_left_clip_colors_on;
#endif
