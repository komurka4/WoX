#include "xeen/map_objects.h"
#include "xeen/monster_record.h"
#include "xeen/ds_globals.h"
#include "xeen/byte_order.h"
#include "xeen/entities.h"
xeen_resource_status xeen_entities_objects(const uint8_t *mob, size_t size, xeen_movement *m,
                                           uint16_t *next)
{
    size_t count = 1, i;
    if (!mob || !m) {
        return XR_BAD_INPUT;
    }
    /* DOS checks the following record after copying the current one. The
     * four-byte FF sentinel must therefore follow at least one record. */
    for (;; count++) {
        size_t p = XEEN_MOB_HEADER_BYTES + count * XEEN_MOB_ENTRY_BYTES;
        if (p + XEEN_MOB_ENTRY_BYTES > size) {
            return XR_TRUNCATED;
        }
        if (mob[p] == 255 && mob[p + 1] == 255 && mob[p + 2] == 255 && mob[p + 3] == 255) {
            break;
        }
        /* E530 is the subsequent original monster storage. A malformed input
         * must not be permitted to corrupt it on the host. */
        if (count >= XEEN_MAP_OBJECTS_MAX) {
            return XR_CAPACITY;
        }
    }
    for (i = 0; i < count; i++) {
        if ((size_t)mob[XEEN_MOB_HEADER_BYTES + i * XEEN_MOB_ENTRY_BYTES + 2u] >= size) {
            return XR_TRUNCATED;
        }
    }
    for (i = 0; i < count; i++) {
        size_t p = XEEN_MOB_HEADER_BYTES + i * XEEN_MOB_ENTRY_BYTES;
        uint16_t d = (uint16_t)(XEEN_DS_MAP_OBJECTS + i * XEEN_MAP_OBJECT_BYTES);
        m->ds[d + XEEN_MAP_OBJECT_X] = mob[p];
        m->ds[d + XEEN_MAP_OBJECT_Y] = mob[p + 1u];
        m->ds[d + XEEN_MAP_OBJECT_SLOT] = mob[p + 2u];
        m->ds[d + XEEN_MAP_OBJECT_SPRITE] = mob[XEEN_MOB_OBJECT_SPRITES + mob[p + 2u]];
        m->ds[d + XEEN_MAP_OBJECT_DIRECTION] = mob[p + 3u];
        m->ds[d + XEEN_MAP_OBJECT_FRAME] = 100;
    }
    m->ds[XEEN_DS_MAP_OBJECT_COUNT] = (uint8_t)count;
    if (next) {
        *next = (uint16_t)count;
    }
    return XR_OK;
}
/* A resource of exactly WANT bytes copied into DS at AT. */
static xeen_resource_status ds_load(const uint8_t *bytes, size_t size, xeen_movement *m, size_t want, size_t at)
{
    size_t i;
    if (!bytes || !m) {
        return XR_BAD_INPUT;
    }
    if (size != want) {
        return XR_BAD_INDEX;
    }
    for (i = 0; i < size; i++) {
        m->ds[at + i] = bytes[i];
    }
    return XR_OK;
}
xeen_resource_status xeen_entities_animation(const uint8_t *bytes, size_t size, xeen_movement *m)
{
    return ds_load(bytes, size, m, 121u * XEEN_OBJECT_ANIMATION_BYTES, XEEN_DS_OBJECT_ANIMATION);
}
xeen_resource_status xeen_entities_wall_frames(const uint8_t *bytes, size_t size, xeen_movement *m)
{
    return ds_load(bytes, size, m, XEEN_WALL_ITEM_SPRITES, XEEN_DS_WALL_ITEM_FRAMES);
}
/* The object poses (XEEN_DS_OBJECT_POSES): the map load installs the 16 bytes of the program data's object_poses. Kept
 * as a literal (equal to the image's bytes): historical evidence tools compile this unit without the program data. */
xeen_resource_status xeen_entities_orientation(xeen_movement *m)
{
    static const uint8_t original[XEEN_OBJECT_POSES] = {0, 1, 2, 3, 3, 0, 1, 2, 2, 3, 0, 1, 1, 2, 3, 0};
    size_t i;
    if (!m) {
        return XR_BAD_INPUT;
    }
    for (i = 0; i < sizeof original; i++) {
        m->ds[XEEN_DS_OBJECT_POSES + i] = original[i];
    }
    return XR_OK;
}

/** Returns nonzero when the four bytes at P of the monster/object list are the FFh end marker. */
static int entity_sentinel(const uint8_t *mob, size_t p)
{
    return mob[p] == 255 && mob[p + 1u] == 255 && mob[p + 2u] == 255 && mob[p + 3u] == 255;
}
/*
 * The end of the .mob list whose first entry is at FROM into *END: the next entry holding the FFh end marker after
 * FROM (FROM itself is never tested: the original copies at least one row per list, even a placeholder). Returns 0
 * when the file ends first.
 */
