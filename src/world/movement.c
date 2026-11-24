#include "xeen/party.h"
#include "xeen/character_record.h"
#include "xeen/ds_globals.h"
#include "xeen/monster_record.h"
#include "xeen/movement.h"
#include <string.h>
#include "movement_internal.h"
#include "movement_tables.inc"
/* Slot SLOT's map record and a field offset (xeen/map_record.h) in int arithmetic, as these sites compute them. */
#define SLOT_DATA(slot)  ((int)XEEN_DS_MAP_DATA + (slot) * (int)XEEN_MAP_RECORD_BYTES)
#define MAP_FIELD(field) ((int)XEEN_MAP_RECORD_##field)
uint16_t xeen_read16(const xeen_movement *m, uint16_t a)
{
    return (uint16_t)((uint16_t)m->ds[a] | (uint16_t)((uint16_t)m->ds[(uint16_t)(a + 1u)] << 8));
}
void xeen_write16(xeen_movement *m, uint16_t a, uint16_t v)
{
    B(a) = (uint8_t)v;
    B(a + 1u) = (uint8_t)(v >> 8);
}
/** Interprets a byte as the original's signed value (CBW). */
int xeen_signed_byte(uint8_t b)
{
    return b < 128u ? (int)b : (int)b - 256;
}
/** Interprets a 16-bit word as the original's signed value, widened to 32 bits. */
int32_t xeen_movement_signed_word(uint16_t w)
{
    return w < 32768u ? (int32_t)w : (int32_t)w - INT32_C(65536);
}
/* SAR on a 16-bit word by COUNT (0..31): the sign fills from 15 on. */
static uint16_t xeen_signed_word_sar(uint16_t w, uint16_t count)
{
    int32_t v = xeen_movement_signed_word(w);
    if (count > 15u) {
        count = 15u;
    }
    return (uint16_t)(v < 0 ? ~((~(uint32_t)(int32_t)v) >> count) : (uint32_t)v >> count);
}
/** Stores the party's direction, position and map (DS ED96..ED99). */
void xeen_navigation_set(xeen_movement *m, xeen_navigation n)
{
    B(XEEN_DS_PARTY_DIRECTION) = n.direction;
    B(XEEN_DS_PARTY_X) = n.x;
    B(XEEN_DS_PARTY_Y) = n.y;
    B(XEEN_DS_PARTY_MAP) = n.map;
}
void xeen_movement_init(xeen_movement *m, xeen_service_fn fn, void *user)
{
    memset(m, 0, sizeof(*m));
    m->service = fn;
    m->user = user;
    memcpy(m->ds + 0x64e, offset_x, sizeof(offset_x));
    memcpy(m->ds + 0x70e, offset_y, sizeof(offset_y));
    memcpy(m->ds + XEEN_DS_MOVEMENT_WALL_SHIFT, shifts, sizeof(shifts));
}
/**
 * Asks the host for movement service S with arguments A and B and returns its result.
 *
 * A refused service sets m->status to XEEN_SERVICE_REQUIRED; once the status is not XEEN_OK no
 * further service is requested and 0 is returned.
 */
uint16_t xeen_movement_request_service(xeen_movement *m, xeen_service s, uint16_t a, uint16_t b)
{
    uint16_t r = 0;
    if (m->status == XEEN_OK && (!m->service || !m->service(m->user, m, s, a, b, &r))) {
        m->status = XEEN_SERVICE_REQUIRED;
    }
    return r;
}
/**
 * Finds map ID in the nine loaded-map slots (DS F46A) and makes it the current slot (F494).
 *
 * Returns the slot index, or -1 with m->status XEEN_CACHE_MISS when the map is not loaded.
 */
static int select_map_slot(xeen_movement *m, uint16_t id)
{
    int i;
    for (i = 0; i < 9; i++) {
        if (W(XEEN_DS_MAP_SLOT_IDS + 2u * (unsigned)i) == id) {
            S(XEEN_DS_MAP_SLOT, i);
            return i;
        }
    }
    m->status = XEEN_CACHE_MISS;
    return -1;
}
/**
 * The cell value of a queried position beyond the loaded maps' 32x32 window.
 *
 * Sets the surface F490 (6 on the Darkside maps listed in fallback6, 7 on Clouds maps 19h..1Bh,
 * else 0) and F48E to 8888h, and returns 8888h, as the original's map query does.
 */
