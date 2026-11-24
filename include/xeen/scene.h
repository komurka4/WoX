#ifndef XEEN_SCENE_H
#define XEEN_SCENE_H
#include <stddef.h>
#include "xeen/caller.h"
/*
 * The 3D scene draw's stages (the original 1B503 after its scene-timer mark), by the original address each stage
 * hands over to: a stage returns the next one's value as its boundary (a stage that ends early returns its own stop
 * instead, or a query's: enum xeen_query_boundary). Indoors: prepare the render, scan the view walls, the effects,
 * the monsters, objects and walls, the attacking group's shuffle, present the frame, the terrain, the geometry, the
 * renderer, the party scan, restore the group; outdoors: the effects, monsters, objects, shuffle, present, terrain,
 * renderer, party scan and restore. Both then animate, draw, and wait for the scene timer.
 */
enum xeen_scene_stage {
    XEEN_SCENE_PREPARE_RENDER = 0x1b70f,        /* the render records and orientation priorities */
    XEEN_SCENE_VIEW_WALL_SCAN = 0x1c9d4,        /* indoors: count the wall kinds of the view slots */
    XEEN_SCENE_INDOOR_EFFECTS = 0x1bbdf,        /* indoors: effect lifetimes and paired records */
    XEEN_SCENE_OUTDOOR_EFFECTS = 0x1b7f1,       /* outdoors: the effect records */
    XEEN_SCENE_OUTDOOR_MONSTERS = 0x2126c,      /* outdoors: the view slots and monster records */
    XEEN_SCENE_OUTDOOR_OBJECTS = 0x219e4,       /* outdoors: the object records */
    XEEN_SCENE_OUTDOOR_SHUFFLE = 0x1b9b6,       /* outdoors: the attacking group's shuffle */
    XEEN_SCENE_PRESENT = 0x4e33f,               /* the shuffle is done: the frame is presented before the terrain */
    XEEN_SCENE_INDOOR_TERRAIN = 0x22034,        /* indoors: the terrain's map queries */
    XEEN_SCENE_INDOOR_GEOMETRY = 0x22b25,       /* indoors: visibility and geometry */
    XEEN_SCENE_INDOOR_RENDER = 0x2469a,         /* indoors: the renderer draws the built view (list 962h) */
    XEEN_SCENE_INDOOR_RESTORE = 0x1bed9,        /* indoors: the party scan is done; restore the shuffled group */
    XEEN_SCENE_OUTDOOR_TERRAIN = 0x246cd,       /* outdoors: the terrain slots and wall features */
    XEEN_SCENE_OUTDOOR_RENDER = 0x2538b,        /* outdoors: the renderer draws the built view (list 1166h) */
    XEEN_SCENE_OUTDOOR_RESTORE = 0x1baa2,       /* outdoors: the party scan is done; restore the shuffled group */
    XEEN_SCENE_ANIMATE = 0x1b0f6,               /* the frame counters and monster animation */
    XEEN_SCENE_DRAW = 0x1e467,                  /* the animation is done: the scene is drawn */
    XEEN_SCENE_EVENT_REFRESH = 0x1c19b,         /* an event script is running (UI mode 9): the event refresh 4A1B7
                                                 * runs before the frame wait */
    XEEN_SCENE_FRAME_WAIT = 0x1c1b3,            /* the scene waits until its timer shows two ticks */
    /* Phase 262: the indoor view's helper stages and the tail between the drawing and the frame */
    XEEN_SCENE_INDOOR_MONSTERS = 0x1ed46,       /* indoors: the view slots and monster records (then the objects) */
    XEEN_SCENE_INDOOR_OBJECTS = 0x20011,        /* indoors: the object records (then the walls) */
    XEEN_SCENE_INDOOR_WALLS = 0x208fb,          /* indoors: the wall records (then the shuffle) */
    XEEN_SCENE_INDOOR_SHUFFLE = 0x1bde4,        /* indoors: the attacking group's shuffle */
    XEEN_SCENE_DARKEN = 0x1bf62,                /* the scene is drawn: the view darkens by the light level */
    XEEN_SCENE_SUFFIX = 0x1bf6e,                /* the darkening is done: the scene's suffix */
    XEEN_SCENE_SUFFIX_PRESENT = 0x1c112,        /* the suffix is done: its windows are presented */
    XEEN_SCENE_ANIMATE_NO_FRAMES = 0x1b4ef      /* a wall item without frames: the original divides by zero here */
};
/* The scene after its BIOS sample 4EA35, from 1B510: with the view window (9877) open, the early RETF to the command
 * poll (boundary 17B09: the caller's input checks still follow); else the prefix 2A701 (a failure returns its status
 * at boundary 2A701), then every map object's animation (animate_object: the frozen flag 3068, the facing and the
 * event's selected object), and boundary 1B70F (XEEN_SCENE_PREPARE_RENDER: the render is the caller's next stage).
 * No argument: XEEN_BAD_INPUT at 1B510. This continuation never declares the whole scene complete. */