static int list_end(const uint8_t *mob, size_t size, size_t from, size_t *end)
{
    size_t at = from;
    do {
        at += XEEN_MOB_ENTRY_BYTES;
        if (at + XEEN_MOB_ENTRY_BYTES > size) {
            return 0;
        }
    } while (!entity_sentinel(mob, at));
    *end = at;
    return 1;
}
/*
 * Checks the monster entries START..END before anything is written: each entry's sprite slot must be inside the
 * file (else XR_TRUNCATED), and the row of the monster that slot names must be readable: inside the table of
 * DB_SIZE bytes and, past the loaded FILE_SIZE bytes, only from ALIAS_START (the event buffer the original's table
 * overlaps; else XR_NOT_FOUND). The first failing entry decides.
 */
static xeen_resource_status monsters_resolvable(const uint8_t *mob, size_t size, size_t start, size_t end,
                                                size_t file_size, size_t alias_start, size_t db_size)
{
    size_t p;
    for (p = start; p < end; p += XEEN_MOB_ENTRY_BYTES) {
        size_t slot = XEEN_MOB_MONSTER_SPRITES + mob[p + 2u], row;
        if (slot >= size) {
            return XR_TRUNCATED;
        }
        row = (size_t)mob[slot] * 60u;
        if (row + 60u > db_size) {
            return XR_NOT_FOUND;
        }
        if (row + 60u > file_size && row < alias_start) {
            return XR_NOT_FOUND;
        }
    }
    return XR_OK;
}
/*
 * Installs monster entry ENTRY as the map's monster I: its position and sprite slot, the monster id the slot names,
 * its hit points from the monster's row, the idle frame drawn 0..7 (the draw's incoming AX the hit points) and the
 * colour effect: the type's effect as cycle and effect, and, for a type with one, a step drawn 0..7 (incoming AX the
 * row's offset & 15). The frame and the step store the ranged results, not the raw words. Original: from 1A38C
 * (the draws 1A3F0, 1A47D: 504DA(0, 7)).
 */
static void install_monster(xeen_movement *m, unsigned i, const uint8_t *entry, const uint8_t *mob, const uint8_t *db,
                            xeen_entity_random_fn random, void *user)
{
    uint16_t d = (uint16_t)XEEN_MAP_MONSTER(i), ax, v = 0;
    size_t row;
    m->ds[d + XEEN_MAP_MONSTER_X] = entry[0];
    m->ds[d + XEEN_MAP_MONSTER_Y] = entry[1];
    m->ds[d + XEEN_MAP_MONSTER_SPRITE_SLOT] = entry[2];
    m->ds[d + XEEN_MAP_MONSTER_ID] = mob[XEEN_MOB_MONSTER_SPRITES + entry[2]];
    row = (size_t)m->ds[d + XEEN_MAP_MONSTER_ID] * 60u;
    ax = (uint16_t)((uint16_t)db[row + XEEN_MONSTER_HP] | (uint16_t)((uint16_t)db[row + XEEN_MONSTER_HP + 1u] << 8));
    xeen_write16(m, (uint16_t)(d + XEEN_MAP_MONSTER_HP), ax);
    (void)xeen_entity_rng_range(random(user, ax), 0, 7, &v);
    m->ds[d + XEEN_MAP_MONSTER_FRAME] = (uint8_t)v;
    ax = db[row + XEEN_MONSTER_EFFECT];
    xeen_write16(m, (uint16_t)(d + XEEN_MAP_MONSTER_EFFECT), ax);
    xeen_write16(m, (uint16_t)(d + XEEN_MAP_MONSTER_EFFECT_CYCLE), ax);
    if (db[row + XEEN_MONSTER_EFFECT]) {
        (void)xeen_entity_rng_range(random(user, (uint16_t)(row & 15u)), 0, 7, &v);
        xeen_write16(m, (uint16_t)(d + XEEN_MAP_MONSTER_EFFECT_STEP), v);
    }
}
/* Installs wall-item entry ENTRY as the map's wall item I: position, sprite slot, the sprite the slot names and the
 * direction. Original: 1A4EE..1A57B. */