static uint16_t beyond_map_cell(xeen_movement *m)
{
    unsigned id = B(XEEN_DS_PARTY_MAP);
    uint16_t v = 0;
    if (B(XEEN_DS_WORLD)) {
        if (id >= 0x35u && id <= 0x81u && fallback6[id - 0x35u]) {
            v = 6;
        }
    } else if (id >= 0x19u && id <= 0x1bu) {
        v = 7;
    }
    S(XEEN_DS_CELL_SURFACE, v);
    S(XEEN_DS_CELL_WALLS, XEEN_CELL_OUTSIDE_MAPS);
    return XEEN_CELL_OUTSIDE_MAPS;
}
/* Walks the cell (X, Y) of map *ID across the map edges: per axis (Y first), a coordinate outside 0..15 (bit 4) is
 * brought back by 16 and *ID becomes the neighbour the current map's south/north (or west/east) link names. Returns
 * the cache slot of the map the cell lies in; -1 when a map is not in the native cache; with MISSING_STOP, -2 at a
 * zero link (otherwise a zero link keeps the previous map's slot and *ID 0: the boundary helper's rule). */
static int traverse(xeen_movement *m, int *x, int *y, uint16_t *id, int missing_stop)
{
    int s = select_map_slot(m, *id), axis;
    if (s < 0) {
        return -1;
    }
    for (axis = 0; axis < 2; axis++) {
        int *c = axis == 0 ? y : x;
        if (((uint16_t)*c & 16u) != 0u) {
            int off = axis == 0 ? (*c < 0 ? MAP_FIELD(SOUTH) : MAP_FIELD(NORTH))
                                : (*c < 0 ? MAP_FIELD(WEST) : MAP_FIELD(EAST));
            *c += *c < 0 ? 16 : -16;
            *id = W(SLOT_DATA(s) + off);
            if (!*id) {
                if (missing_stop) {
                    return -2;
                }
            } else {
                s = select_map_slot(m, *id);
                if (s < 0) {
                    return -1;
                }
            }
        }
    }
    return s;
}
xeen_status xeen_navigation_query(xeen_movement *m, uint16_t index, uint16_t *ax)
{
    int x, y, slot;
    uint16_t table_index, map_id, cell_word, shift;
    m->status = XEEN_OK;
    /* 1C1C1: the view slot INDEX (0..47) from the facing's step tables. No facing bound in the original (the mirror
     * "&" leaves ED96 = 50h): t = ED96 x 30h + index in 16 bits indexes XEEN_DS_STEP_X/Y and the wall shifts. */
    if (!ax || index >= 48) {
        return m->status = XEEN_BAD_INPUT;
    }
    table_index = (uint16_t)(48u * B(XEEN_DS_PARTY_DIRECTION) + index);
    x = xeen_signed_byte(B(XEEN_DS_PARTY_X)) + xeen_signed_byte(B(XEEN_DS_STEP_X(0u, table_index)));
    y = xeen_signed_byte(B(XEEN_DS_PARTY_Y)) + xeen_signed_byte(B(XEEN_DS_STEP_Y(0u, table_index)));
    if (x > 31 || y > 31) {
        *ax = beyond_map_cell(m);
        return m->status;
    }
    map_id = B(XEEN_DS_PARTY_MAP);
    slot = traverse(m, &x, &y, &map_id, 1);
    if (slot == -1) {
        return m->status;
    }
    if (slot == -2) {
        if (W(XEEN_DS_OUTDOORS)) {
            S(XEEN_DS_CELL_WALLS, 0);
            S(XEEN_DS_CELL_SURFACE, 15);
            *ax = 0;
        } else {
            *ax = beyond_map_cell(m);
        }
        return m->status;
    }
    cell_word = W(SLOT_DATA(slot) + y * 32 + x * 2);
    if (W(XEEN_DS_OUTDOORS)) {
        S(0xf48c, map_id ? cell_word >> 8 : 0); /* F48C: the outdoor cell word's high byte (numeric: no name yet) */
        S(XEEN_DS_CELL_WALLS, map_id ? (cell_word >> 4) & 15u : 0);
        S(XEEN_DS_CELL_SURFACE, map_id ? cell_word & 15u : 0);
        *ax = W(XEEN_DS_CELL_WALLS);
    } else if (map_id) {
        S(XEEN_DS_CELL_SURFACE, B(SLOT_DATA(slot) + MAP_FIELD(METADATA) + y * 16 + x) & 7u);
        S(XEEN_DS_CELL_WALLS, cell_word);
        /* 1C499: CL = the byte 7CE + t x 2; 1C4A3 SAR AX, CL (the count
         * masked to 5 bits, a count of 15 or more fills with the sign). */
        shift = (uint16_t)(B(XEEN_DS_MOVEMENT_WALL_SHIFT + 2u * table_index) & 31u);
        *ax = (uint16_t)((uint16_t)(xeen_signed_word_sar(cell_word, shift)) & 15u);
    } else {
        *ax = 0;
    }
    return m->status;
}
xeen_status xeen_wall_set(xeen_movement *m, int16_t x0, int16_t y0, uint16_t direction,
                          uint16_t value)
{
    int x = x0, y = y0, s;
    uint16_t id, shift, word, mask, address;
    if (!m) {
        return XEEN_BAD_INPUT;
    }
    m->status = XEEN_OK;
    if (direction > 3u || value > 15u) {
        return m->status = XEEN_BAD_INPUT;
    }
    id = B(XEEN_DS_PARTY_MAP);
    s = traverse(m, &x, &y, &id, 1);
    if (s < 0) {
        return m->status == XEEN_OK ? (m->status = XEEN_BAD_INPUT) : m->status;
    }
    shift = W(XEEN_DS_WALL_SHIFT(direction, 2u));
    if (shift > 15u) {
        return m->status = XEEN_BAD_INPUT;
    }
    address = (uint16_t)(XEEN_DS_MAP_DATA + (unsigned)s * XEEN_MAP_RECORD_BYTES + ((unsigned)y & 15u) * 32u +
                         ((unsigned)x & 15u) * 2u);
    word = W(address);
    mask = (uint16_t) ~(uint16_t)(15u << shift);
    S(address, (uint16_t)((word & mask) | (uint16_t)(value << shift)));
    return m->status;
}
/*
 * Outdoors, whether the party may enter the cell the step's navigation query looked up, by its terrain
 * (CELL_WALLS): the mountains (and in Darkside kind 5, "snotree" by its own class) need a Mountaineer; kinds 3 and
 * 6 (by their own classes the dense forests "dtree" and "dsnotree") a Path Finder; another terrain above 14 is
 * refused; else by the surface's classification: deep water needs Walk on Water, water a Swimmer or Walk on
 * Water, space is refused and every other surface is open. A skill is asked of the host (XEEN_ABILITY); a refused
 * request leaves m->status SERVICE_REQUIRED and answers no.
 * Original: 1780E..17890 (the terrain table CS:0831; the classifications 17833..1784D: water, deep water 8, space
 * 0Fh to the refusal 17886, the others to 178FC).
 */
