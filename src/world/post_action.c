#include "xeen/post_action.h"
#include "xeen/caller.h"
#include "xeen/ds_globals.h"
#include "xeen/character_record.h"
#include "xeen/monster_record.h"
#include "xeen/gameplay_tick.h"
#include "xeen/monster_update.h"
#include "xeen/byte_order.h"
#include "xeen/input_poll.h"
/*
 * Where the step's continuation stops here: its refused sound and the surface's owners (the fall, the edge, the
 * damage scene, the defeat, the desert's services). Each value is the original address the continuation reaches
 * (tools/xeen_replay/stops.json); after the step's toggles the loop's tail 17F07 (caller.c) owns the rest.
 */
enum {
    POST_ACTION_STOP_STEP_SOUND = 0x1881b,    /* the step's sound was refused */
    SURFACE_STOP_FALL = 0x173a0,              /* the surface starts a fall */
    SURFACE_STOP_EDGE = 0x17334,              /* the step left the map at its edge */
    SURFACE_STOP_DAMAGE_SCENE = 0x17344,      /* the surface's damage scene */
    SURFACE_STOP_DEFEATED = 0x17380,          /* the surface defeated the party */
    SURFACE_STOP_NO_SKILL_SERVICE = 0x172c3,  /* the desert's Navigator test needs the ability service */
    SURFACE_STOP_DESERT_CLOCK = 0x172cd       /* without a Navigator the desert costs time: the clock services */
};
/* The damage a harmful surface does (lava as fire; the sky, and clouds without Levitate, as a physical fall). */
#define SURFACE_DAMAGE 100u
/** Ends the post-action continuation at unsupported BOUNDARY (in m->status too). */
static xeen_caller_result stop(xeen_movement *m, uint32_t boundary)
{
    xeen_caller_result r = {XEEN_SERVICE_REQUIRED, boundary};
    if (m) {
        m->status = r.status;
    }
    return r;
}
/* The writes that end a step: the sign's text is no longer shown, bit 15 of the step flip word and the step parity
 * flip. Original: the same three instructions twice, 17D88..17D93 (after the step's sound) and 17DB2..17DBD (Space's
 * default path). */
static void step_toggles(xeen_movement *m)
{
    m->ds[XEEN_DS_SIGN_SHOWN] = 0;
    xeen_write16(m, XEEN_DS_STEP_FLIP, (uint16_t)(xeen_read16(m, XEEN_DS_STEP_FLIP) ^ 0x8000u));
    m->ds[XEEN_DS_STEP_PARITY] ^= 1;
}
/* The post-action tail after the step's sound: the step's toggles (step_toggles), then 17D98's `jmp 17F07`, the
 * loop's tail (xeen_caller_after_command: the fall, the exit, the join 17ACC). M is not NULL. */
static xeen_caller_result after_audio(xeen_movement *m)
{
    step_toggles(m);
    return xeen_caller_after_command(m);
}
xeen_caller_result xeen_post_action_resume_audio(xeen_movement *m, xeen_native_audio *audio)
{
    if (!m) {
        xeen_caller_result r = {XEEN_BAD_INPUT, XEEN_AUDIO_EFFECT_CALL};
        return r;
    }
    if (xeen_native_audio_effect(audio, m, (uint16_t)(7u + m->ds[XEEN_DS_STEP_SOUND])) != XEEN_OK) {
        return stop(m, POST_ACTION_STOP_STEP_SOUND);
    }
    return after_audio(m);
}
/* The step's surface tail: a fall (unless Darkside's game flag 118 is set), the relocation, the edge, the cell's
 * damage scene and the defeat test. Returns OK with boundary 17389. Original: 172EF..17388; 2F847(F01F, 176h). */
