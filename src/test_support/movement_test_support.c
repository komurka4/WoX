/*
 * The movement module's single-step entry points used only by the tests (the game's movement runs through the tick).
 *
 * Test support (Phase 284): functions only the tests call, moved out of src/world/movement.c
 * so that the game's executable does not carry them. They are built into the xeen_test_support library,
 * which the test programs link and the shipped programs do not; their declarations stay where they were.
 */
#include "xeen/party.h"
#include "xeen/character_record.h"
#include "xeen/ds_globals.h"
#include "xeen/monster_record.h"
#include "xeen/movement.h"
#include <string.h>
#include "../world/movement_internal.h"

/** Returns the party's direction, position and map (DS ED96..ED99). */
xeen_navigation xeen_navigation_get(const xeen_movement *m)
{
    xeen_navigation n = {B(XEEN_DS_PARTY_DIRECTION), B(XEEN_DS_PARTY_X), B(XEEN_DS_PARTY_Y),
                         B(XEEN_DS_PARTY_MAP)};
    return n;
}
enum {
    MINUTES_PER_DAY = 1440, /* 30CF3's wrap */
    DAYS_PER_YEAR = 100     /* days 0..99 (the original's signed day test) */
};
/* The test model of 30CF3 (the game's is gameplay_tick.c's clock and day_wrap): AMOUNT minutes added, the day
 * wrapping past 1440 minutes and the year past day 99, then the clock tail service with AMOUNT and the day before. */
static xeen_status clock_add(xeen_movement *m, uint16_t amount)
{
    uint16_t old = W(XEEN_DS_DAY);
    m->status = XEEN_OK;
    S(XEEN_DS_MINUTE_OF_DAY, W(XEEN_DS_MINUTE_OF_DAY) + amount);
    while (W(XEEN_DS_MINUTE_OF_DAY) >= MINUTES_PER_DAY) {
        S(XEEN_DS_MINUTE_OF_DAY, W(XEEN_DS_MINUTE_OF_DAY) - MINUTES_PER_DAY);
        S(XEEN_DS_DAY, W(XEEN_DS_DAY) + 1u);
        if (xeen_movement_signed_word(W(XEEN_DS_DAY)) >= DAYS_PER_YEAR) {
            S(XEEN_DS_DAY, W(XEEN_DS_DAY) - DAYS_PER_YEAR);
            S(XEEN_DS_YEAR, W(XEEN_DS_YEAR) + 1u);
        }
    }
    xeen_movement_request_service(m, XEEN_CLOCK_TAIL, amount, old);
    return m->status;
}
/* The test model of a step's time charge: 10 minutes outdoors, 1 indoors (none for a defeated party), the
 * time-conditions service, the clock (clock_add), then the monsters' countdown: a running one asks the 96C0 service,
 * and it restarts at 3. Returns the status after the first refused service. */
xeen_status xeen_charge_translation_time(xeen_movement *m)
{
    uint16_t amount;
    m->status = XEEN_OK;
    if (B(XEEN_DS_PARTY_DEFEATED)) {
        return m->status;
    }
    amount = W(XEEN_DS_OUTDOORS) ? 10 : 1;
    xeen_movement_request_service(m, XEEN_TIME_CONDITIONS, amount, 0);
    if (m->status != XEEN_OK) {
        return m->status;
    }
    clock_add(m, amount);
    if (m->status != XEEN_OK) {
        return m->status;
    }
    if (B(XEEN_DS_MONSTER_COUNTDOWN)) {
        xeen_movement_request_service(m, XEEN_96C0, 0, 0);
    }
    if (m->status == XEEN_OK) {
        B(XEEN_DS_MONSTER_COUNTDOWN) = 3;
    }
    return m->status;
}
enum {
    SURFACE_DAMAGE = 100,       /* lava, sky and an unlevitated cloud */
    DESERT_MINUTES = 170,       /* a desert step outdoors without a Navigator (30CF3(170)) */
    DARKSIDE_NO_FALL = 0x176,   /* game flag bit 256 + 118: Darkside's flag 118 keeps the party from falling */
    OUTDOOR_EDGE_BIT = 16       /* x or y 16..31: the cell is past the map's edge, outdoors */
};
/*
 * The test model of the step's surface rules (17264..17388; the game's is post_action.c's
 * xeen_post_action_surface_native and surface_tail): the party acted, the sign gone; by the destination's surface
 * class: space defeats, lava burns (100, fire), sky and an unlevitated cloud drop (100, physical, the fall scene), a
 * desert outdoors costs 170 minutes without a Navigator. Then Darkside's flag 118 cancels the fall, a fall relocates,
 * a cell past the outdoor edge commits the boundary, and a damage scene (the step parity flipped around it, the
 * member selector cleared for the tile effect) or a defeat's scene follows. Returns the status after the first refused service.
 */