static int outdoor_step_allowed(xeen_movement *m)
{
    uint16_t terrain = W(XEEN_DS_CELL_WALLS);
    uint8_t surface;
    if (xeen_outdoor_mountain_kind(terrain) || (terrain == 5 && B(XEEN_DS_WORLD))) {
        return xeen_movement_request_service(m, XEEN_ABILITY, XEEN_SKILL_MOUNTAINEER, 0) != 0;
    }
    if (terrain == 3 || terrain == 6) {
        return xeen_movement_request_service(m, XEEN_ABILITY, XEEN_SKILL_PATH_FINDER, 0) != 0;
    }
    if (terrain > 14) {
        return 0;
    }
    surface = B(XEEN_DS_MAP_FIELD(CLASSIFICATION) + W(XEEN_DS_CELL_SURFACE));
    if (surface == XEEN_SURFACE_DEEP_WATER) {
        return B(XEEN_DS_EFFECT_WALK_ON_WATER) != 0;
    }
    if (surface == XEEN_SURFACE_WATER) {
        return xeen_movement_request_service(m, XEEN_ABILITY, XEEN_SKILL_SWIMMER, 0) != 0 ||
               B(XEEN_DS_EFFECT_WALK_ON_WATER) != 0;
    }
    return surface != XEEN_SURFACE_SPACE;
}
/*
 * Indoors, whether the party may take the step COMMAND: the wall of its own cell in the step's direction (the
 * navigation query 2, with the party still turned that way; FACING restored after it) must be below the map's
 * threshold, and a destination of surface index 4 (DESTINATION, the cell's metadata surface the first query left)
 * needs a Swimmer or Walk on Water. A forward step that is allowed and has a door face ahead (the view's locked-door
 * face) plays the door scene: DOOR_OPENING set, sound 2Fh, scene 1, DOOR_OPENING cleared once the scene returns (a
 * suspended scene keeps it for its caller, which resumes at 178F7). A failed query or a refused request leaves
 * m->status set; the answer is then not used.
 * Original: 17892..178FC (the threshold C0FC 1789A; surface index 4 178B4; the door 178D3..178F7).
 */
