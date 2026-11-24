#include "xeen/projectile.h"
#include "xeen/monster_record.h"
#include "xeen/ds_globals.h"
#include "xeen/scene.h"
#include "xeen/scene_construct.h"
#include <stdio.h>
#include <string.h>

/*
 * Where a projectile delivery stops when it cannot go on natively; each value is the original address of the step
 * (tools/xeen_replay/stops_variable.json: the three-argument stop() below is a variable site).
 */
enum {
    PROJECTILE_STOP_NOT_QUEUED = 0x4128f,       /* no shot queued, the delivery already started, or over eight members */
    PROJECTILE_STOP_SHOOTER_RECORD = 0x412c9,   /* a queued shooter's id past the monster database */
    PROJECTILE_STOP_SHOOTER_TYPE = 0x412d1,     /* a queued shooter's attack type past Energy */
    PROJECTILE_STOP_NO_SHOOTER = 0x41298,       /* the queue names no shooter */
    PROJECTILE_STOP_CHOSEN_RECORD = 0x41338,    /* the chosen shooter's record past the database */
    PROJECTILE_STOP_CHOSEN_TYPE = 0x41340,      /* the chosen shooter's attack type past Energy */
    PROJECTILE_STOP_NO_AUDIO = 0x1880e,         /* sound on and no audio service for the shot's effect */
    PROJECTILE_STOP_SOUND_REFUSED = 0x1880e,    /* the shot's sound effect was refused */
    PROJECTILE_STOP_RESOURCE_NAME = 0x412f3,    /* the "pow%u.icn" name could not be formatted */
    PROJECTILE_STOP_POW_SPRITES = 0x18931,      /* the shot's pow sprites could not be loaded */
    PROJECTILE_STOP_NOT_STARTED = 0x414b0,      /* a scene step before the setup, or with nothing queued */
    PROJECTILE_STOP_WINDOW_UP = 0x1b517,        /* window 11 is open: the scene returns early */
    PROJECTILE_STOP_SCENE_PREFIX = 0x1b51a,     /* the scene's shared prefix stopped (its status is returned) */
    PROJECTILE_STOP_OUTDOOR_MONSTERS = 0x2126c, /* the outdoor scene's monsters could not be placed */
    PROJECTILE_STOP_MONSTERS = 0x1ed46,         /* the indoor scene's monsters could not be placed */
    PROJECTILE_STOP_FINISH_EARLY = 0x39654,     /* the finish before a started, completed delivery */
    PROJECTILE_STOP_FINISH_PARTY = 0x39704      /* the finish with over eight members */
};
static const uint16_t indoor_levels[4] = {XEEN_DS_INDOOR_PROJECTILES(0u), XEEN_DS_INDOOR_PROJECTILES(1u),
                                          XEEN_DS_INDOOR_PROJECTILES(2u), XEEN_DS_INDOOR_PROJECTILES(3u)};
static const uint16_t outdoor_levels[4] = {XEEN_DS_OUTDOOR_PROJECTILES(0u), XEEN_DS_OUTDOOR_PROJECTILES(1u),
                                           XEEN_DS_OUTDOOR_PROJECTILES(2u), XEEN_DS_OUTDOOR_PROJECTILES(3u)};
/** Returns the projectile levels' records for the map (outdoor when F608 is set, else indoor), nearest first. */
static const uint16_t *levels(const xeen_movement *m)
{
    return xeen_read16(m, XEEN_DS_OUTDOORS) ? outdoor_levels : indoor_levels;
}
/** Records unsupported ADDRESS in *BOUNDARY and returns STATUS. */
static xeen_status stop(uint32_t *boundary, uint32_t address, xeen_status status)
{
    *boundary = address;
    return status;
}
/** Returns the 60-byte monster database row of ID, or NULL when it is past the database. */
static const uint8_t *monster_row(const uint8_t *db, size_t size, uint8_t id)
{
    return (size_t)id * XEEN_MONSTER_RECORD_BYTES + XEEN_MONSTER_RECORD_BYTES <= size
                   ? db + (size_t)id * XEEN_MONSTER_RECORD_BYTES
                   : NULL;
}

/* Every party position whose missile slot holds a tier (1, 2, 3 or more) gets the shot's four level records: the
 * scales 3, 7, 11, 15 (nearest level first), FFFFh frames everywhere, then the sprite HANDLE at the level of its tier
 * (tier 1 the nearest, 2 the next, any other the third). */
