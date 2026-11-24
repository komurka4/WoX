#ifndef XEEN_WORLD_ENTRY_H
#define XEEN_WORLD_ENTRY_H
#include "xeen/caller.h"
#include "xeen/presentation.h"
/* The map entry's finish: where it begins (the continuation after the entry's scene returns, 17A20) and where it
 * hands back to the exploration loop once done (17AF7). */
enum xeen_world_entry_boundary {
    XEEN_ENTRY_FINISH = 0x17a20,
    XEEN_ENTRY_DONE = 0x17af7
};
/* The finish's resumable stages, in order (*stage: the next to run; a stage that has run is never run again). */
enum xeen_world_entry_stage {
    XEEN_ENTRY_STAGE_PORTRAITS = 0,  /* the party's portraits (the first call's) */
    XEEN_ENTRY_STAGE_COMMAND_ICONS,  /* the command icons' sprites */
    XEEN_ENTRY_STAGE_FIRST_SCREEN,   /* window 0 presented */
    XEEN_ENTRY_STAGE_ICONS,          /* the saved windows' test, the two closes and the icons' draw list */
    XEEN_ENTRY_STAGE_ICON_BOX,       /* the icons' window presented */
    XEEN_ENTRY_STAGE_SCREEN,         /* window 0 presented again */
    XEEN_ENTRY_STAGE_BUTTONS,        /* the exploration buttons and the page copy */
    XEEN_ENTRY_STAGE_MONSTERS,       /* the monsters enabled, the exploration mode and its fade */
    XEEN_ENTRY_STAGE_METADATA,       /* the party cell's metadata */
    XEEN_ENTRY_STAGE_STEP_EVENT      /* done but the step event's test (17AE5 or 17AF7) */
};
/* Continuation after scene(0) returns at 17A20. cursor selection records original
 * F460/frame0; actual cursor pixels are supplied by native presentation owner.
 * Stages preserve completed effects across unsupported/modal boundaries.
 * Initial *stage = XEEN_ENTRY_STAGE_PORTRAITS (0); success returns boundary 17AF7. No scene is executed here. */
xeen_caller_result xeen_world_entry_finish(xeen_movement *, xeen_presentation *, unsigned *stage);
/* Original 2ABB8(update) (presentation): the party bar, faces, HP
 * gems and the 96C3 highlight; update nonzero adds 50BF6(21h). */
xeen_status xeen_world_entry_party_bar(xeen_movement *, xeen_presentation *, int update);
/* 2ABB8's draw calls in its order (CAP at least 14), without
 * drawing them. */
xeen_status xeen_world_entry_party_bar_draws(xeen_movement *, xeen_sprite_draw *, size_t cap,
                                             size_t *n);
/* Party member MEMBER's maximum HP for the party bar's HP gems: xeen_combat_max_hp (original 2AAC2), refused with
 * XEEN_SERVICE_REQUIRED where the original's unbounded table scans would run past their ends (an age word of FFFFh,
 * an Endurance at or past every bonus threshold). XEEN_BAD_INPUT for a member past 5. Reads DS only. */
xeen_status xeen_world_entry_max_hp(const xeen_movement *, unsigned member, uint16_t *hp);
#endif