static void install_wall_item(xeen_movement *m, unsigned i, const uint8_t *entry, const uint8_t *mob)
{
    uint16_t d = (uint16_t)(XEEN_DS_WALL_ITEMS + i * XEEN_WALL_ITEM_BYTES);
    m->ds[(uint16_t)(d + XEEN_WALL_ITEM_X)] = entry[0];
    m->ds[d + XEEN_WALL_ITEM_Y] = entry[1];
    m->ds[d + XEEN_WALL_ITEM_SLOT] = entry[2];
    m->ds[d + XEEN_WALL_ITEM_SPRITE] = mob[XEEN_MOB_WALL_SPRITES + entry[2]];
    m->ds[d + XEEN_WALL_ITEM_DIRECTION] = entry[3];
}
/*
 * Loads the map's monsters and wall items from its .mob file (MOB, SIZE bytes): after the 48-byte header and the
 * objects' list (its length from the map's object count, ending in the FFh marker), the monsters' list and the wall
 * items' list, each ending in the marker. The lists are found and every entry checked first (a short file
 * XR_TRUNCATED, more than 107 monsters or XEEN_WALL_ITEMS_MAX wall items XR_CAPACITY, an unreadable monster row
 * XR_NOT_FOUND), so a refused file writes nothing; then the records are installed in order (the monsters' draws in
 * their order) and the counts set: none for the Clouds arena (map 85), none for the wall items when the first
 * one's sprite is FFh. On Clouds map 15 with its first three monsters off the map, party flag 56 is cleared.
 */
xeen_resource_status xeen_entities_monsters_walls_aliased(const uint8_t *mob, size_t size,
                                                          const uint8_t *db, size_t file_size,
                                                          size_t alias_start, size_t db_size,
                                                          xeen_movement *m,
                                                          xeen_entity_random_fn random, void *user)
{
    size_t start, end, wall, last, p;
    unsigned i;
    xeen_resource_status s;
    if (!mob || !db || !m || !random) {
        return XR_BAD_INPUT;
    }
    start = XEEN_MOB_HEADER_BYTES +
            ((size_t)m->ds[XEEN_DS_MAP_OBJECT_COUNT] + 1u) * XEEN_MOB_ENTRY_BYTES;
    if (start < XEEN_MOB_HEADER_BYTES + XEEN_MOB_ENTRY_BYTES || start > size ||
        !entity_sentinel(mob, start - XEEN_MOB_ENTRY_BYTES)) {
        return XR_BAD_INPUT;
    }
    if (!list_end(mob, size, start, &end)) {
        return XR_TRUNCATED;
    }
    if ((end - start) / XEEN_MOB_ENTRY_BYTES > 107u) {
        return XR_CAPACITY; /* E530..EDB3 party state follows. */
    }
    wall = end + XEEN_MOB_ENTRY_BYTES;
    if (!list_end(mob, size, wall, &last)) {
        return XR_TRUNCATED;
    }
    if ((last - wall) / XEEN_MOB_ENTRY_BYTES > XEEN_WALL_ITEMS_MAX) {
        return XR_CAPACITY; /* DD28..DFF7 before objects. */
    }
    if ((s = monsters_resolvable(mob, size, start, end, file_size, alias_start, db_size)) != XR_OK) {
        return s;
    }
    for (p = wall; p < last; p += XEEN_MOB_ENTRY_BYTES) {
        if (XEEN_MOB_WALL_SPRITES + mob[p + 2u] >= size) {
            return XR_TRUNCATED;
        }
    }
    for (i = 0, p = start; p < end; i++, p += XEEN_MOB_ENTRY_BYTES) {
        install_monster(m, i, mob + p, mob, db, random, user);
    }
    m->ds[XEEN_DS_MAP_MONSTER_COUNT] = (uint8_t)((end - start) / XEEN_MOB_ENTRY_BYTES);
    /* 1A4CA, the Clouds arena (map 55h, DS 94 0 at 1A0C0): the
     * records are copied, then 13FE7 fills the MOB's monster ids (50D4:
     * 678C, mob + 10h) with FFh and 9C59 = 0 (the sprites: map_mob_ids). */
    if (!m->ds[XEEN_DS_WORLD] && m->ds[XEEN_DS_PARTY_MAP] == 85) {
        m->ds[XEEN_DS_MAP_MONSTER_COUNT] = 0;
    }
    for (i = 0, p = wall; p < last; i++, p += XEEN_MOB_ENTRY_BYTES) {
        install_wall_item(m, i, mob + p, mob);
    }
    m->ds[XEEN_DS_WALL_ITEM_COUNT] = m->ds[XEEN_DS_WALL_ITEMS + XEEN_WALL_ITEM_SPRITE] == 255
                                             ? 0
                                             : (uint8_t)((last - wall) / XEEN_MOB_ENTRY_BYTES);
    /* 1A5BF..1A602: Clouds (DS 94 0 at 1A0C0) map 15 with its
     * first three monster records all off the map (x or y above 1Fh) clears
     * party flag 56 (2F8F3(F01F, 38h, 0)); 30E49 sets it on leaving so. */
    if (!m->ds[XEEN_DS_WORLD] && m->ds[XEEN_DS_PARTY_MAP] == 15u && xeen_map15_monsters_gone(m->ds)) {
        xeen_ds_set_bit(m->ds, XEEN_DS_GAME_FLAGS, 56u, 0);
    }
    return XR_OK;
}
/** 16-bit ADC: returns A + B + *CARRY and leaves the carry out in *CARRY. */
static uint16_t rng_adc(uint16_t a, uint16_t b, unsigned *carry)
{
    uint32_t value = (uint32_t)a + b + *carry;
    *carry = value > 65535u;
    return (uint16_t)value;
}
/** 16-bit SBB: returns A - B - *CARRY and leaves the borrow in *CARRY. */
static uint16_t rng_sbb(uint16_t a, uint16_t b, unsigned *carry)
{
    uint32_t sub = (uint32_t)b + *carry;
    *carry = (uint32_t)a < sub;
    return (uint16_t)((uint32_t)a - sub);
}
/**
 * 16-bit RCR by one: returns A rotated right through *CARRY and leaves the bit shifted out in
 * *CARRY.
 */