xeen_status xeen_post_translation(xeen_movement *m)
{
    uint16_t cell = 0, skill, damage = 0;
    uint8_t surface, save;
    B(XEEN_DS_PARTY_ACTED) = 1;
    B(XEEN_DS_SIGN_SHOWN) = 0;
    xeen_navigation_query(m, XEEN_STEP_HERE, &cell);
    if (m->status != XEEN_OK) {
        return m->status;
    }
    surface = B(XEEN_DS_MAP_FIELD(CLASSIFICATION) + W(XEEN_DS_CELL_SURFACE)); /* the class (xeen/map_record.h) */
    if (surface == XEEN_SURFACE_SPACE) {
        B(XEEN_DS_PARTY_DEFEATED) = 1;
    } else if (surface == XEEN_SURFACE_LAVA) {
        damage = SURFACE_DAMAGE;
        S(XEEN_DS_DAMAGE_TYPE, XEEN_ATTACK_FIRE);
    } else if (surface == XEEN_SURFACE_SKY || (surface == XEEN_SURFACE_CLOUD && !B(XEEN_DS_EFFECT_LEVITATE))) {
        damage = SURFACE_DAMAGE;
        S(XEEN_DS_DAMAGE_TYPE, XEEN_ATTACK_PHYSICAL);
        B(XEEN_DS_FALL_SCENE) = 1;
    } else if (surface == XEEN_SURFACE_DESERT && W(XEEN_DS_OUTDOORS)) {
        skill = xeen_movement_request_service(m, XEEN_ABILITY, XEEN_SKILL_NAVIGATOR, 0);
        if (m->status != XEEN_OK) {
            return m->status;
        }
        if (!skill) {
            clock_add(m, DESERT_MINUTES);
        }
    }
    if (m->status != XEEN_OK) {
        return m->status;
    }
    if (B(XEEN_DS_WORLD) && xeen_movement_request_service(m, XEEN_FLAG_QUERY, XEEN_DS_GAME_FLAGS, DARKSIDE_NO_FALL)) {
        B(XEEN_DS_FALL_SCENE) = 0;
        return m->status;
    }
    if (m->status != XEEN_OK) {
        return m->status;
    }
    if (B(XEEN_DS_FALL_SCENE)) {
        xeen_movement_request_service(m, XEEN_RELOCATE, 0, 0);
    }
    if (m->status != XEEN_OK) {
        return m->status;
    }
    if (((B(XEEN_DS_PARTY_X) | B(XEEN_DS_PARTY_Y)) & OUTDOOR_EDGE_BIT) && W(XEEN_DS_OUTDOORS)) {
        xeen_boundary_commit(m);
    }
    if (m->status != XEEN_OK) {
        return m->status;
    }
    if (damage) {
        B(XEEN_DS_STEP_PARITY) ^= 1;
        xeen_movement_request_service(m, XEEN_SCENE, 1, 0);
        save = B(XEEN_DS_MEMBER_SELECTOR);
        B(XEEN_DS_MEMBER_SELECTOR) = 0;
        xeen_movement_request_service(m, XEEN_TILE_EFFECT, damage, W(XEEN_DS_DAMAGE_TYPE));
        B(XEEN_DS_MEMBER_SELECTOR) = save;
        B(XEEN_DS_STEP_PARITY) ^= 1;
    } else if (B(XEEN_DS_PARTY_DEFEATED)) {
        xeen_movement_request_service(m, XEEN_SCENE, 1, 0);
    }
    return m->status;
}
/*
 * The test model of a forward step (the game's runs through the tick and post_action.c): the translation gate, then
 * for an allowed step the move, its time, the surface rules (xeen_post_translation), the step counter, the step's
 * sound and its toggles; then a fall relocates, and unless defeated or leaving, the cell's metadata, its step event,
 * the world update and the scene. *ALLOWED is the gate's answer. Returns the status after the first refused service.
 */
xeen_status xeen_process_forward(xeen_movement *m, uint16_t *allowed)
{
    S(XEEN_DS_INPUT_CODE, XEEN_MOVE_FORWARD);
    xeen_translation_gate(m, XEEN_MOVE_FORWARD, allowed);
    if (m->status != XEEN_OK) {
        return m->status;
    }
    if (*allowed) {
        xeen_forward_delta(m);
        xeen_charge_translation_time(m);
        if (m->status != XEEN_OK) {
            return m->status;
        }
        xeen_post_translation(m);
        if (m->status != XEEN_OK) {
            return m->status;
        }
        /* EFEE: the step counter 0..23, only written (deliberately numeric; post_action.c 17D54's count) */
        S(0xefee, W(0xefee) + 1u);
        if (xeen_movement_signed_word(W(0xefee)) > 23) {
            S(0xefee, 0);
        }
        B(XEEN_DS_STEP_SOUND) ^= 1;
        xeen_movement_request_service(m, XEEN_SOUND, (uint16_t)(7u + B(XEEN_DS_STEP_SOUND)), 0); /* sounds 7 and 8 */
        if (m->status != XEEN_OK) {
            return m->status;
        }
        B(XEEN_DS_SIGN_SHOWN) = 0; /* the step's toggles (post_action.c step_toggles, 17D88) */
        S(XEEN_DS_STEP_FLIP, W(XEEN_DS_STEP_FLIP) ^ 0x8000u);
        B(XEEN_DS_STEP_PARITY) ^= 1;
    }
    if (B(XEEN_DS_FALL_SCENE)) {
        xeen_movement_request_service(m, XEEN_RELOCATE, 0, 0);
    }
    if (m->status != XEEN_OK || B(XEEN_DS_PARTY_DEFEATED) || W(XEEN_DS_LOOP_EXIT)) {
        return m->status;
    }
    xeen_metadata(m);
    if (m->status != XEEN_OK) {
        return m->status;
    }
    if (W(XEEN_DS_CELL_STEP_EVENT)) {
        S(XEEN_DS_LOOP_EXIT, xeen_movement_request_service(m, XEEN_EVENT, 0, 0));
        if (m->status != XEEN_OK || W(XEEN_DS_LOOP_EXIT)) {
            return m->status;
        }
    }
    xeen_movement_request_service(m, XEEN_WORLD_UPDATE, 0, 0);
    xeen_movement_request_service(m, XEEN_SCENE, 1, 0);
    return m->status;
}