xeen_caller_result xeen_scene_after_timer(xeen_movement *);
/* Resume exactly at 1B70F. Prepares original render records and orientation
 * priorities. The F608==0 branch stops before 1C9D4 (callsite 1BBDA); the
 * outdoor F608!=0 branch stops 1B7F1 before its effect loop (earlier phases
 * had these labels swapped). No scene completion is implied. */
xeen_caller_result xeen_scene_prepare_render(xeen_movement *);
/* Execute 1C9D4, the indoor view scan: clears the view wall flags (XEEN_DS_VIEW_WALLS) and counts the wall kinds of
 * all 44 view slots into them (the real map queries), with 1C952's light / exploration-bit effects after slot 2 and
 * XEEN_DS_SPOT_DOORS_LIT after slot 7. Requires map cache and restored party. Original 1B70F reaches it only when
 * F608==0 (indoor-mode maps such as Clouds 28/49; earlier phases called it the "outdoor" helper); the genuine outdoor
 * (F608!=0) branch is the xeen_scene_outdoor_* stages.
 * Stops at caller 1BBDF before combat/entity processing; never unlocks input.
 * Failures retain preceding effects and must not be retried as a fresh call. */
xeen_caller_result xeen_scene_scan_view_walls(xeen_movement *);
/* Install only original initialized DS:4D5C..4D73 data, before first scene.
 * Does not initialize monster state or authenticate an empty encounter. */
xeen_status xeen_scene_init_effect_tables(xeen_movement *);
/* Execute caller 1BBDF..1BDD5, decrementing effect lifetimes and preparing
 * paired scene records. Stops BEFORE call 1ED46; never unlocks commands.
 * Call exactly once at that pending boundary; writes retain original order. */
xeen_caller_result xeen_scene_prepare_effects(xeen_movement *);
/* Complete monster scene helper 1ED46. Database is borrowed original resident
 * backing, indexed by id*60. Scene/sprite tables must already be initialized.
 * Missing database rows reject before mutation. Does not complete the scene. */
xeen_status xeen_scene_init_monster_tables(xeen_movement *);
xeen_status xeen_scene_monsters(xeen_movement *, const uint8_t *, size_t);
xeen_status xeen_scene_objects(xeen_movement *);
xeen_status xeen_scene_walls(xeen_movement *);
/* Execute 1BDE4..1BECF once, suspending before presentation service 4E33F.
 * selected receives original SI (0..3); its owner must retain it until the
 * post-22034 restoration. FF selectors still read their original DS record.
 * This does not present a frame, restore records, or complete the scene. */
xeen_caller_result xeen_scene_shuffle(xeen_movement *, uint16_t *selected);
/* Original 1BED9..1BF59. Caller must prove 4E33F AND22034 completed before
 * invoking this low-level stage. Copies selected temporary records in word
 * order; stops before 1B0F6 at caller 1BF59. Not reachable from incomplete 22034.
 * selected must be the persistent shuffle output0..3. */
xeen_caller_result xeen_scene_restore_group(xeen_movement *, uint16_t selected);
/* Original 1B0F6 (called at 1BF59): frame counters F47C (mod5) and 3069 (mod8),
 * per-monster frame/bounce/lifetime/effect timers (database rows +30/+31),
 * attack/return record shifts selected by 97F6/97F9 and F608, then object
 * frame counters (DD2B, stride8, mod frame count 306D[sprite]). Database rows
 * and nonzero frame counts are validated before any write. Stops at 1E467. */
xeen_caller_result xeen_scene_animate(xeen_movement *, const uint8_t *database, size_t size);

/* Outdoor (F608!=0) scene branch of 1B70F, after xeen_scene_prepare_render
 * stopped at 1B7F1. Each stage rejects indoor DS (BAD_INPUT, no writes).
 * (1C952 light/exploration bits, shared with 246CD: xeen_scene_light.) */
/* 1B7DA..1B9AA: effect records (table 4D76, outdoor list 1166). Stops 2126C. */
xeen_caller_result xeen_scene_outdoor_effects(xeen_movement *);
/* 2126C: 98B4..98CD view slots, monster record words and outdoor monster
 * records. Database is the borrowed id*60 backing; a missing row for a
 * drawn monster rejects with DS unchanged. */
xeen_status xeen_scene_outdoor_monsters(xeen_movement *, const uint8_t *, size_t);
/* 219E4: 98D9 and outdoor object records. */
xeen_status xeen_scene_outdoor_objects(xeen_movement *);
/* 1B9B6..1BA95: attacking-group shuffle; selected (0..3) is original SI and
 * must be retained across 4E33F and 246CD. Stops 4E33F. */
xeen_caller_result xeen_scene_outdoor_shuffle(xeen_movement *, uint16_t *selected);
/* 1BAA2..1BB25 restoration after 246CD returned; stops 1B0F6 (1BF59). */
xeen_caller_result xeen_scene_outdoor_restore(xeen_movement *, uint16_t selected);
#endif