static xeen_caller_result surface_tail(xeen_movement *m, unsigned damage)
{
    xeen_caller_result done = {XEEN_OK, XEEN_SURFACE_DONE};
    /* On Darkside, its game flag 118 keeps the party from falling (game flag bit 256 + 118 = 176h). */
    if (m->ds[XEEN_DS_WORLD] && xeen_ds_get_bit(m->ds, XEEN_DS_GAME_FLAGS, 256u + 118u)) {
        m->ds[XEEN_DS_FALL_SCENE] = 0;
    } else {
        if (m->ds[XEEN_DS_FALL_SCENE]) {
            m->fall_caller = 1; /* the session's SURFACE_MOVEMENT */
            m->fall_damage = (uint16_t)damage;
            return stop(m, SURFACE_STOP_FALL);
        }
        /* SI was computed before the edge step (17277 1C1C1(2):
         * the destination cell) and 17339 tests it after 19D94's load. */
        if (((m->ds[XEEN_DS_PARTY_X] | m->ds[XEEN_DS_PARTY_Y]) & 0x10u) && xeen_read16(m, XEEN_DS_OUTDOORS)) {
            m->edge_damage = (uint16_t)damage;
            return stop(m, SURFACE_STOP_EDGE);
        }
        if (damage) {
            m->ds[XEEN_DS_STEP_PARITY] ^= 1;
            return stop(m, SURFACE_STOP_DAMAGE_SCENE);
        }
        if (m->ds[XEEN_DS_PARTY_DEFEATED]) {
            return stop(m, SURFACE_STOP_DEFEATED);
        }
    }
    m->status = XEEN_OK;
    return done;
}
/* 17264..17388. Returns OK with boundary 17389 when 17264 returns. The
 * desert outdoors (172BA): 2BEF9(10), the party's Navigator
 * skill (the movement's ability service); without it 30CF3(170) needs the
 * session's clock services: boundary 172CD, then
 * xeen_post_action_surface_after_desert_native. */