static void arm_monster_shots(xeen_movement *m, uint16_t handle)
{
    const uint16_t *at = levels(m);
    unsigned i, k;
    for (i = 0; i < 8u; ++i) {
        unsigned tier = m->ds[XEEN_DS_MISSILE_SLOTS + i];
        if (!tier) {
            continue;
        }
        for (k = 0; k < 4u; ++k) {
            xeen_write16(m, (uint16_t)(at[k] + XEEN_VIEW_RECORD_BYTES * i + XEEN_VIEW_RECORD_SCALE),
                         (uint16_t)(3u + 4u * k));
        }
        for (k = 0; k < 4u; ++k) {
            xeen_write16(m, (uint16_t)(at[k] + XEEN_VIEW_RECORD_BYTES * i), 0xffffu);
        }
        k = tier == 1u ? 0u : tier == 2u ? 1u : 2u;
        xeen_write16(m, (uint16_t)(at[k] + XEEN_VIEW_RECORD_BYTES * i), handle);
    }
}

xeen_status xeen_projectile_setup(xeen_projectile_delivery *p, xeen_movement *m, const uint8_t *db,
                                  size_t size, xeen_sprite_cache *cache,
                                  const xeen_sprite_archives *archives, xeen_native_audio *audio,
                                  uint32_t *boundary)
{
    const uint8_t *monster = NULL;
    unsigned i, chosen = XEEN_MONSTER_SHOOTERS;
    uint8_t pow_id = 0;
    uint16_t handle, effect;
    char resource[16];
    int found = 0;
    if (!p || !m || !db || !cache || !archives || !boundary) {
        return XEEN_BAD_INPUT;
    }
    *boundary = 0;
    if (p->started || !m->ds[XEEN_DS_MONSTER_SHOTS_QUEUED] || !m->ds[XEEN_DS_PARTY_COUNT] ||
        m->ds[XEEN_DS_PARTY_COUNT] > 8u) {
        return stop(boundary, PROJECTILE_STOP_NOT_QUEUED, XEEN_BAD_INPUT);
    }
    for (i = 0; i < XEEN_MONSTER_SHOOTERS; ++i) {
        uint8_t id = m->ds[XEEN_DS_MONSTER_SHOOTERS + i];
        if (id == 255u) {
            continue;
        }
        monster = monster_row(db, size, id);
        if (!monster) {
            return stop(boundary, PROJECTILE_STOP_SHOOTER_RECORD, XEEN_SERVICE_REQUIRED);
        }
        if (monster[XEEN_MONSTER_ATTACK_TYPE] > XEEN_ATTACK_ENERGY) {
            return stop(boundary, PROJECTILE_STOP_SHOOTER_TYPE, XEEN_SERVICE_REQUIRED);
        }
        pow_id = m->ds[XEEN_DS_MONSTER_POW_ICON + monster[XEEN_MONSTER_ATTACK_TYPE]];
        found = 1;
        if (pow_id != 12u) {
            chosen = i;
            break;
        }
    }
    if (!found) {
        return stop(boundary, PROJECTILE_STOP_NO_SHOOTER, XEEN_SERVICE_REQUIRED);
    }
    /* 4130E reads 97D0[36]==97F4 if no non12 projectile broke the scan.
     * This is an observable original lookup, not the final queued row. */
    monster = monster_row(db, size, m->ds[XEEN_DS_MONSTER_SHOOTERS + chosen]);
    if (!monster) {
        return stop(boundary, PROJECTILE_STOP_CHOSEN_RECORD, XEEN_SERVICE_REQUIRED);
    }
    if (monster[XEEN_MONSTER_ATTACK_TYPE] > XEEN_ATTACK_ENERGY) {
        return stop(boundary, PROJECTILE_STOP_CHOSEN_TYPE, XEEN_SERVICE_REQUIRED);
    }
    effect = m->ds[XEEN_DS_MONSTER_POW_CUE + monster[XEEN_MONSTER_ATTACK_TYPE]];
    if (m->ds[XEEN_DS_SOUND_ON] && !audio) {
        return stop(boundary, PROJECTILE_STOP_NO_AUDIO, XEEN_SERVICE_REQUIRED);
    }
    if (snprintf(resource, sizeof resource, "pow%u.icn", (unsigned)pow_id) < 0) {
        return stop(boundary, PROJECTILE_STOP_RESOURCE_NAME, XEEN_BAD_INPUT);
    }
    if (xeen_sprite_load(cache, archives, m->ds[XEEN_DS_WORLD], resource,
                         xeen_read16(m, XEEN_DS_RESIDENT_SPRITE_SLOT), &handle) != XR_OK) {
        return stop(boundary, PROJECTILE_STOP_POW_SPRITES, XEEN_SERVICE_REQUIRED);
    }
    memcpy(m->ds + XEEN_DS_FILE_NAME, resource, strlen(resource) + 1u);
    xeen_write16(m, XEEN_DS_PROJECTILE_SPRITE, handle);
    if (xeen_native_audio_effect(audio, m, effect) != XEEN_OK) {
        return stop(boundary, PROJECTILE_STOP_SOUND_REFUSED, XEEN_SERVICE_REQUIRED);
    }
    arm_monster_shots(m, handle);
    *p = (xeen_projectile_delivery){0};
    p->started = 1;
    p->pow_id = pow_id;
    p->handle = handle;
    p->effect = effect;
    memcpy(p->resource, resource, strlen(resource) + 1u);
    return XEEN_OK;
}