static int indoor_step_allowed(xeen_movement *m, uint16_t command, uint8_t facing, uint8_t destination)
{
    uint16_t wall = 0;
    int ok;
    xeen_navigation_query(m, XEEN_STEP_HERE, &wall);
    B(XEEN_DS_PARTY_DIRECTION) = facing;
    if (m->status != XEEN_OK || xeen_movement_signed_word(wall) >= B(XEEN_DS_MAP_FIELD(THRESHOLD))) {
        return 0;
    }
    ok = destination != 4 || xeen_movement_request_service(m, XEEN_ABILITY, XEEN_SKILL_SWIMMER, 0) != 0 ||
         B(XEEN_DS_EFFECT_WALK_ON_WATER) != 0;
    /* a face of kind 4 or 13 ahead (the view's door faces; xeen/map_record.h's indoor wall kinds) */
    if (ok && command == XEEN_MOVE_FORWARD &&
        B(XEEN_DS_VIEW_FACE(XEEN_INDOOR_WALL_LOCKED_DOOR, XEEN_VIEW_FACE_AHEAD))) {
        B(XEEN_DS_DOOR_OPENING) = 1;
        xeen_movement_request_service(m, XEEN_SOUND, 0x2f, 0);
        xeen_movement_request_service(m, XEEN_SCENE, 1, 0);
        if (m->status == XEEN_OK) {
            B(XEEN_DS_DOOR_OPENING) = 0;
        }
    }
    return ok;
}
/*
 * Whether the party may take the step COMMAND (forward, back, strafe left or right): the party turns to the step's
 * direction for the navigation query 7 (the destination cell; its surface kept), then the outdoor or the indoor
 * rule decides (outdoor_step_allowed, indoor_step_allowed), the facing restored in both. A refused step plays the
 * bump sound (15h outdoors, 2Eh indoors). *ALLOWED is 1 or 0, written only when the gate ends XEEN_OK.
 * Returns m->status: BAD_INPUT for a facing above 3, another command or no ALLOWED; a failed query's status; or
 * SERVICE_REQUIRED when the host refused a skill, sound or scene request (the door scene's suspension).
 * Original: 177AE..17900 (the command F5FE; the query 7 at 177F6; the refusal sound 17886, AX 0; allowed 178FC, AX 1).
 */
xeen_status xeen_translation_gate(xeen_movement *m, uint16_t command, uint16_t *allowed)
{
    uint8_t facing = B(XEEN_DS_PARTY_DIRECTION), destination;
    uint16_t ax = 0;
    int ok;
    m->status = XEEN_OK;
    if (!allowed || facing > 3 ||
        (command != XEEN_MOVE_FORWARD && command != XEEN_MOVE_BACK && command != XEEN_MOVE_STRAFE_LEFT &&
         command != XEEN_MOVE_STRAFE_RIGHT)) {
        return m->status = XEEN_BAD_INPUT;
    }
    *allowed = 0;
    B(XEEN_DS_PARTY_DIRECTION) = (uint8_t)xeen_step_direction(facing, command);
    xeen_navigation_query(m, 7, &ax);
    destination = B(XEEN_DS_CELL_SURFACE);
    if (m->status != XEEN_OK) {
        B(XEEN_DS_PARTY_DIRECTION) = facing;
        return m->status;
    }
    if (W(XEEN_DS_OUTDOORS)) {
        B(XEEN_DS_PARTY_DIRECTION) = facing;
        ok = outdoor_step_allowed(m);
    } else {
        ok = indoor_step_allowed(m, command, facing, destination);
    }
    if (m->status != XEEN_OK) {
        return m->status;
    }
    if (!ok) {
        xeen_movement_request_service(m, XEEN_SOUND, W(XEEN_DS_OUTDOORS) ? 0x15 : 0x2e, 0);
    }
    if (m->status == XEEN_OK) {
        *allowed = (uint16_t)ok;
    }
    return m->status;
}
/* The party's step one cell ahead on its facing (xeen_direction_step), each coordinate a byte that wraps as the
 * original's INC/DEC does; a facing above 3 moves nothing. No gate, no bound: the caller allowed the step.
 * Original: 17CFD..17D13 (the forward step's byte INC/DEC). */
