#ifndef XEEN_POST_ACTION_H
#define XEEN_POST_ACTION_H
#include "xeen/caller.h"
#include "xeen/audio.h"
/* Where the post-action's surface step (17264, the party cell's terrain) ends when it hands a step on (its
 * boundary): the original address of what follows. */
enum xeen_surface_boundary {
    XEEN_SURFACE_DONE = 0x17389,            /* the surface step returned; the caller goes on at 17D50 */
    XEEN_SURFACE_EDGE = 0x17334,            /* the party stepped past the map's edge: the neighbouring map's load */
    XEEN_SURFACE_DAMAGE_SCENE = 0x17344,    /* the surface hurt the party: its damage scene (1B503(1)) runs */
    XEEN_SURFACE_DEFEAT_SCENE = 0x17380,    /* the surface defeated the party (F60A): its scene runs */
    XEEN_SURFACE_DESERT_CLOCK = 0x172cd     /* a desert step without a Navigator: its time charge (30CF3(170)) */
};
/* Resume only after the movement-time prefix reached 17264. Executes original
 * post-action and movement caller until the next unresolved service. */
/* Test support (the xeen_test_support library, src/test_support/; not in the game): */
xeen_caller_result xeen_post_action_after_movement(xeen_movement *);
/* Original 17264 alone (terrain effects of the party cell), as called by
 * Shoot 17EFA. XEEN_OK with boundary 17389 when it returns; its unresolved
 * calls (173A0, 17334 map edge, 17344 damage scene, 17380, outdoor kind 6
 * at 172C3) stop with SERVICE_REQUIRED and their writes. */
xeen_caller_result xeen_post_action_surface_native(xeen_movement *);
/* 172D5 onward, after the caller's 30CF3(170) for a desert cell
 * (xeen_post_action_surface_native returned boundary 172CD). */
xeen_caller_result xeen_post_action_surface_after_desert_native(xeen_movement *);
/* Same gameplay path with an explicit native audio service. */
xeen_caller_result xeen_post_action_after_movement_native(xeen_movement *, xeen_native_audio *);
/* Caller continuation at 17D50 once 17264 has returned (EFEE count, the step
 * sound, the 17F07-side checks), e.g. after the 17344 damage branch, which
 * jumps to 17386 without the 17377 F60A test. */
xeen_caller_result xeen_post_action_after_surface_native(xeen_movement *, xeen_native_audio *);
/* Caller continuation at 17D50 after 17264 returned from a successful map-edge
 * load. Terrain and boundary processing have already run. */
xeen_caller_result xeen_post_action_after_transition_native(xeen_movement *, xeen_native_audio *);
/* Resume exactly at the pending 1881B call; does not replay terrain, counters
 * or the 9775 toggle. Caller must have stopped at that boundary. */
xeen_caller_result xeen_post_action_resume_audio(xeen_movement *, xeen_native_audio *);
/* One-shot original 17DA8 continuation; unsupported writes remain persistent. */
xeen_caller_result xeen_post_action_default_space_native(xeen_movement *, xeen_native_audio *);
#endif