xeen_status xeen_projectile_advance(xeen_projectile_delivery *p, xeen_movement *m,
                                    const uint8_t *db, size_t size, uint32_t *boundary)
{
    xeen_status status;
    xeen_caller_result result;
    if (!p || !m || !db || !boundary) {
        return XEEN_BAD_INPUT;
    }
    *boundary = 0;
    if (!p->started || !m->ds[XEEN_DS_MONSTER_SHOTS_QUEUED]) {
        return stop(boundary, PROJECTILE_STOP_NOT_STARTED, XEEN_BAD_INPUT);
    }
    if (p->complete) {
        return XEEN_OK;
    }
    if (m->ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_VIEW)]) {
        return stop(boundary, PROJECTILE_STOP_WINDOW_UP, XEEN_SERVICE_REQUIRED);
    }
    /* 97F6 gates out 96C0 movement recursion in the shared scene prefix. */
    if ((status = xeen_scene_prefix(m)) != XEEN_OK) {
        return stop(boundary, PROJECTILE_STOP_SCENE_PREFIX, status);
    }
    status = xeen_read16(m, XEEN_DS_OUTDOORS) ? xeen_scene_outdoor_monsters(m, db, size)
                                    : xeen_scene_monsters(m, db, size);
    if (status != XEEN_OK) {
        return stop(boundary, xeen_read16(m, XEEN_DS_OUTDOORS) ? PROJECTILE_STOP_OUTDOOR_MONSTERS : PROJECTILE_STOP_MONSTERS, status);
    }
    /* Original renderer-finish scan precedes the 1B0F6 handle shift. The
     * semantic adapter retains that extra final-empty-frame completion gate. */
    if (xeen_read16(m, XEEN_DS_OUTDOORS)) {
        (void)xeen_scene_construct_outdoor_finish(m);
    } else {
        (void)xeen_scene_construct_finish(m);
    }
    result = xeen_scene_animate(m, db, size);
    if (result.boundary != XEEN_SCENE_DRAW || result.status != XEEN_SERVICE_REQUIRED) {
        return stop(boundary, result.boundary, result.status);
    }
    m->status = XEEN_OK;
    ++p->scenes;
    p->complete = (uint8_t)(m->ds[0x97f7] == 0u);
    return XEEN_OK;
}

void xeen_projectile_clear_member(xeen_movement *m, unsigned member)
{
    const uint16_t *at = levels(m);
    unsigned k;
    /* Original 39654 clears far-to-near, all scales before handles. */
    for (k = 4u; k > 0u; --k) {
        xeen_write16(m, (uint16_t)(at[k - 1u] + XEEN_VIEW_RECORD_BYTES * member + XEEN_VIEW_RECORD_SCALE), 0);
    }
    for (k = 4u; k > 0u; --k) {
        xeen_write16(m, (uint16_t)(at[k - 1u] + XEEN_VIEW_RECORD_BYTES * member), 0xffffu);
    }
}
void xeen_projectile_end_attack(xeen_movement *m)
{
    unsigned i;
    m->ds[0x97f7] = 0; /* 97F7: a missile slot in use (the scans' copy; kept numeric in ds_globals.h) */
    for (i = 0; i < m->ds[XEEN_DS_PARTY_COUNT]; ++i) {
        xeen_projectile_clear_member(m, i);
    }
    memset(m->ds + XEEN_DS_MISSILE_SLOTS, 0, 8);
}
xeen_status xeen_projectile_finish(xeen_projectile_delivery *p, xeen_movement *m,
                                   uint32_t *boundary)
{
    if (!p || !m || !boundary) {
        return XEEN_BAD_INPUT;
    }
    *boundary = 0;
    if (!p->started || !p->complete) {
        return stop(boundary, PROJECTILE_STOP_FINISH_EARLY, XEEN_BAD_INPUT);
    }
    if (p->finished) {
        return XEEN_OK;
    }
    if (m->ds[XEEN_DS_PARTY_COUNT] > 8u) {
        return stop(boundary, PROJECTILE_STOP_FINISH_PARTY, XEEN_BAD_INPUT);
    }
    xeen_projectile_end_attack(m);
    p->finished = 1;
    return XEEN_OK;
}