xeen_caller_result xeen_post_action_surface_native(xeen_movement *m)
{
    uint16_t ax, kind;
    unsigned damage = 0;
    if (!m) {
        xeen_caller_result r = {XEEN_BAD_INPUT, XEEN_TICK_POST_ACTION};
        return r;
    }
    m->edge_damage = 0;
    m->ds[XEEN_DS_PARTY_ACTED] = 1;
    m->ds[XEEN_DS_SIGN_SHOWN] = 0;
    if (xeen_navigation_query(m, XEEN_STEP_HERE, &ax) != XEEN_OK) {
        xeen_caller_result r = {m->status, XEEN_NAVIGATION_QUERY};
        return r;
    }
    kind = m->ds[(uint16_t)(XEEN_DS_MAP_FIELD(CLASSIFICATION) + xeen_read16(m, XEEN_DS_CELL_SURFACE))];
    switch (kind) { /* the surface class's rule (xeen/map_record.h) */
        case XEEN_SURFACE_LAVA:
            damage = SURFACE_DAMAGE;
            xeen_write16(m, XEEN_DS_DAMAGE_TYPE, XEEN_ATTACK_FIRE);
            break;
        case XEEN_SURFACE_DESERT:
            if (xeen_read16(m, XEEN_DS_OUTDOORS)) {
                uint16_t skill = 0;
                if (!m->service || !m->service(m->user, m, XEEN_ABILITY, XEEN_SKILL_NAVIGATOR, 0, &skill)) {
                    return stop(m, SURFACE_STOP_NO_SKILL_SERVICE);
                }
                if (!skill) {
                    return stop(m, SURFACE_STOP_DESERT_CLOCK);
                }
            }
            break;
        case XEEN_SURFACE_SKY:
            damage = SURFACE_DAMAGE;
            xeen_write16(m, XEEN_DS_DAMAGE_TYPE, XEEN_ATTACK_PHYSICAL);
            m->ds[XEEN_DS_FALL_SCENE] = 1;
            break;
        case XEEN_SURFACE_CLOUD:
            if (!m->ds[XEEN_DS_EFFECT_LEVITATE]) {
                damage = SURFACE_DAMAGE;
                xeen_write16(m, XEEN_DS_DAMAGE_TYPE, XEEN_ATTACK_PHYSICAL);
                m->ds[XEEN_DS_FALL_SCENE] = 1;
            }
            break;
        case XEEN_SURFACE_SPACE:
            m->ds[XEEN_DS_PARTY_DEFEATED] = 1;
            break;
        default:
            break;
    }
    return surface_tail(m, damage);
}
xeen_caller_result xeen_post_action_surface_after_desert_native(xeen_movement *m)
{
    if (!m) {
        xeen_caller_result r = {XEEN_BAD_INPUT, 0x172d5};
        return r;
    }
    return surface_tail(m, 0);
}
xeen_caller_result xeen_post_action_after_movement_native(xeen_movement *m,
                                                          xeen_native_audio *audio)
{
    xeen_caller_result surface = xeen_post_action_surface_native(m);
    if (surface.status != XEEN_OK) {
        return surface;
    }
    return xeen_post_action_after_surface_native(m, audio);
}
xeen_caller_result xeen_post_action_after_surface_native(xeen_movement *m, xeen_native_audio *audio)
{
    uint16_t command, count;
    if (!m) {
        xeen_caller_result r = {XEEN_BAD_INPUT, 0x17d50};
        return r;
    }
    /* EFEE: a word every step advances, 0..23 (17D54..17D60: past 23, signed, it starts again at 0); no reader is
     * known, so it stays numeric. */
    count = (uint16_t)(xeen_read16(m, 0xefee) + 1u);
    if (count < 32768u && count > 23u) {
        count = 0;
    }
    xeen_write16(m, 0xefee, count);
    command = xeen_read16(m, XEEN_DS_INPUT_CODE);
    /* Every command but Space and W (57h) plays the alternating step sound (17D66..17D87: 9775 ^= 1, 1880E(7 + it)). */
    if (command != XEEN_KEY_SPACE && command != 'W') {
        m->ds[XEEN_DS_STEP_SOUND] ^= 1;
        return xeen_post_action_resume_audio(m, audio);
    }
    return after_audio(m);
}

xeen_caller_result xeen_post_action_after_transition_native(xeen_movement *m,
                                                            xeen_native_audio *audio)
{
    if (!m) {
        xeen_caller_result r = {XEEN_BAD_INPUT, 0x17d50};
        return r;
    }
    /* 17339 with SI set: 1733D's damage scene (no 17377 F60A test). */
    if (m->edge_damage) {
        m->edge_damage = 0;
        m->ds[XEEN_DS_STEP_PARITY] ^= 1;
        return stop(m, SURFACE_STOP_DAMAGE_SCENE);
    }
    if (m->ds[XEEN_DS_PARTY_DEFEATED]) {
        return stop(m, SURFACE_STOP_DEFEATED);
    }
    return xeen_post_action_after_surface_native(m, audio);
}

/* 17DA8: the clock, the explicit second condition pass, the step's toggles (17DB2, step_toggles), then the shared
 * 17D50 post-action. Like movement, this is a one-shot continuation. On an unsupported branch
 * earlier changes persist; callers must not restart it to retry the command. */
xeen_caller_result xeen_post_action_default_space_native(xeen_movement *m, xeen_native_audio *audio)
{
    xeen_gameplay_tick tick;
    uint32_t boundary = 0;
    if (!m) {
        xeen_caller_result r = {XEEN_BAD_INPUT, 0x17da8};
        return r;
    }
    tick = xeen_gameplay_tick_wait_native(m);
    if (tick.boundary != XEEN_TICK_POST_ACTION) {
        return stop(m, tick.boundary);
    }
    if (xeen_input_release_96c0_extended(m, &boundary) != XEEN_OK) {
        return stop(m, boundary);
    }
    step_toggles(m);
    return xeen_post_action_after_movement_native(m, audio);
}