void xeen_forward_delta(xeen_movement *m)
{
    int dx = 0, dy = 0;
    if (xeen_direction_step(B(XEEN_DS_PARTY_DIRECTION), &dx, &dy)) {
        B(XEEN_DS_PARTY_X) = (uint8_t)(B(XEEN_DS_PARTY_X) + dx);
        B(XEEN_DS_PARTY_Y) = (uint8_t)(B(XEEN_DS_PARTY_Y) + dy);
    }
}
xeen_status xeen_scene_prefix(xeen_movement *m)
{
    uint8_t advanced;
    m->status = XEEN_OK;
    /* The animation step counter: an 8-bit increment (wrapping at 256) before the division by four. */
    advanced = (uint8_t)((unsigned)B(XEEN_DS_SURFACE_STEP) + 1u);
    B(XEEN_DS_SURFACE_STEP) = (uint8_t)(advanced % 4u);
    if (!B(XEEN_DS_SURFACE_STEP)) {
        S(XEEN_DS_SURFACE_FLIP, W(XEEN_DS_SURFACE_FLIP) ^ 0x8000u);
    }
    if (B(XEEN_DS_MONSTER_COUNTDOWN) &&
        (B(XEEN_DS_UI_MODE) == XEEN_UI_MODE_EXPLORATION ||
         B(XEEN_DS_UI_MODE) == XEEN_UI_MODE_COMBAT) &&
        !B(XEEN_DS_MONSTER_SHOTS_QUEUED) && B(XEEN_DS_MONSTERS_ENABLED)) {
        B(XEEN_DS_MONSTER_COUNTDOWN) = (uint8_t)(B(XEEN_DS_MONSTER_COUNTDOWN) - 1u);
        if (!B(XEEN_DS_MONSTER_COUNTDOWN)) {
            xeen_movement_request_service(m, XEEN_96C0, 0, 0);
        }
    }
    return m->status;
}
xeen_status xeen_boundary_commit(xeen_movement *m)
{
    int x = xeen_signed_byte(B(XEEN_DS_PARTY_X)), y = xeen_signed_byte(B(XEEN_DS_PARTY_Y)), s;
    uint16_t id = B(XEEN_DS_PARTY_MAP);
    m->status = XEEN_OK;
    if (!((B(XEEN_DS_PARTY_X) | B(XEEN_DS_PARTY_Y)) & 16u)) {
        return m->status = XEEN_BAD_INPUT;
    }
    s = traverse(m, &x, &y, &id, 0);
    if (s < 0) {
        return m->status;
    }
    B(XEEN_DS_PARTY_X) = (uint8_t)x;
    B(XEEN_DS_PARTY_Y) = (uint8_t)y;
    if (id) {
        xeen_movement_request_service(m, XEEN_LOAD_MAP, id, 0);
    }
    return m->status;
}
xeen_status xeen_metadata(xeen_movement *m)
{
    return xeen_metadata_at(m, (int16_t)xeen_signed_byte(B(XEEN_DS_PARTY_X)),
                            (int16_t)xeen_signed_byte(B(XEEN_DS_PARTY_Y)));
}
/* 1C844(x, y). as the original, with no range check: per axis
 * whose bit 4 is set (y first: SI, then x: CX) one step through the slot's
 * link word (C0D2/C0CE, C0D4/C0D0), then the F46A search for that id, a
 * zero link included (the original has no zero test: it finds the first
 * slot holding id 0); the cell byte is BFCC + slot x 37Ch + y x 16 + x in
 * 16-bit arithmetic over the DS image. The mirror "&" (Clouds map 4Dh,
 * (56,56), no neighbours) so reads C5F0 in slot 1 (verified in original
 * bytes). The original's search is unbounded; a miss within the 9 slots
 * stays XEEN_CACHE_MISS (the DS-wide search is not verified). */