static uint16_t rng_rotate(uint16_t a, unsigned *carry)
{
    uint16_t result = (uint16_t)((a >> 1) | (*carry ? 0x8000u : 0u));
    *carry = a & 1u;
    return result;
}
/**
 * The original's random-number mixer: combines AX, CX and DX with the three state words through the
 * same ADC/SBB/RCR sequence, updates STATE and returns the new AX (the raw draw). OUT, when not
 * NULL, receives the final carry flag (0 or 1) of the closing SBB AX,DX (5055D), which the original
 * returns with the draw (xeen_entity_rng_carry).
 */
static uint16_t rng_mix(xeen_entity_rng *state, uint16_t ax, uint16_t cx, uint16_t dx,
                        unsigned *out)
{
    unsigned carry = 0;
    dx ^= state->words[0];
    cx = rng_adc(cx, state->words[1], &carry);
    ax = rng_sbb(ax, state->words[2], &carry);
    cx = rng_adc(cx, cx, &carry);
    ax = rng_adc(ax, ax, &carry);
    cx = rng_adc(cx, cx, &carry);
    dx = rng_rotate(dx, &carry);
    cx = rng_adc(cx, cx, &carry);
    dx = rng_rotate(dx, &carry);
    cx = rng_adc(cx, cx, &carry);
    ax = rng_adc(ax, ax, &carry);
    dx = rng_rotate(dx, &carry);
    cx = rng_adc(cx, cx, &carry);
    state->words[0] = rng_adc(state->words[0], dx, &carry);
    state->words[1] = rng_sbb(state->words[1], cx, &carry);
    state->words[2] ^= ax;
    carry = 0;
    ax = rng_adc(ax, cx, &carry);
    ax = rng_sbb(ax, dx, &carry);
    if (out) {
        *out = carry;
    }
    return ax;
}
/**
 * Runs the random-number mixer on STATE with the given AX, CX and DX; returns the raw draw (0 when
 * STATE is NULL).
 */
uint16_t xeen_entity_rng_mix(xeen_entity_rng *state, uint16_t ax, uint16_t cx, uint16_t dx)
{
    if (!state) {
        return 0;
    }
    return rng_mix(state, ax, cx, dx, NULL);
}
int xeen_entity_rng_carry(const xeen_rng_transition *t)
{
    xeen_entity_rng copy;
    unsigned carry = 0;
    if (!t) {
        return 0;
    }
    copy = t->before;
    (void)rng_mix(&copy, t->post_time_ax, t->cx, t->dx, &carry);
    return (int)carry;
}

/**
 * Draws one raw random number the way the original does: AX 2C00h and the DOS time (CX hour:minute,
 * DX second:hundredth) are mixed into STATE. TRACE, when not NULL, records the inputs, the result
 * and the state before and after.
 */
uint16_t xeen_entity_rng_draw(xeen_entity_rng *state, uint16_t incoming, xeen_rng_time time,
                              xeen_rng_transition *trace)
{
    uint16_t cx = (uint16_t)((uint16_t)time.hour * 256u + time.minute);
    uint16_t dx = (uint16_t)((uint16_t)time.second * 256u + time.hundredth);
    xeen_entity_rng before = *state;
    uint16_t raw = xeen_entity_rng_mix(state, 0x2c00u, cx, dx);
    if (trace) {
        *trace = (xeen_rng_transition){incoming, 0x2c00u, cx, dx, raw, before, *state};
    }
    return raw;
}

int xeen_entity_rng_range(uint16_t raw, uint16_t low, uint16_t high, uint16_t *result)
{
    uint16_t width = (uint16_t)(high + 1u - low);
    int32_t divisor = width < 0x8000u ? (int32_t)width : (int32_t)width - 65536;
    if (!divisor || !result) {
        return 0;
    }
    *result = (uint16_t)((int32_t)low + (int32_t)(raw & 0x7fffu) % divisor);
    return 1;
}
