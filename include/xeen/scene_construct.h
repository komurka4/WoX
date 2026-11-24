#ifndef XEEN_SCENE_CONSTRUCT_H
#define XEEN_SCENE_CONSTRUCT_H
#include "xeen/caller.h"
#include "xeen/scene.h" /* enum xeen_scene_stage: the boundaries these stages return */
#include "xeen/presentation.h"
/* Original 22034..22B25: 25 map queries and terrain resource/frame/flag
 * selection. Caller must own initialized original scene tables and map cache.
 * Resource words remain original handles; no resolution or rendering is claimed.
 * Stops before visibility/geometry processing at 22B25. Earlier writes survive
 * a query failure; owner must not restart a partially executed call. */
xeen_caller_result xeen_scene_construct_terrain(xeen_movement *);
/* Original 22B25..2469A: 39 visibility rules (frame words of later records),
 * surface tail 243EC..245F9, then coordinate queries 1C844 at 24603 and, when
 * byte 9C6E is set, at 24662 for navigation index7. Writes surface/terrain words
 * 0962/096C/096E/0978/0984 and AB4=7. Stops before renderer call 2469A.
 * A1C844 failure keeps earlier writes (boundary 1C844); never restart it. */
xeen_caller_result xeen_scene_construct_geometry(xeen_movement *);

/* Reads DS only. Rejects (BAD_INPUT) a list with no terminator before DS end
 * or more than XEEN_DRAW_LIST_MAX drawn entries; out is then unspecified. */
xeen_status xeen_scene_collect_draw_list(const xeen_movement *, uint16_t window, uint16_t list,
                                         xeen_draw_list *out);
/* Original 2469F..246CC after renderer return: party-selection scan writes
 * 97F7 (any nonzero 9744+i for i < signed count ED8C) and copies it to 97F9.
 * Returns to caller continuation 1BED9. */
xeen_caller_result xeen_scene_construct_finish(xeen_movement *);
/* Outdoor 246CD..2538B: 25 terrain slots (records 118A..), 1C952, 23
 * wall-feature queries (F40A[F48E]) and the sky/ground flag words. Stops
 * before renderer call 2538B (list 1166, window 3). A query failure keeps
 * earlier writes (boundary 1C1C1); never restart a partial call. */
xeen_caller_result xeen_scene_construct_outdoor(xeen_movement *);
/* Original 25390..253BF after the outdoor renderer: same party scan as
 * 2469F; returns to the outdoor caller continuation 1BAA2. */
xeen_caller_result xeen_scene_construct_outdoor_finish(xeen_movement *);
#endif