xeen_status xeen_metadata_at(xeen_movement *m, int16_t x0, int16_t y0)
{
    static const uint16_t flag_words[] = {XEEN_DS_CELL_UNLOCKED, XEEN_DS_CELL_NO_REST, XEEN_DS_CELL_DARK, XEEN_DS_CELL_STEP_EVENT,
                                          0xf480, 0xf47e};
    static const uint8_t flag_masks[] = {128, 64, 32, 16, 8, 7};
    uint16_t x = (uint16_t)x0, y = (uint16_t)y0, map_id = B(XEEN_DS_PARTY_MAP);
    int slot, i;
    uint8_t cell;
    m->status = XEEN_OK;
    if ((slot = select_map_slot(m, map_id)) < 0) {
        return m->status;
    }
    if (y & 16u) {
        map_id = W(SLOT_DATA(slot) + ((y & 0x8000u) ? MAP_FIELD(SOUTH) : MAP_FIELD(NORTH)));
        y = (uint16_t)(y + ((y & 0x8000u) ? 16u : 0xfff0u));
        if ((slot = select_map_slot(m, map_id)) < 0) {
            return m->status;
        }
    }
    if (x & 16u) {
        map_id = W(SLOT_DATA(slot) + ((x & 0x8000u) ? MAP_FIELD(WEST) : MAP_FIELD(EAST)));
        x = (uint16_t)(x + ((x & 0x8000u) ? 16u : 0xfff0u));
        if ((slot = select_map_slot(m, map_id)) < 0) {
            return m->status;
        }
    }
    cell = B(XEEN_DS_MAP_FIELD(METADATA) + (uint16_t)((unsigned)slot * XEEN_MAP_RECORD_BYTES) + (uint16_t)(y << 4) + x);
    for (i = 0; i < 6; i++) {
        S(flag_words[i], cell & flag_masks[i]);
    }
    return m->status;
}
int xeen_cache_traverse(xeen_movement *m, int *x, int *y, uint16_t *id)
{
    return traverse(m, x, y, id, 1);
}
/* 1C952. The Cartographer test (2BEF9 for skill 4) is true for any member with a nonzero skill byte; conditions are
 * not consulted. This is deliberately not a general skill service. */
xeen_status xeen_scene_light(xeen_movement *m)
{
    unsigned member;
    if (!m) {
        return XEEN_BAD_INPUT;
    }
    if (B(XEEN_DS_PARTY_ACTED)) {
        uint16_t cell, bit, address;
        if (xeen_metadata(m) != XEEN_OK) {
            return m->status;
        }
        if (W(XEEN_DS_CELL_DARK) && W(XEEN_DS_PARTY_LIGHT)) {
            S(XEEN_DS_PARTY_LIGHT, W(XEEN_DS_PARTY_LIGHT) - 1u);
        }
        for (member = 0; member < B(XEEN_DS_PARTY_COUNT); member++) {
            if (B(XEEN_PARTY_RECORD(member) + XEEN_CHARACTER_SKILLS + XEEN_SKILL_CARTOGRAPHER)) {
                break;
            }
        }
        if (member < B(XEEN_DS_PARTY_COUNT)) {
            cell = (uint16_t)((B(XEEN_DS_PARTY_Y) & 15u) * 16u + (B(XEEN_DS_PARTY_X) & 15u));
            bit = (uint16_t)(0x80u >> (cell & 7u));
            address = (uint16_t)(XEEN_DS_MAP_FIELD(SEEN) + W(XEEN_DS_MAP_SLOT) * XEEN_MAP_RECORD_BYTES + (cell >> 3u));
            B(address) |= (uint8_t)bit;
        }
    }
    S(XEEN_DS_VIEW_LIGHT_LEVEL, (!W(XEEN_DS_PARTY_LIGHT) && (W(XEEN_DS_MAP_FIELD(MODE)) & XEEN_MAP_MODE_DARK)) ? 0 : 4);
    return XEEN_OK;
}
