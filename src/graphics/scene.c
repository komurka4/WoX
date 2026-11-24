#include "xeen/byte_order.h"
#include "xeen/map_objects.h"
#include "xeen/monster_record.h"
#include "xeen/ds_globals.h"
#include "xeen/program_data.h"
#include "xeen/raster.h"
#include "xeen/scene.h"
#include "xeen/windows.h"
#include <stdlib.h>
#include <string.h>
/** Copies one 12-byte scene draw record from DS SOURCE to DS DESTINATION. */
static void copy_scene_record(xeen_movement *m, uint16_t destination, uint16_t source)
{
    unsigned i;
    for (i = 0; i < 12; i += 2) {
        xeen_write16(m, (uint16_t)(destination + i), xeen_read16(m, (uint16_t)(source + i)));
    }
}
/** Stops the scene build at unsupported original ADDRESS (in m->status too). */
static xeen_caller_result boundary(xeen_movement *m, uint32_t address)
{
    xeen_caller_result r = {XEEN_SERVICE_REQUIRED, address};
    if (m) {
        m->status = r.status;
    }
    return r;
}
/* The two view lists the scene builds: the indoor one (XEEN_DS_INDOOR_VIEW_LIST) and the outdoor one
 * (XEEN_DS_OUTDOOR_VIEW_LIST). The same view cells and slots have a record in each, by number. */
enum view_list { INDOOR_LIST, OUTDOOR_LIST };
/* The DS offset of record NUMBER of view list LIST. */
static uint16_t view_record(enum view_list list, unsigned number)
{
    return (uint16_t)(list == OUTDOOR_LIST ? XEEN_DS_OUTDOOR_VIEW_RECORD(number) : XEEN_DS_INDOOR_VIEW_RECORD(number));
}
/**
 * Returns the offset in XEEN_DS_OBJECT_ANIMATION of the map object at OBJECT's animation record for the party's
 * FACING: its sprite's 12-byte record plus the pose (XEEN_DS_OBJECT_POSES by the object's own facing x 4 + FACING).
 */
static uint16_t frame_base(const xeen_movement *m, uint16_t object, uint16_t facing)
{
    uint16_t pose = (uint16_t)((uint16_t)m->ds[(uint16_t)(object + XEEN_MAP_OBJECT_DIRECTION)] * 4u + facing),
             map = (uint16_t)(XEEN_DS_OBJECT_POSES + pose);
    return (uint16_t)((uint16_t)m->ds[(uint16_t)(object + XEEN_MAP_OBJECT_SPRITE)] * XEEN_OBJECT_ANIMATION_BYTES +
                      m->ds[map]);
}
/* Whether OBJECT, the cell's event object, is a chest whose lock is being picked: a positive pick count (signed) and
 * a chest sprite (Darkside's 15, Clouds' 16, or 58 or 73 on either world; the map load and the opened chest keep
 * the sprite byte equal to the object's id). Original: 1B5CA (98D9, 9755 > 0; 50D4:677C of the id byte). */
static int picking_chest(const xeen_movement *m, uint16_t object)
{
    uint8_t sprite = m->ds[(uint16_t)(object + XEEN_MAP_OBJECT_SPRITE)];
    return m->ds[XEEN_DS_OBJECT_LOCK] > 0 && m->ds[XEEN_DS_OBJECT_LOCK] < 0x80 &&
           (sprite == (m->ds[XEEN_DS_WORLD] ? 15u : 16u) || sprite == 58u || sprite == 73u);
}
/*
 * Map object OBJECT's animation for one scene, at the party's FACING: its frame counts up to its pose's limit and
 * starts again at the pose's reset value, or while the party turns (FROZEN) stays at the reset value; the chest
 * whose lock is being picked (SELECTED, the event object, and picking_chest) runs its own frames instead: past 4
 * (always, for sprite 58) back to 1, never its pose's reset. Then the pose's flip becomes the object's
 * XEEN_MAP_OBJECT_FLIP.
 * The pose (its animation record): the original computes it again at each of its three uses; the only write in
 * between is the frame byte (E003h + 8 i), which is not one of the bytes it is computed from (the object's +4 and
 * +5, XEEN_DS_OBJECT_POSES, the animation records below 0E00h). Original: 1B5BA..1B6F6 (1B624..1B633 the chest).
 */
static void animate_object(xeen_movement *m, uint16_t object, uint16_t facing, int frozen, int selected)
{
    uint16_t pose = (uint16_t)(XEEN_DS_OBJECT_ANIMATION + frame_base(m, object, facing)),
             frame = (uint16_t)(object + XEEN_MAP_OBJECT_FRAME);
    if (frozen) {
        m->ds[frame] = m->ds[(uint16_t)(pose + XEEN_OBJECT_ANIMATION_RESET)];
    } else {
        m->ds[frame] = (uint8_t)(m->ds[frame] + 1u);
        if (selected && picking_chest(m, object)) {
            if (m->ds[frame] > 4u || m->ds[(uint16_t)(object + XEEN_MAP_OBJECT_SPRITE)] == 58u) {
                m->ds[frame] = 1;
            }
        } else if (m->ds[(uint16_t)(pose + XEEN_OBJECT_ANIMATION_LIMIT)] < m->ds[frame]) {
            m->ds[frame] = m->ds[(uint16_t)(pose + XEEN_OBJECT_ANIMATION_RESET)];
        }
    }
    m->ds[(uint16_t)(object + XEEN_MAP_OBJECT_FLIP)] = m->ds[(uint16_t)(pose + XEEN_OBJECT_ANIMATION_FLIP)];
}
/*
 * The stage after the frame timer: while the view's window is open it returns to the command poll at once; else the
 * scene prefix (xeen_scene_prefix: the surface step, the monster countdown), then every map object's animation
 * (animate_object). The party's facing, the event object, the object count and TURNED are read once, before the
 * first object, as the original does. Original: 1B510..1B70F (1B572.. the objects, their fields +3, +4, +5 and +6
 * walked with four pointers).
 */
xeen_caller_result xeen_scene_after_timer(xeen_movement *m)
{
    uint16_t i, object = XEEN_DS_MAP_OBJECTS, facing, selected, count;
    uint8_t frozen;
    if (!m) {
        xeen_caller_result r = {XEEN_BAD_INPUT, 0x1b510};
        return r;
    }
    m->status = XEEN_OK;
    if (m->ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_VIEW)]) {
        return boundary(m, XEEN_LOOP_COMMAND_POLL);
    }
    if (xeen_scene_prefix(m) != XEEN_OK) {
        xeen_caller_result r = {m->status, 0x2a701};
        return r;
    }
    facing = m->ds[XEEN_DS_PARTY_DIRECTION];
    selected = (uint16_t)((uint16_t)m->ds[XEEN_DS_EVENT_OBJECT] - 1u);
    count = m->ds[XEEN_DS_MAP_OBJECT_COUNT];
    frozen = m->ds[XEEN_DS_TURNED];
    for (i = 0; i < count; i++, object = (uint16_t)(object + XEEN_MAP_OBJECT_BYTES)) {
        animate_object(m, object, facing, frozen != 0, i == selected);
    }
    return boundary(m, XEEN_SCENE_PREPARE_RENDER);
}

/*
 * The projectiles: 8 records a level (one per party member) at four levels (XEEN_DS_*_PROJECTILES). A flight passes
 * the levels one step a scene: a monster's shot (while XEEN_DS_MONSTER_SHOTS_QUEUED; checked first) from level 3,
 * the farthest, to level 0, the party's cell; the party's missiles (while XEEN_DS_MISSILES_FLYING) from level 0 out
 * to level 3. FLIGHT_LEVEL is the record of member I at step STEP (0..3) of the current flight in LIST.
 */
static int monster_shot_flight(const xeen_movement *m)
{
    return m->ds[XEEN_DS_MONSTER_SHOTS_QUEUED] != 0;
}
static uint16_t flight_level(enum view_list list, int monster_shot, unsigned step, unsigned i)
{
    unsigned level = monster_shot ? 3u - step : step;
    uint16_t first = list == OUTDOOR_LIST ? XEEN_DS_OUTDOOR_PROJECTILES(level) : XEEN_DS_INDOOR_PROJECTILES(level);
    return (uint16_t)(first + XEEN_VIEW_RECORD_BYTES * i);
}
/* Each member's projectile is drawn with the frame of its flight step: the first level in flight order whose record
 * is taken (its handle not FFFFh) gets frame 0, 1 or 2 for steps 0..2 and 0 again at the last step. The records'
 * handles and frames are words apart and the levels are fixed, so the tests read no frame this writes. Original:
 * outdoors 1B737..1B7D3, indoors 1BB3C..1BBD8. */
static void set_projectile_frames(xeen_movement *m, enum view_list list)
{
    static const uint16_t frame_at_step[4] = {0, 1, 2, 0};
    unsigned i, step;
    int monster_shot = monster_shot_flight(m);
    if (!monster_shot && !m->ds[XEEN_DS_MISSILES_FLYING]) {
        return;
    }
    for (i = 0; i < 8; i++) {
        for (step = 0; step < 4; step++) {
            uint16_t record = flight_level(list, monster_shot, step, i);
            if (xeen_read16(m, record) != XEEN_VIEW_RECORD_UNUSED) {
                xeen_write16(m, (uint16_t)(record + XEEN_VIEW_RECORD_FRAME), frame_at_step[step]);
                break;
            }
        }
    }
}
/*
 * The render preparation: outdoors the frames of the outdoor_hidden_records (each table word read when its turn
 * comes: a write may land on a later one), indoors the frames of records 3..169 are set to FFFFh (unused); then the
 * projectiles' frames (set_projectile_frames). Outdoors TURNED is cleared before the outdoor effects; indoors the
 * view-wall scan comes next. Original: 1B70F..1B7D5 / 1BB28..1BBDA.
 */
xeen_caller_result xeen_scene_prepare_render(xeen_movement *m)
{
    uint16_t i;
    int outdoor;
    if (!m) {
        xeen_caller_result r = {XEEN_BAD_INPUT, XEEN_SCENE_PREPARE_RENDER};
        return r;
    }
    outdoor = xeen_read16(m, XEEN_DS_OUTDOORS) != 0;
    if (outdoor) {
        for (i = XEEN_DS_OUTDOOR_HIDDEN_RECORDS; i != XEEN_DS_OUTDOOR_HIDDEN_RECORDS + 2u * XEEN_OUTDOOR_HIDDEN_RECORDS;
             i = (uint16_t)(i + 2u)) {
            uint16_t address = (uint16_t)(XEEN_DS_OUTDOOR_VIEW_LIST + XEEN_VIEW_RECORD_FRAME + xeen_read16(m, i) * 12u);
            xeen_write16(m, address, XEEN_VIEW_RECORD_UNUSED);
        }
    } else {
        for (i = XEEN_VIEW_FIELD(XEEN_DS_INDOOR_VIEW_LIST, 3u, FRAME);
             i != XEEN_VIEW_FIELD(XEEN_DS_INDOOR_VIEW_LIST, 170u, FRAME); i = (uint16_t)(i + XEEN_VIEW_RECORD_BYTES)) {
            xeen_write16(m, i, XEEN_VIEW_RECORD_UNUSED);
        }
    }
    set_projectile_frames(m, outdoor ? OUTDOOR_LIST : INDOOR_LIST);
    if (outdoor) {
        m->ds[XEEN_DS_TURNED] = 0;
        return boundary(m, XEEN_SCENE_OUTDOOR_EFFECTS);
    }
    return boundary(m, XEEN_SCENE_VIEW_WALL_SCAN);
}

/*
 * The view slots' places in the wall flags (XEEN_DS_VIEW_WALLS), decoded from 1C9D4's 44 jump tables: each slot's
 * draw position, and the face index of the 20 slots that are faces (NO_FACE for the side walls).
 */
#define NO_FACE 0xffu
static const uint8_t view_position[XEEN_VIEW_SLOTS] = {25, 28, 27, 29, 26, 20, 23, 22, 24, 21, 11, 16, 12, 17, 15,
                                                       19, 14, 18, 13, 34, 32, 0,  30, 1,  8,  2,  7,  6,  9,  5,
                                                       10, 4,  31, 3,  33, 35, 39, 38, 37, 36, 40, 41, 42, 43};
static const uint8_t view_face[XEEN_VIEW_SLOTS] = {
        17, NO_FACE, 19, NO_FACE, 18, 14, NO_FACE, 16, NO_FACE, 15, 9, NO_FACE, 10, NO_FACE, 13,
        NO_FACE, 12, NO_FACE, 11, 0, NO_FACE, 1, NO_FACE, 2, NO_FACE, 3, NO_FACE, 8, NO_FACE, 7,
        NO_FACE, 6, NO_FACE, 4, NO_FACE, 5, NO_FACE, NO_FACE, NO_FACE, NO_FACE, NO_FACE, NO_FACE, NO_FACE, NO_FACE};
/*
 * Returns the wall flag that view SLOT's wall of KIND (1..15) counts into: kind 2 into XEEN_DS_VIEW_WALL_2; a side
 * wall, or a face of kind 5 or 8, into XEEN_DS_VIEW_WALL; any other face into its kind's face array.
 */
static uint16_t view_wall_flag(unsigned slot, unsigned kind)
{
    if (kind == 2u) {
        return XEEN_DS_VIEW_WALL_2(view_position[slot]);
    }
    if (view_face[slot] == NO_FACE || kind == 5u || kind == 8u) {
        return XEEN_DS_VIEW_WALL(view_position[slot]);
    }
    return XEEN_DS_VIEW_FACE(kind, view_face[slot]);
}
xeen_caller_result xeen_scene_scan_view_walls(xeen_movement *m)
{
    uint16_t index, kind, address;
    if (!m || xeen_read16(m, XEEN_DS_OUTDOORS)) {
        xeen_caller_result r = {XEEN_BAD_INPUT, XEEN_SCENE_VIEW_WALL_SCAN};
        return r;
    }
    memset(m->ds + XEEN_DS_VIEW_WALLS, 0, XEEN_VIEW_WALLS_BYTES);
    for (index = 0; index < XEEN_VIEW_SLOTS; index++) {
        if (xeen_navigation_query(m, index, &kind) != XEEN_OK) {
            xeen_caller_result r = {m->status, XEEN_NAVIGATION_QUERY};
            return r;
        }
        if (kind >= 1 && kind <= 15) {
            address = view_wall_flag(index, kind);
            m->ds[address] = (uint8_t)(m->ds[address] + 1u);
        }
        /* 1C952 keeps its order relative to query 2. */
        if (index == 2 && xeen_scene_light(m) != XEEN_OK) {
            xeen_caller_result r = {m->status, XEEN_METADATA_QUERY};
            return r;
        }
        if (index == 7) {
            m->ds[XEEN_DS_SPOT_DOORS_LIT] = (uint8_t)(xeen_read16(m, XEEN_DS_CELL_WALLS) != XEEN_CELL_OUTSIDE_MAPS &&
                                                      m->ds[XEEN_DS_VIEW_WALL(XEEN_VIEW_POSITION_AHEAD)] != 0);
        }
    }
    /* Genuine helper return to caller. Its subsequent combat sprite/entity
     * work is not a rendering service acknowledgement or command readiness. */
    return boundary(m, XEEN_SCENE_INDOOR_EFFECTS);
}

/* Copies SIZE bytes at AT of the original's initialized DS (the compiled program data) into M's DS. */
static void copy_program_data(xeen_movement *m, uint16_t at, size_t size)
{
    memcpy(m->ds + at, xeen_program_ds_image + at, size);
}
/* The indoor hit effects' tables (initialized data, from the program data before it is loaded whole, as
 * xeen_scene_init_monster_tables): hit_effect_records, hit_effect_x and hit_element_x_offset, 12 words. */
xeen_status xeen_scene_init_effect_tables(xeen_movement *m)
{
    if (!m) {
        return XEEN_BAD_INPUT;
    }
    copy_program_data(m, XEEN_DS_HIT_EFFECT_RECORDS, 2u * (3u + 6u + 3u)); /* 4D5C..4D73 */
    return XEEN_OK;
}
/*
 * The hit effects drawn over the party cell's three monster slots: hit slot I of the XEEN_DS_HIT_* arrays is view
 * slot I (XEEN_DS_VIEW_MONSTER(I)). Its two effect records follow the slot monster's record in the view list: the
 * blow (the charpow sprite, frame 0) and, in the record after it, the element effect. The table word at
 * RECORDS + 2 I holds the blow record's number (the program data: 157, 151, 154 indoors after the monster records 156,
 * 150, 153; 119, 113, 116 outdoors), LIST the view list. Original: indoors 1BBE4..1BDD5 (table 4D5C, list 0962),
 * outdoors 1B7DA..1B9AC (table 4D76, list 1166), the same code apart from the addresses.
 */
typedef struct {
    uint16_t records, list;
} hit_effects;
enum hit_effect { HIT_BLOW = 0, HIT_ELEMENT_EFFECT = 1 }; /* the record after the table's */
/*
 * The DS offset of FIELD (XEEN_VIEW_RECORD_*) of slot SLOT's EFFECT record. The record is located from the table word
 * at every access, as the original does (the word is read again before each read and write): a write to an effect
 * record can land on the table word itself (the record number is any word; the address wraps in 16 bits), and the
 * accesses after it then go to the new record. Reads between two writes may come in any order.
 */
static uint16_t hit_field(const xeen_movement *m, const hit_effects *hits, unsigned slot, enum hit_effect effect,
                          unsigned field)
{
    uint16_t number = (uint16_t)(xeen_read16(m, (uint16_t)(hits->records + 2u * slot)) + (unsigned)effect);
    return (uint16_t)(hits->list + (uint16_t)(number * XEEN_VIEW_RECORD_BYTES) + field);
}
static void set_hit_field(xeen_movement *m, const hit_effects *hits, unsigned slot, enum hit_effect effect,
                          unsigned field, uint16_t value)
{
    xeen_write16(m, hit_field(m, hits, slot, effect, field), value);
}
/* The original's signed 16-bit VALUE divided by 3 (IDIV, truncating towards zero). */
static uint16_t effect_third(uint16_t value)
{
    int32_t signed_value = xeen_sign16(value);
    return (uint16_t)(signed_value / 3);
}
/* An effect drawn with the enlarge scale (XEEN_SCALE_ENLARGE, a hit of 100 or more) takes a third of its x: the
 * record's x, read again as stored. */
static void third_of_x(xeen_movement *m, const hit_effects *hits, unsigned slot, enum hit_effect effect)
{
    set_hit_field(m, hits, slot, effect, XEEN_VIEW_RECORD_X,
                  effect_third(xeen_read16(m, hit_field(m, hits, slot, effect, XEEN_VIEW_RECORD_X))));
}
/* The x of slot SLOT's hits (hit_effect_x: two per slot, LAYOUT 1 for the pair layout, effects()). */
static uint16_t hit_x(const xeen_movement *m, unsigned slot, unsigned layout)
{
    return xeen_read16(m, (uint16_t)(XEEN_DS_HIT_EFFECT_X + 4u * slot + 2u * layout));
}
/* The blow: x, frame 0, the damage's scale, then the enlarged blow's third of x and y 60, any other y 73; the clip
 * flags; the charpow sprite last. The enlarge test reads the scale word again, as the original does (1BCAC); the only
 * write in between stores that word's own value (even addresses: no partial overlap), so the second read cannot
 * differ. */
static void draw_blow(xeen_movement *m, const hit_effects *hits, unsigned slot, unsigned layout)
{
    uint16_t scale = (uint16_t)(XEEN_DS_HIT_DAMAGE_SCALE + 2u * slot);
    set_hit_field(m, hits, slot, HIT_BLOW, XEEN_VIEW_RECORD_X, hit_x(m, slot, layout));
    set_hit_field(m, hits, slot, HIT_BLOW, XEEN_VIEW_RECORD_FRAME, 0);
    set_hit_field(m, hits, slot, HIT_BLOW, XEEN_VIEW_RECORD_SCALE, xeen_read16(m, scale));
    if (xeen_read16(m, scale) == XEEN_HIT_SCALE_100) {
        third_of_x(m, hits, slot, HIT_BLOW);
        set_hit_field(m, hits, slot, HIT_BLOW, XEEN_VIEW_RECORD_Y, 60);
    } else {
        set_hit_field(m, hits, slot, HIT_BLOW, XEEN_VIEW_RECORD_Y, 73);
    }
    set_hit_field(m, hits, slot, HIT_BLOW, XEEN_VIEW_RECORD_FLAGS, XEEN_DRAW_CLIP_ROWS | XEEN_DRAW_CLIP_COLUMNS);
    set_hit_field(m, hits, slot, HIT_BLOW, XEEN_VIEW_RECORD_HANDLE, xeen_read16(m, XEEN_DS_CHARPOW_SPRITES));
}
/* The element effect: the blow's x plus the slot's offset (hit_element_x_offset), the element as the frame, the
 * element's scale (read again for the enlarge test, 1BD72, as for the blow), the enlarged effect's third of x (its y
 * is left as it was); the clip flags; the charpow sprite last. */
static void draw_element(xeen_movement *m, const hit_effects *hits, unsigned slot, unsigned layout)
{
    uint16_t scale = (uint16_t)(XEEN_DS_HIT_ELEMENT_SCALE + 2u * slot);
    set_hit_field(m, hits, slot, HIT_ELEMENT_EFFECT, XEEN_VIEW_RECORD_X,
                  (uint16_t)(hit_x(m, slot, layout) +
                             xeen_read16(m, (uint16_t)(XEEN_DS_HIT_ELEMENT_X_OFFSET + 2u * slot))));
    set_hit_field(m, hits, slot, HIT_ELEMENT_EFFECT, XEEN_VIEW_RECORD_FRAME, m->ds[XEEN_DS_HIT_ELEMENT + slot]);
    set_hit_field(m, hits, slot, HIT_ELEMENT_EFFECT, XEEN_VIEW_RECORD_SCALE, xeen_read16(m, scale));
    if (xeen_read16(m, scale) == XEEN_HIT_SCALE_100) {
        third_of_x(m, hits, slot, HIT_ELEMENT_EFFECT);
    }
    set_hit_field(m, hits, slot, HIT_ELEMENT_EFFECT, XEEN_VIEW_RECORD_FLAGS,
                  XEEN_DRAW_CLIP_ROWS | XEEN_DRAW_CLIP_COLUMNS);
    set_hit_field(m, hits, slot, HIT_ELEMENT_EFFECT, XEEN_VIEW_RECORD_HANDLE, xeen_read16(m, XEEN_DS_CHARPOW_SPRITES));
}
/*
 * One effects pass over hit slots 0..2, each in turn: both effect records are hidden first (the element's, then the
 * blow's: FFFFh in the handle word); while the slot's hit lasts (HIT_FRAMES nonzero) one frame of it is used up and
 * the blow (while HIT_PHYSICAL) and the element (while HIT_ELEMENT) are drawn; each of those bytes is read when it is
 * tested, after the writes before it.
 *
 * The layout: the hit x has a second set for a party cell with two monsters, chosen when view slot 1 is nonzero and
 * view slot 2 was zero at the pass's start (read once, before any write; slot 1 again for each hit). The test reads
 * the previous scene's selectors with 0, not FFh, as the empty value (1BC46 / 1B81D): like the click areas' 18E0E,
 * apparently holds_pair's test with the wrong empty value. An original quirk, kept; the intent is inferred.
 */
static void effects(xeen_movement *m, const hit_effects *hits)
{
    uint8_t slot_2_at_start = m->ds[XEEN_DS_VIEW_MONSTER(2)];
    unsigned slot;
    for (slot = 0; slot < 3; slot++) {
        unsigned layout;
        set_hit_field(m, hits, slot, HIT_ELEMENT_EFFECT, XEEN_VIEW_RECORD_HANDLE, XEEN_VIEW_RECORD_UNUSED);
        set_hit_field(m, hits, slot, HIT_BLOW, XEEN_VIEW_RECORD_HANDLE, XEEN_VIEW_RECORD_UNUSED);
        if (!m->ds[XEEN_DS_HIT_FRAMES + slot]) {
            continue;
        }
        layout = m->ds[XEEN_DS_VIEW_MONSTER(1)] != 0 && slot_2_at_start == 0;
        --m->ds[XEEN_DS_HIT_FRAMES + slot];
        if (m->ds[XEEN_DS_HIT_PHYSICAL + slot]) {
            draw_blow(m, hits, slot, layout);
        }
        if (m->ds[XEEN_DS_HIT_ELEMENT + slot]) {
            draw_element(m, hits, slot, layout);
        }
    }
}
xeen_caller_result xeen_scene_prepare_effects(xeen_movement *m)
{
    static const hit_effects indoor = {XEEN_DS_HIT_EFFECT_RECORDS, XEEN_DS_INDOOR_VIEW_LIST};
    if (!m) {
        xeen_caller_result r = {XEEN_BAD_INPUT, XEEN_SCENE_INDOOR_EFFECTS};
        return r;
    }
    m->ds[XEEN_DS_TURNED] = 0;
    effects(m, &indoor);
    return boundary(m, XEEN_SCENE_INDOOR_MONSTERS);
}
xeen_caller_result xeen_scene_outdoor_effects(xeen_movement *m)
{
    static const hit_effects outdoor = {XEEN_DS_OUTDOOR_HIT_EFFECT_RECORDS, XEEN_DS_OUTDOOR_VIEW_LIST};
    if (!m || !xeen_read16(m, XEEN_DS_OUTDOORS)) {
        xeen_caller_result r = {XEEN_BAD_INPUT, XEEN_SCENE_OUTDOOR_EFFECTS};
        return r;
    }
    effects(m, &outdoor);
    return boundary(m, XEEN_SCENE_OUTDOOR_MONSTERS);
}

/*
 * The indoor view's twelve cells where monsters, objects and wall items are drawn, named VIEW_CELL_<ahead>_<side> like
 * the wall positions (XEEN_VIEW_POS_*): AHEAD rows in front of the party (0 its own row), in column SIDE (C the
 * party's, L1 / R1 one to the left / right, L2 / R2 two). A cell's value is its step entry minus 2: the cell lies at
 * XEEN_DS_STEP_X/Y(facing, cell + 2) from the party (the program data's movement_offset_x/_y, read from 0650/0710;
 * Phase 257). The scans visit them in monster_views' order.
 */
enum view_cell {
    VIEW_CELL_0_C = 0, /* the party's own cell; its three monster slots are the combat's attackers */
    VIEW_CELL_1_L1 = 3,
    VIEW_CELL_1_C = 5,
    VIEW_CELL_1_R1 = 7,
    VIEW_CELL_2_L1 = 10,
    VIEW_CELL_2_C = 12,
    VIEW_CELL_2_R1 = 14,
    VIEW_CELL_3_L2 = 21,
    VIEW_CELL_3_L1 = 23,
    VIEW_CELL_3_C = 25,
    VIEW_CELL_3_R1 = 27,
    VIEW_CELL_3_R2 = 29
};
/*
 * The view cells where monsters are drawn, in the order both monster scans visit them (indoors 1ED46, outdoors
 * 2126C: the same twelve cells with the same slots). A cell has one to three drawing slots; slot K has a selector
 * byte (XEEN_DS_VIEW_MONSTERS + selectors[K], the map monster drawn there or FFh), a record in each view list
 * (indoor[K], outdoor[K]) and the record's x, X[K]. A cell of three slots that holds exactly two monsters draws its
 * first two at PAIR_X instead (holds_pair). A monster's DEPTH, its cell's row, picks its y indoors
 * (XEEN_DS_MONSTER_VIEW_Y). The x values are the original's immediates: 1FEF3..1FFFA adds them, 21721..2185F stores
 * the same values (the outdoor records' x starts from them, the indoor ones from the flier's bob).
 */
typedef struct {
    uint8_t cell, depth, count, selectors[3], indoor[3], outdoor[3];
    int8_t x[3], pair_x[2];
} monster_view;
static const monster_view monster_views[12] = {
        {VIEW_CELL_0_C, 0, 3, {0, 1, 2}, {156, 150, 153}, {118, 112, 115}, {-5, -67, 58}, {31, -36}},
        {VIEW_CELL_1_C, 1, 3, {3, 4, 5}, {132, 130, 131}, {94, 92, 93}, {-7, -38, 25}, {8, -23}},
        {VIEW_CELL_1_L1, 1, 1, {12}, {128}, {90}, {-112}, {0}},
        {VIEW_CELL_1_R1, 1, 1, {13}, {129}, {91}, {98}, {0}},
        {VIEW_CELL_2_C, 2, 3, {6, 7, 8}, {106, 104, 105}, {75, 73, 74}, {-8, -24, 9}, {0, -16}},
        {VIEW_CELL_2_L1, 2, 2, {14, 20}, {100, 101}, {69, 70}, {-65, -85}, {0}},
        {VIEW_CELL_2_R1, 2, 2, {15, 21}, {102, 103}, {71, 72}, {49, 65}, {0}},
        {VIEW_CELL_3_C, 3, 3, {9, 10, 11}, {70, 68, 69}, {52, 50, 51}, {-9, -17, -1}, {-5, -13}},
        {VIEW_CELL_3_L1, 3, 3, {16, 22, 24}, {62, 60, 61}, {44, 42, 43}, {-34, -41, -26}, {-27, -37}},
        {VIEW_CELL_3_L2, 3, 1, {18}, {66}, {48}, {-58}, {0}},
        {VIEW_CELL_3_R1, 3, 3, {17, 23, 25}, {65, 63, 64}, {47, 45, 46}, {16, -16, 23}, {20, -12}},
        {VIEW_CELL_3_R2, 3, 1, {19}, {67}, {49}, {40}, {0}}};
/* The draw positions by place (ds_globals.h XEEN_VIEW_POS_*): FACE(ahead, side) the wall across that cell on its far
 * side, LEFT and RIGHT its side walls. */
#define FACE(ahead, side)  XEEN_VIEW_POS_FACE(ahead, side)
#define LEFT(ahead, side)  XEEN_VIEW_POS_LEFT(ahead, side)
#define RIGHT(ahead, side) XEEN_VIEW_POS_RIGHT(ahead, side)

/*
 * Occlusion. The scans do not trace sight lines: a cell is hidden when the walls the view scan counted
 * (XEEN_DS_VIEW_WALL: a side wall of any kind but 2, a face of kind 5 or 8; xeen_scene_scan_view_walls) close every
 * way to it that the original considers. The way to a cell runs up the party's column and, for a side cell, steps
 * diagonally into the side column at the end, round one corner.
 */
/* Whether the view scan counted a wall at draw position POSITION. */
static int wall_at(const xeen_movement *m, unsigned position)
{
    return m->ds[XEEN_DS_VIEW_WALL(position)] != 0;
}
/* Whether a wall across the party's own column closes it before row AHEAD (0..3; 0 never). */
static int column_closed(const xeen_movement *m, unsigned ahead)
{
    static const uint8_t faces[3] = {FACE(0, C), FACE(1, C), FACE(2, C)};
    unsigned row;
    for (row = 0; row < ahead; row++) {
        if (wall_at(m, faces[row])) {
            return 1;
        }
    }
    return 0;
}
/*
 * The diagonal step from the column cell (C, a) to the side cell (L1 or R1, a + 1), by its four walls. It goes round
 * the corner by one of two routes: aside first (through NEAR_SIDE, the side wall of (C, a), then FAR_FACE, the face of
 * the side cell (L1/R1, a)), or ahead first (through NEAR_FACE, the face of (C, a), then FAR_SIDE, the side wall of
 * (C, a + 1)). FAR_FACE and FAR_SIDE are the target cell's own two walls towards the party.
 */
typedef struct {
    uint8_t near_side, far_face, near_face, far_side;
} corner;
static const corner left_corners[3] = {{LEFT(0, C), FACE(0, L1), FACE(0, C), LEFT(1, C)},
                                       {LEFT(1, C), FACE(1, L1), FACE(1, C), LEFT(2, C)},
                                       {LEFT(2, C), FACE(2, L1), FACE(2, C), LEFT(3, C)}};
static const corner right_corners[3] = {{RIGHT(0, C), FACE(0, R1), FACE(0, C), RIGHT(1, C)},
                                        {RIGHT(1, C), FACE(1, R1), FACE(1, C), RIGHT(2, C)},
                                        {RIGHT(2, C), FACE(2, R1), FACE(2, C), RIGHT(3, C)}};
/* How a corner test reads the target cell's own two walls together. */
enum pair_test {
    PAIR_AS_WALLS,   /* two walls (both counted): the objects' and the wall items' scans, and every corner but the first */
    PAIR_AS_BYTE_AND /* the AND of the two counts: the monster scan's first-row corners (1ED46: 1F187, 1F26C) */
};
/*
 * Whether corner C is closed: both of its routes are walled. The original tests the four pairs that take one wall from
 * each route; with PAIR_AS_BYTE_AND it tests the pair of the target's own walls by ANDing their counts, so two
 * nonzero counts with no common bit leave that pair open and only a near wall closes the corner. The view scan's
 * counts are 0 or 1 (each position takes at most one slot's wall), so only a constructed state reaches that case.
 */
static int corner_closed(const xeen_movement *m, const corner *c, enum pair_test test)
{
    if (test == PAIR_AS_BYTE_AND && wall_at(m, c->far_face) && wall_at(m, c->far_side) &&
        !(m->ds[XEEN_DS_VIEW_WALL(c->far_face)] & m->ds[XEEN_DS_VIEW_WALL(c->far_side)])) {
        return wall_at(m, c->near_face) || wall_at(m, c->near_side);
    }
    return (wall_at(m, c->near_side) || wall_at(m, c->far_face)) &&
           (wall_at(m, c->near_face) || wall_at(m, c->far_side));
}
/* Whether the side cell (L1/R1, AHEAD) is hidden: the party's column is closed before row AHEAD - 1, or the corner
 * from (C, AHEAD - 1) into the side column is (CORNERS by row, TEST its far pair's reading). */
static int side_cell_hidden(const xeen_movement *m, const corner corners[3], unsigned ahead, enum pair_test test)
{
    return column_closed(m, ahead - 1u) || corner_closed(m, &corners[ahead - 1u], test);
}
/**
 * Returns nonzero when walls hide view cell CELL (an enum view_cell) from the party: the shared test of the monster
 * (1ED46), object (20011) and wall item (208FB) scans; TEST is PAIR_AS_BYTE_AND for the monster scan. The party's own
 * cell is never hidden. A column cell is hidden by a face nearer in the column; a side cell (L1/R1) by the column
 * before its row or by its corner. The cells two columns out, (3, L2) and (3, R2), are reached from (2, L1/R1): that
 * cell's test, then the face of (2, L1/R1) and the side wall of (3, L1/R1) towards them. Any other cell is hidden.
 */
static int view_cell_hidden(const xeen_movement *m, unsigned cell, enum pair_test test)
{
    switch (cell) {
        case VIEW_CELL_0_C:
            return 0;
        case VIEW_CELL_1_C:
            return column_closed(m, 1);
        case VIEW_CELL_2_C:
            return column_closed(m, 2);
        case VIEW_CELL_3_C:
            return column_closed(m, 3);
        case VIEW_CELL_1_L1:
            return side_cell_hidden(m, left_corners, 1, test);
        case VIEW_CELL_1_R1:
            return side_cell_hidden(m, right_corners, 1, test);
        case VIEW_CELL_2_L1:
            return side_cell_hidden(m, left_corners, 2, PAIR_AS_WALLS);
        case VIEW_CELL_2_R1:
            return side_cell_hidden(m, right_corners, 2, PAIR_AS_WALLS);
        case VIEW_CELL_3_L1:
            return side_cell_hidden(m, left_corners, 3, PAIR_AS_WALLS);
        case VIEW_CELL_3_R1:
            return side_cell_hidden(m, right_corners, 3, PAIR_AS_WALLS);
        case VIEW_CELL_3_L2:
            return side_cell_hidden(m, left_corners, 2, PAIR_AS_WALLS) || wall_at(m, FACE(2, L1)) ||
                   wall_at(m, LEFT(3, L1));
        case VIEW_CELL_3_R2:
            return side_cell_hidden(m, right_corners, 2, PAIR_AS_WALLS) || wall_at(m, FACE(2, R1)) ||
                   wall_at(m, RIGHT(3, R1));
        default:
            return 1;
    }
}
/** Adds DELTA to the DS word at A (a monster draw position). */
static void monster_shift(xeen_movement *m, uint16_t a, int delta)
{
    xeen_write16(m, a, (uint16_t)(xeen_read16(m, a) + delta));
}
/* Whether the record at ENTITY (a map monster, an object or a wall item: signed x at +0, y at +1) stands in view
 * cell CELL of the party at (PX, PY) facing FACING: the cells' x and y steps at DS 0650 and 0710 (the program data's
 * movement_offset_x/_y tables from 064E/070E), 48 bytes a facing. */
static int at_view_cell(const xeen_movement *m, uint16_t entity, int px, int py, uint16_t facing, unsigned cell)
{
    uint16_t offset = (uint16_t)(facing * 48u + cell);
    return (uint16_t)xeen_signed_byte(m->ds[entity + 1u]) ==
                   (uint16_t)(py + xeen_signed_byte(m->ds[(uint16_t)(XEEN_DS_STEP_Y(0u, 2u) + offset)])) &&
           (uint16_t)xeen_signed_byte(m->ds[entity]) ==
                   (uint16_t)(px + xeen_signed_byte(m->ds[(uint16_t)(XEEN_DS_STEP_X(0u, 2u) + offset)]));
}
/* The selector byte of slot K of VIEW. */
static uint16_t slot_selector(const monster_view *view, unsigned k)
{
    return (uint16_t)(XEEN_DS_VIEW_MONSTERS + view->selectors[k]);
}
/* The first free slot of VIEW (selector FFh; the slots fill in order), or -1 when every slot is taken. */
static int free_slot(const xeen_movement *m, const monster_view *view)
{
    unsigned k;
    for (k = 0; k < view->count; k++) {
        if (m->ds[slot_selector(view, k)] == XEEN_VIEW_NO_MONSTER) {
            return (int)k;
        }
    }
    return -1;
}
/* Whether VIEW is a cell of three slots holding exactly two monsters: its second slot taken, its third free. */
static int holds_pair(const xeen_movement *m, const monster_view *view)
{
    return view->count == 3u && m->ds[slot_selector(view, 1)] != XEEN_VIEW_NO_MONSTER &&
           m->ds[slot_selector(view, 2)] == XEEN_VIEW_NO_MONSTER;
}
/*
 * After the scan, every monster record of LIST gets its slot's x (monster_views), whether or not the slot is taken:
 * indoors added to the record's x (1FEF3), outdoors stored (21721). Each record is its own word and the pair test
 * reads only selector bytes, so the order of the 26 writes is not observable; the original writes the 14 records
 * without a pair rule first.
 */
static void place_monster_records(xeen_movement *m, enum view_list list)
{
    unsigned v, k;
    for (v = 0; v < 12; v++) {
        const monster_view *view = &monster_views[v];
        int pair = holds_pair(m, view);
        for (k = 0; k < view->count; k++) {
            int x = pair && k < 2u ? view->pair_x[k] : view->x[k];
            if (list == INDOOR_LIST) {
                monster_shift(m, (uint16_t)(view_record(list, view->indoor[k]) + XEEN_VIEW_RECORD_X), x);
            } else {
                xeen_write16(m, (uint16_t)(view_record(list, view->outdoor[k]) + XEEN_VIEW_RECORD_X), (uint16_t)x);
            }
        }
    }
}
/* The sprite that draws map monster ENTITY and, in *FRAME, its frame there: frames 0..7 are in the monster's own
 * sprite (XEEN_DS_MONSTER_SPRITES by its sprite slot), frames 8 and above in its attack sprite, counted from 0.
 * Indoors (2079D) and outdoors (2126C) alike. */
static uint16_t monster_sprite(const xeen_movement *m, uint16_t entity, uint16_t *frame)
{
    uint16_t slot = m->ds[entity + XEEN_MAP_MONSTER_SPRITE_SLOT];
    *frame = m->ds[entity + XEEN_MAP_MONSTER_FRAME];
    if (*frame < 8u) {
        return xeen_read16(m, (uint16_t)(XEEN_DS_MONSTER_SPRITES + 2u * slot));
    }
    *frame = (uint16_t)(*frame - 8u);
    return xeen_read16(m, (uint16_t)(XEEN_DS_MONSTER_ATTACK_SPRITES + 2u * slot));
}
/* A monster's view record keeps only the top four bits of its draw flags (the scene's own); add_effect_flags then
 * adds the monster's current effect: while map monster ENTITY has an effect (a word, 0 none), the flags word of that
 * effect's current step (the 16-byte rows of XEEN_DS_MONSTER_EFFECT_FLAGS, a word per step; the address wraps in
 * 16 bits). */
static void clear_monster_flags(xeen_movement *m, uint16_t record)
{
    xeen_write16(m, (uint16_t)(record + XEEN_VIEW_RECORD_FLAGS),
                 (uint16_t)(xeen_read16(m, (uint16_t)(record + XEEN_VIEW_RECORD_FLAGS)) & 0xf000u));
}
static void add_effect_flags(xeen_movement *m, uint16_t record, uint16_t entity)
{
    uint16_t effect = xeen_read16(m, (uint16_t)(entity + XEEN_MAP_MONSTER_EFFECT));
    if (effect) {
        uint16_t step = xeen_read16(m, (uint16_t)(entity + XEEN_MAP_MONSTER_EFFECT_STEP));
        xeen_write16(m, (uint16_t)(record + XEEN_VIEW_RECORD_FLAGS),
                     (uint16_t)(xeen_read16(m, (uint16_t)(record + XEEN_VIEW_RECORD_FLAGS)) |
                                xeen_read16(m, (uint16_t)(XEEN_DS_MONSTER_EFFECT_FLAGS + 16u * effect + 2u * step))));
    }
}
/* The host's guard (not the original's): every map monster's record lies inside the monster database of SIZE
 * bytes, checked before anything is written (a missing row is not fabricated). */
static int map_monsters_in_database(const xeen_movement *m, size_t size)
{
    unsigned i;
    for (i = 0; i < m->ds[XEEN_DS_MAP_MONSTER_COUNT]; i++) {
        if ((size_t)m->ds[XEEN_MAP_MONSTER(i) + XEEN_MAP_MONSTER_ID] * XEEN_MONSTER_RECORD_BYTES +
                    XEEN_MONSTER_RECORD_BYTES >
            size) {
            return 0;
        }
    }
    return 1;
}
/* Indoor view record RECORD draws map monster ENTITY (SPRITE, FRAME) at DEPTH: its y by depth; x 0, or for a flier
 * the bob by the scene phase (x set, y moved); then the flags (clear_monster_flags, add_effect_flags). */
static void fill_indoor_record(xeen_movement *m, uint16_t record, uint16_t entity, uint16_t sprite, uint16_t frame,
                               int flying, unsigned depth)
{
    xeen_write16(m, (uint16_t)(record + XEEN_VIEW_RECORD_FRAME), frame);
    xeen_write16(m, record, sprite);
    xeen_write16(m, (uint16_t)(record + XEEN_VIEW_RECORD_Y),
                 (uint16_t)xeen_signed_byte(m->ds[XEEN_DS_MONSTER_VIEW_Y + depth]));
    if (flying) {
        uint16_t phase = m->ds[XEEN_DS_SCENE_PHASE];
        xeen_write16(m, (uint16_t)(record + XEEN_VIEW_RECORD_X),
                     (uint16_t)xeen_signed_byte(m->ds[(uint16_t)(XEEN_DS_MONSTER_BOB_X + phase)]));
        monster_shift(m, (uint16_t)(record + XEEN_VIEW_RECORD_Y),
                      xeen_signed_byte(m->ds[(uint16_t)(XEEN_DS_MONSTER_BOB_Y + phase)]));
    } else {
        xeen_write16(m, (uint16_t)(record + XEEN_VIEW_RECORD_X), 0);
    }
    clear_monster_flags(m, record);
    add_effect_flags(m, record, entity);
}
/* Whether a monster in view cell CELL becomes active even while walls hide it: the party's column up to two rows
 * ahead (1ED46 sets the byte before its occlusion test for these three cells). */
static int wakes_unseen(unsigned cell)
{
    return cell == VIEW_CELL_0_C || cell == VIEW_CELL_1_C || cell == VIEW_CELL_2_C;
}
/* The indoor view's monsters: every map monster standing in one of the twelve view cells becomes active and, unless
 * walls hide the cell (view_cell_hidden, the byte-AND reading), takes the cell's first free slot (its selector
 * becomes the monster's number) and fills that slot's record. A monster in the party's column up to two rows ahead
 * becomes active even when hidden (wakes_unseen). One monster may stand in several cells' views. Then the records'
 * x positions (place_monster_records). Refused, DS unchanged, when a monster's database row is missing (the host's
 * guard). Original: 1ED46. */
xeen_status xeen_scene_monsters(xeen_movement *m, const uint8_t *database, size_t size)
{
    unsigned i, v;
    int px, py;
    uint16_t facing;
    if (!m || !database) {
        return XEEN_BAD_INPUT;
    }
    /* Validate before the original selector clear: host bounds checks are not
     * permission to fabricate a missing FF database record. */
    if (!map_monsters_in_database(m, size)) {
        return XEEN_SERVICE_REQUIRED;
    }
    px = xeen_signed_byte(m->ds[XEEN_DS_PARTY_X]);
    py = xeen_signed_byte(m->ds[XEEN_DS_PARTY_Y]);
    facing = m->ds[XEEN_DS_PARTY_DIRECTION];
    memset(m->ds + XEEN_DS_VIEW_MONSTERS, XEEN_VIEW_NO_MONSTER, XEEN_VIEW_MONSTER_SLOTS);
    for (i = 0; i < m->ds[XEEN_DS_MAP_MONSTER_COUNT]; i++) {
        uint16_t entity = (uint16_t)XEEN_MAP_MONSTER(i), frame, sprite = monster_sprite(m, entity, &frame);
        /* a flier bobs (the 3054/305C offsets by the scene phase) */
        uint8_t flying =
                database[(size_t)m->ds[entity + XEEN_MAP_MONSTER_ID] * XEEN_MONSTER_RECORD_BYTES + XEEN_MONSTER_FLYING];
        for (v = 0; v < 12; v++) {
            const monster_view *view = &monster_views[v];
            int k;
            if (!at_view_cell(m, entity, px, py, facing, view->cell)) {
                continue;
            }
            if (wakes_unseen(view->cell)) {
                m->ds[entity + XEEN_MAP_MONSTER_ACTIVE] = 1;
            }
            if (view_cell_hidden(m, view->cell, PAIR_AS_BYTE_AND)) {
                continue;
            }
            m->ds[entity + XEEN_MAP_MONSTER_ACTIVE] = 1;
            k = free_slot(m, view);
            if (k >= 0) {
                m->ds[slot_selector(view, (unsigned)k)] = (uint8_t)i;
                fill_indoor_record(m, view_record(INDOOR_LIST, view->indoor[k]), entity, sprite, frame, flying,
                                   view->depth);
            }
        }
    }
    place_monster_records(m, INDOOR_LIST);
    m->status = XEEN_OK;
    return XEEN_OK;
}

/*
 * The party cell's monster groups: view slot K (XEEN_DS_VIEW_MONSTER(K)) draws its monster in a group of three
 * consecutive records, the monster's and its two hit effects' (effects()), from its record in monster_views' first cell
 * (indoors XEEN_DS_PARTY_CELL_MONSTER_RECORDS). The shuffle moves the first attacking group to the three records
 * from FRONT_GROUP[list] (indoors XEEN_DS_FRONT_MONSTER_RECORDS), which are drawn after them, and the restore moves
 * it back.
 */
static const uint8_t front_group[2] = {159, 121};
static unsigned party_cell_group(enum view_list list, unsigned slot)
{
    const monster_view *cell = &monster_views[0];
    return list == OUTDOOR_LIST ? cell->outdoor[slot] : cell->indoor[slot];
}
/* Copies the group of three records from record FROM to record TO of LIST, record by record, word by word. */
static void copy_group(xeen_movement *m, enum view_list list, unsigned to, unsigned from)
{
    unsigned record;
    for (record = 0; record < 3; record++) {
        copy_scene_record(m, view_record(list, to + record), view_record(list, from + record));
    }
}
/* Hides the group of three records from record FIRST of LIST: FFFFh in each handle word, the last record first.
 * (The indoor original hides the party cell's group in the order 2, 0, 1: three constant stores to distinct words
 * with no read between them, so the order is not observable.) */
static void hide_group(xeen_movement *m, enum view_list list, unsigned first)
{
    xeen_write16(m, view_record(list, first + 2u), XEEN_VIEW_RECORD_UNUSED);
    xeen_write16(m, view_record(list, first + 1u), XEEN_VIEW_RECORD_UNUSED);
    xeen_write16(m, view_record(list, first), XEEN_VIEW_RECORD_UNUSED);
}
/*
 * The shuffle before the frame is presented: the front group is hidden; then the first party-cell slot whose
 * monster attacks or is hit (its frame 8 or more; an empty slot's FFh indexes past the map monsters and reads
 * whatever is there) has its group moved to the front group and hidden in place. *SELECTED is that slot + 1, or 0.
 * Original: indoors 1BDE4..1BECF, outdoors 1B9B6..1BA98.
 */
static void shuffle(xeen_movement *m, enum view_list list, uint16_t *selected)
{
    unsigned slot;
    hide_group(m, list, front_group[list]);
    *selected = 0;
    for (slot = 0; slot < 3; slot++) {
        unsigned group = party_cell_group(list, slot);
        if (m->ds[(uint16_t)(XEEN_MAP_MONSTER(m->ds[XEEN_DS_VIEW_MONSTER(slot)]) + XEEN_MAP_MONSTER_FRAME)] < 8) {
            continue;
        }
        copy_group(m, list, front_group[list], group);
        hide_group(m, list, group);
        *selected = (uint16_t)(slot + 1u);
        break;
    }
}
/* The restore after the party scan: SELECTED's group (shuffle) comes back from the front group. Original: indoors
 * 1BED9.., outdoors 1BAA2..1BB25. */
static void restore(xeen_movement *m, enum view_list list, uint16_t selected)
{
    if (selected) {
        copy_group(m, list, party_cell_group(list, selected - 1u), front_group[list]);
    }
}
xeen_caller_result xeen_scene_shuffle(xeen_movement *m, uint16_t *selected)
{
    if (!m || !selected) {
        xeen_caller_result r = {XEEN_BAD_INPUT, XEEN_SCENE_INDOOR_SHUFFLE};
        return r;
    }
    shuffle(m, INDOOR_LIST, selected);
    return boundary(m, XEEN_SCENE_PRESENT);
}
xeen_caller_result xeen_scene_restore_group(xeen_movement *m, uint16_t selected)
{
    if (!m || selected > 3) {
        xeen_caller_result r = {XEEN_BAD_INPUT, XEEN_SCENE_INDOOR_RESTORE};
        return r;
    }
    restore(m, INDOOR_LIST, selected);
    return boundary(m, XEEN_SCENE_ANIMATE);
}
/*
 * The scene's monster and object tables, installed at the monster preparation, before the program data is loaded
 * whole (xeen_game_load_program_data, which requires these earlier writes to agree with it): the colour-effect flag
 * rows 0..15, the flying bob's x and y, the indoor monster y by depth and the map object x and y rows (object_view_x
 * then object_view_y). In the original these are simply initialized data; the copies keep a movement state prepared
 * without the whole image (the combat fixtures) holding them. Original initialized MZ DS bytes (the removed
 * scene_monster_tables.inc held them as literals); baseline SHA256
 * 88704c229ada806289dc4a513f77c88e1966fb8e3cf1d23652c748a7a7cd1687.
 */
xeen_status xeen_scene_init_monster_tables(xeen_movement *m)
{
    if (!m) {
        return XEEN_BAD_INPUT;
    }
    copy_program_data(m, XEEN_DS_MONSTER_EFFECT_FLAGS, 16u * 16u);
    copy_program_data(m, XEEN_DS_MONSTER_BOB_X, 2u * 8u);
    copy_program_data(m, XEEN_DS_MONSTER_VIEW_Y, 4u);
    copy_program_data(m, XEEN_DS_OBJECT_VIEW_X, 4u * XEEN_OBJECT_VIEW_ROW_BYTES);
    return XEEN_OK;
}

/* The objects' view (indoors 20011, outdoors 219E4), by view cell in monster_views' order: each cell's record in
 * each view list (by number) and its place in the object position rows (a word a place: the x row
 * XEEN_DS_OBJECT_VIEW_X indoors, XEEN_DS_OUTDOOR_OBJECT_VIEW_X outdoors; the y row XEEN_DS_OBJECT_VIEW_Y in both).
 * The places follow the records' order (row 3: C, L1, R1, L2, R2), not the scan's. */
typedef struct {
    uint8_t cell, place, indoor, outdoor;
} object_view;
static const object_view object_views[12] = {
        {VIEW_CELL_0_C, 0, 149, 111},  {VIEW_CELL_1_C, 1, 125, 87},  {VIEW_CELL_1_L1, 2, 126, 88},
        {VIEW_CELL_1_R1, 3, 127, 89},  {VIEW_CELL_2_C, 4, 97, 66},   {VIEW_CELL_2_L1, 5, 98, 67},
        {VIEW_CELL_2_R1, 6, 99, 68},   {VIEW_CELL_3_C, 7, 55, 37},   {VIEW_CELL_3_L1, 8, 56, 38},
        {VIEW_CELL_3_L2, 10, 58, 40},  {VIEW_CELL_3_R1, 9, 57, 39},  {VIEW_CELL_3_R2, 11, 59, 41}};
/* Whether map object sprite SPRITE takes the position rows' second set (XEEN_OBJECT_VIEW_ROW_BYTES further): one
 * sprite per world, Darkside's 2Fh and Clouds' 71h (indoors 20011 and outdoors 219E4 alike; its role is not
 * established). */
static int second_row_sprite(const xeen_movement *m, uint8_t sprite)
{
    return sprite == (m->ds[XEEN_DS_WORLD] ? 0x2fu : 0x71u);
}
/* Sets the mirror bit of view record RECORD's draw flags (XEEN_DRAW_MIRRORED, which the draw list passes on to the
 * renderer) from a map object's flip byte FLIP, keeping the other flags. */
static void set_mirrored(xeen_movement *m, uint16_t record, uint8_t flip)
{
    uint16_t flags = (uint16_t)(xeen_read16(m, (uint16_t)(record + XEEN_VIEW_RECORD_FLAGS)) & ~XEEN_DRAW_MIRRORED);
    xeen_write16(m, (uint16_t)(record + XEEN_VIEW_RECORD_FLAGS), (uint16_t)(flags | (flip ? XEEN_DRAW_MIRRORED : 0u)));
}
/*
 * The view's objects in LIST: every map object in a view cell fills the cell's record when it is still free (frame
 * FFFFh; the first such object wins); indoors only where walls do not hide the cell (view_cell_hidden), outdoors in
 * every cell. The object in the party's own cell becomes the event object (XEEN_DS_EVENT_OBJECT, its number + 1,
 * else 0). The party's place and facing are read once, before the first object (both originals; nothing here writes
 * them). Original: indoors 20011, outdoors 219E4.
 */
static void place_objects(xeen_movement *m, enum view_list list)
{
    uint16_t x_row = list == OUTDOOR_LIST ? XEEN_DS_OUTDOOR_OBJECT_VIEW_X : XEEN_DS_OBJECT_VIEW_X, facing;
    unsigned i, v, count;
    int px = xeen_signed_byte(m->ds[XEEN_DS_PARTY_X]), py = xeen_signed_byte(m->ds[XEEN_DS_PARTY_Y]);
    facing = m->ds[XEEN_DS_PARTY_DIRECTION];
    count = m->ds[XEEN_DS_MAP_OBJECT_COUNT];
    m->ds[XEEN_DS_EVENT_OBJECT] = 0;
    for (i = 0; i < count; i++) {
        uint16_t entity = (uint16_t)(XEEN_DS_MAP_OBJECTS + XEEN_MAP_OBJECT_BYTES * i);
        uint8_t flip = m->ds[entity + XEEN_MAP_OBJECT_FLIP], frame = m->ds[entity + XEEN_MAP_OBJECT_FRAME];
        uint16_t sprite =
                xeen_read16(m, (uint16_t)(XEEN_DS_OBJECT_SPRITES + 2u * m->ds[entity + XEEN_MAP_OBJECT_SLOT]));
        unsigned rows = second_row_sprite(m, m->ds[entity + XEEN_MAP_OBJECT_SPRITE]) ? XEEN_OBJECT_VIEW_ROW_BYTES : 0u;
        for (v = 0; v < 12; v++) {
            const object_view *view = &object_views[v];
            uint16_t record = view_record(list, list == OUTDOOR_LIST ? view->outdoor : view->indoor);
            if (!at_view_cell(m, entity, px, py, facing, view->cell) ||
                (list == INDOOR_LIST && view_cell_hidden(m, view->cell, PAIR_AS_WALLS)) ||
                xeen_read16(m, (uint16_t)(record + XEEN_VIEW_RECORD_FRAME)) != XEEN_VIEW_RECORD_UNUSED) {
                continue;
            }
            xeen_write16(m, (uint16_t)(record + XEEN_VIEW_RECORD_X),
                         xeen_read16(m, (uint16_t)(x_row + rows + view->place * 2u)));
            xeen_write16(m, (uint16_t)(record + XEEN_VIEW_RECORD_Y),
                         xeen_read16(m, (uint16_t)(XEEN_DS_OBJECT_VIEW_Y + rows + view->place * 2u)));
            xeen_write16(m, (uint16_t)(record + XEEN_VIEW_RECORD_FRAME), frame);
            xeen_write16(m, record, sprite);
            set_mirrored(m, record, flip);
            if (view->cell == VIEW_CELL_0_C) {
                m->ds[XEEN_DS_EVENT_OBJECT] = (uint8_t)(i + 1u);
            }
        }
    }
}
/* The indoor view's objects (place_objects). Original: 20011. */
xeen_status xeen_scene_objects(xeen_movement *m)
{
    if (!m) {
        return XEEN_BAD_INPUT;
    }
    place_objects(m, INDOOR_LIST);
    m->status = XEEN_OK;
    return XEEN_OK;
}

/* 208FB, the wall items' view: each view shows, in its slot of XEEN_DS_VIEW_WALL_ITEMS and its record, the first wall
 * item facing the party's way in its view cell. The twelve primary views (one per cell) are hidden as an object there
 * would be (view_cell_hidden); the six others (cells 2, L1 / R1 once, 3, L2 / R2 twice) by any of their own listed
 * walls (GUARDS of WALLS), a stricter test: they are not merged with the primaries. */
typedef struct {
    uint8_t cell, selector, primary, guards, walls[7];
    uint16_t record;
} wall_view;
#define R(n) XEEN_DS_INDOOR_VIEW_RECORD(n##u)
static const wall_view wall_views[] = {
        {VIEW_CELL_0_C, 1, 1, 0, {0}, R(148)},
        {VIEW_CELL_1_C, 4, 1, 0, {0}, R(123)},
        {VIEW_CELL_1_L1, 3, 1, 0, {0}, R(122)},
        {VIEW_CELL_1_R1, 5, 1, 0, {0}, R(124)},
        {VIEW_CELL_2_C, 8, 1, 0, {0}, R(94)},
        {VIEW_CELL_2_L1, 7, 1, 0, {0}, R(93)},
        {VIEW_CELL_2_R1, 9, 1, 0, {0}, R(95)},
        {VIEW_CELL_2_L1, 6, 0, 4, {FACE(0, L1), LEFT(0, C), FACE(1, L1), LEFT(2, L1)}, R(92)},
        {VIEW_CELL_2_R1, 10, 0, 4, {FACE(0, R1), RIGHT(0, C), FACE(1, R1), RIGHT(2, R1)}, R(96)},
        {VIEW_CELL_3_C, 15, 1, 0, {0}, R(50)},
        {VIEW_CELL_3_L1, 14, 1, 0, {0}, R(49)},
        {VIEW_CELL_3_L2, 13, 1, 0, {0}, R(48)},
        {VIEW_CELL_3_R1, 16, 1, 0, {0}, R(51)},
        {VIEW_CELL_3_R2, 17, 1, 0, {0}, R(52)},
        {VIEW_CELL_3_L2, 12, 0, 6, {FACE(0, C), FACE(1, L1), FACE(2, L1), LEFT(1, C), LEFT(3, L1), LEFT(3, L2)}, R(47)},
        {VIEW_CELL_3_R2, 18, 0, 6, {FACE(0, C), FACE(1, R1), FACE(2, R1), RIGHT(1, C), RIGHT(3, R1), RIGHT(3, R2)}, R(53)},
        {VIEW_CELL_3_L2, 11, 0, 7, {FACE(0, L1), LEFT(0, C), FACE(1, L1), FACE(2, L2), LEFT(2, L1), LEFT(3, L2), LEFT(3, L3)},
         R(46)},
        {VIEW_CELL_3_R2, 19, 0, 7, {FACE(0, R1), RIGHT(0, C), FACE(1, R1), FACE(2, R2), RIGHT(2, R1), RIGHT(3, R2), RIGHT(3, R3)},
         R(54)}};
#undef R
#undef FACE
#undef LEFT
#undef RIGHT
/* Whether a wall item at VIEW is hidden: a primary view by the cell's test (view_cell_hidden, as for objects), the
 * others by any of their guarding walls. */
static int view_blocked(const xeen_movement *m, const wall_view *view)
{
    unsigned k;
    if (view->primary) {
        return view_cell_hidden(m, view->cell, PAIR_AS_WALLS);
    }
    for (k = 0; k < view->guards; k++) {
        if (wall_at(m, view->walls[k])) {
            return 1;
        }
    }
    return 0;
}
/* The indoor view's wall items: every wall item facing the party's way fills each of its cell's views that is not
 * hidden (view_blocked) and still free (selector FFh; the first such item wins): the view's record (frame, picture)
 * and selector. The item in the party's own cell is noted in XEEN_DS_PARTY_CELL_WALL_ITEM. Original: 208FB. */
xeen_status xeen_scene_walls(xeen_movement *m)
{
    unsigned i, v, count;
    int px, py;
    uint16_t facing;
    if (!m) {
        return XEEN_BAD_INPUT;
    }
    px = xeen_signed_byte(m->ds[XEEN_DS_PARTY_X]);
    py = xeen_signed_byte(m->ds[XEEN_DS_PARTY_Y]);
    facing = m->ds[XEEN_DS_PARTY_DIRECTION];
    count = m->ds[XEEN_DS_WALL_ITEM_COUNT];
    memset(m->ds + XEEN_DS_VIEW_WALL_ITEMS, XEEN_VIEW_NO_WALL_ITEM, XEEN_VIEW_WALL_ITEM_SLOTS);
    m->ds[XEEN_DS_PARTY_CELL_WALL_ITEM] = 0;
    for (i = 0; i < count; i++) {
        uint16_t entity = (uint16_t)(XEEN_DS_WALL_ITEMS + XEEN_WALL_ITEM_BYTES * i),
                 frame = m->ds[entity + XEEN_WALL_ITEM_FRAME];
        uint16_t sprite =
                xeen_read16(m, (uint16_t)(XEEN_DS_WALL_ITEM_SPRITES + 2u * m->ds[entity + XEEN_WALL_ITEM_SLOT]));
        if (m->ds[entity + XEEN_WALL_ITEM_DIRECTION] != facing) {
            continue;
        }
        for (v = 0; v < sizeof wall_views / sizeof wall_views[0]; v++) {
            const wall_view *view = &wall_views[v];
            uint16_t selector = (uint16_t)(XEEN_DS_VIEW_WALL_ITEMS + view->selector);
            if (!at_view_cell(m, entity, px, py, facing, view->cell) || view_blocked(m, view) ||
                m->ds[selector] != XEEN_VIEW_NO_WALL_ITEM) {
                continue;
            }
            xeen_write16(m, (uint16_t)(view->record + XEEN_VIEW_RECORD_FRAME), frame);
            xeen_write16(m, view->record, sprite);
            m->ds[selector] = (uint8_t)i;
            if (view->cell == VIEW_CELL_0_C) {
                m->ds[XEEN_DS_PARTY_CELL_WALL_ITEM] = (uint8_t)(i + 1u);
            }
        }
    }
    m->status = XEEN_OK;
    return XEEN_OK;
}
/*
 * The projectiles move one step (set_projectile_frames' flights): for each member, a projectile at its flight's last
 * step ends there (its handle FFFFh; a monster's shot also frees the member's XEEN_DS_MISSILE_SLOTS byte); otherwise
 * the one at the latest step taken moves on to the next step (handle words only, at fixed levels). Original: outdoors
 * 1B2F2..1B3DB, indoors 1B3DE..1B4C3.
 */
static void advance_projectiles(xeen_movement *m, enum view_list list, int monster_shot)
{
    unsigned i, step;
    for (i = 0; i < 8; i++) {
        uint16_t last = flight_level(list, monster_shot, 3, i);
        if (xeen_read16(m, last) != XEEN_VIEW_RECORD_UNUSED) {
            xeen_write16(m, last, XEEN_VIEW_RECORD_UNUSED);
            if (monster_shot) {
                m->ds[(uint16_t)(XEEN_DS_MISSILE_SLOTS + i)] = 0;
            }
            continue;
        }
        for (step = 3; step-- > 0;) {
            uint16_t at = flight_level(list, monster_shot, step, i);
            if (xeen_read16(m, at) != XEEN_VIEW_RECORD_UNUSED) {
                xeen_write16(m, flight_level(list, monster_shot, step + 1u, i), xeen_read16(m, at));
                xeen_write16(m, at, XEEN_VIEW_RECORD_UNUSED);
                break;
            }
        }
    }
}
/**
 * The original's CWD/IDIV remainder: VALUE as a signed word modulo DIVISOR, taking the dividend's
 * sign.
 */
static uint16_t wrap_mod(uint16_t value, int32_t divisor)
{
    /* Original CWD/IDIV: signed16 dividend, remainder takes dividend's sign. */
    int32_t v = xeen_sign16(value);
    return (uint16_t)(v % divisor);
}
/* Map monster ENTITY's frame (+2) for one scene: unchanged while it has a condition; below 8 it cycles 0..7, or with
 * its row's +30h set runs back and forth (+9 the direction: down to 0, or up to 7 and then 6). From 8 up it steps by
 * one; at 11 it holds while +0Ah counts down to 0 (then frame 0), and reaching 11 from below takes 10 while +0Ah,
 * counted down, stays nonzero (else 0). */
static void animate_monster_frame(xeen_movement *m, uint16_t entity, const uint8_t *row)
{
    uint8_t *frame = &m->ds[(uint16_t)(entity + XEEN_MAP_MONSTER_FRAME)];
    if (m->ds[(uint16_t)(entity + XEEN_MAP_MONSTER_CONDITION)]) {
        /* Non-moving monster entry: frame/bounce state untouched. */
    } else if (*frame < 8) {
        uint8_t *bounce = &m->ds[(uint16_t)(entity + XEEN_MAP_MONSTER_BOUNCE_BACK)];
        if (!row[XEEN_MONSTER_BOUNCE]) {
            *frame = (uint8_t)((uint8_t)(*frame + 1u) % 8u);
        } else if (*bounce) {
            if (*frame) {
                --*frame;
            }
            if (!*frame) {
                *bounce = 0;
            }
        } else {
            *frame = (uint8_t)((uint8_t)(*frame + 1u) % 8u);
            if (!*frame) {
                *bounce ^= 1;
                *frame = 6;
            }
        }
    } else {
        uint8_t *hold = &m->ds[(uint16_t)(entity + XEEN_MAP_MONSTER_FRAME_HOLD)];
        if (*frame == 11) {
            if (!--*hold) {
                *frame = 0;
            }
        } else if (++*frame >= 11) {
            *frame = --*hold ? 10 : 0;
        }
    }
}
/* Map monster ENTITY's effect for one scene while its +0Eh word is set: the +0Ch effect blinks (bit 80h) on the +10h
 * timer, or without an effect the timer counts to 8 and restores +0Eh and +0Ch from the row (+31h). */
static void animate_monster_effect(xeen_movement *m, uint16_t entity, const uint8_t *row)
{
    if (xeen_read16(m, (uint16_t)(entity + XEEN_MAP_MONSTER_EFFECT))) {
        uint16_t effect = (uint16_t)(entity + XEEN_MAP_MONSTER_EFFECT_CYCLE),
                 timer = (uint16_t)(entity + XEEN_MAP_MONSTER_EFFECT_STEP);
        if (xeen_read16(m, effect)) {
            if (xeen_read16(m, effect) & 0x80u) {
                if (xeen_read16(m, timer)) {
                    xeen_write16(m, timer, (uint16_t)(xeen_read16(m, timer) - 1u));
                }
                if (!xeen_read16(m, timer)) {
                    xeen_write16(m, effect, (uint16_t)(xeen_read16(m, effect) ^ 0x80u));
                }
            } else {
                xeen_write16(m, timer, wrap_mod((uint16_t)(xeen_read16(m, timer) + 1u), 3));
                if (!xeen_read16(m, timer)) {
                    xeen_write16(m, effect, (uint16_t)(xeen_read16(m, effect) ^ 0x80u));
                    xeen_write16(m, timer, 2);
                }
            }
        } else {
            xeen_write16(m, timer, wrap_mod((uint16_t)(xeen_read16(m, timer) + 1u), 8));
            if (!xeen_read16(m, timer)) {
                xeen_write16(m, (uint16_t)(entity + XEEN_MAP_MONSTER_EFFECT), row[XEEN_MONSTER_EFFECT]);
                xeen_write16(m, effect, row[XEEN_MONSTER_EFFECT]);
            }
        }
    }
}
/* Wall item I's sprite's frame count (the map's XEEN_DS_WALL_ITEM_FRAMES by the item's sprite). */
static uint16_t wall_item_frames(const xeen_movement *m, unsigned i)
{
    return (uint16_t)(XEEN_DS_WALL_ITEM_FRAMES +
                      m->ds[(uint16_t)(XEEN_DS_WALL_ITEMS + XEEN_WALL_ITEM_SPRITE + XEEN_WALL_ITEM_BYTES * i)]);
}
/* The host's guard (not the original's), before any write: every wall item's sprite has frames (a zero count is the
 * original's divide fault at 1B4EF). No write of the animation can reach the counts or the items' sprite bytes (the
 * monster records lie from E530h, the projectile handles below 1800h, the slots at 9744h). */
static int wall_items_have_frames(const xeen_movement *m)
{
    unsigned i;
    for (i = 0; i < m->ds[XEEN_DS_WALL_ITEM_COUNT]; i++) {
        if (!m->ds[wall_item_frames(m, i)]) {
            return 0;
        }
    }
    return 1;
}
/* Each map monster's frame and effect for one scene (animate_monster_frame, animate_monster_effect; its database row
 * by its id). */
static void animate_monsters(xeen_movement *m, const uint8_t *database)
{
    unsigned i;
    for (i = 0; i < m->ds[XEEN_DS_MAP_MONSTER_COUNT]; i++) {
        uint16_t entity = (uint16_t)XEEN_MAP_MONSTER(i);
        const uint8_t *row =
                database + (size_t)m->ds[(uint16_t)(entity + XEEN_MAP_MONSTER_ID)] * XEEN_MONSTER_RECORD_BYTES;
        animate_monster_frame(m, entity, row);
        animate_monster_effect(m, entity, row);
    }
}
/* Each wall item's frame advances modulo its sprite's frame count (1B4C3..1B4FD). */
static void advance_wall_item_frames(xeen_movement *m)
{
    unsigned i;
    for (i = 0; i < m->ds[XEEN_DS_WALL_ITEM_COUNT]; i++) {
        uint16_t frame = (uint16_t)(XEEN_DS_WALL_ITEMS + XEEN_WALL_ITEM_BYTES * i + XEEN_WALL_ITEM_FRAME);
        m->ds[frame] = (uint8_t)((uint8_t)(m->ds[frame] + 1u) % m->ds[wall_item_frames(m, i)]);
    }
}
/*
 * The scene's animation step (1B0F6, a far procedure): refused before any write when a monster's database row is
 * missing or a wall item's sprite has no frames (the host's guards); then the counters (F47C modulo 5, the scene
 * phase modulo 8), the monsters (animate_monsters), the projectiles' flight (advance_projectiles; the list is read
 * after the monsters: map monster 215's effect word +0Ch is XEEN_DS_OUTDOORS), the wall items' frames.
 */
xeen_caller_result xeen_scene_animate(xeen_movement *m, const uint8_t *database, size_t size)
{
    if (!m || !database) {
        xeen_caller_result r = {XEEN_BAD_INPUT, XEEN_SCENE_ANIMATE};
        return r;
    }
    if (!map_monsters_in_database(m, size)) {
        xeen_caller_result r = {XEEN_SERVICE_REQUIRED, XEEN_SCENE_ANIMATE};
        return r;
    }
    if (!wall_items_have_frames(m)) {
        xeen_caller_result r = {XEEN_BAD_INPUT, XEEN_SCENE_ANIMATE_NO_FRAMES};
        return r;
    }
    xeen_write16(m, 0xf47c, wrap_mod((uint16_t)(xeen_read16(m, 0xf47c) + 1u), 5));
    m->ds[XEEN_DS_SCENE_PHASE] = (uint8_t)((uint8_t)(m->ds[XEEN_DS_SCENE_PHASE] + 1u) % 8u);
    animate_monsters(m, database);
    if (monster_shot_flight(m) || m->ds[XEEN_DS_MISSILES_FLYING]) {
        advance_projectiles(m, xeen_read16(m, XEEN_DS_OUTDOORS) ? OUTDOOR_LIST : INDOOR_LIST, monster_shot_flight(m));
    }
    advance_wall_item_frames(m);
    return boundary(m, XEEN_SCENE_DRAW);
}

xeen_caller_result xeen_scene_outdoor_shuffle(xeen_movement *m, uint16_t *selected)
{
    if (!m || !selected || !xeen_read16(m, XEEN_DS_OUTDOORS)) {
        xeen_caller_result r = {XEEN_BAD_INPUT, XEEN_SCENE_OUTDOOR_SHUFFLE};
        return r;
    }
    shuffle(m, OUTDOOR_LIST, selected);
    return boundary(m, XEEN_SCENE_PRESENT);
}
xeen_caller_result xeen_scene_outdoor_restore(xeen_movement *m, uint16_t selected)
{
    if (!m || selected > 3) {
        xeen_caller_result r = {XEEN_BAD_INPUT, XEEN_SCENE_OUTDOOR_RESTORE};
        return r;
    }
    restore(m, OUTDOOR_LIST, selected);
    return boundary(m, XEEN_SCENE_ANIMATE);
}

/*
 * Outdoors every map monster standing in one of the twelve view cells (monster_views) becomes active and takes the
 * cell's first free slot: the slot record's frame word holds the monster's number until fill_outdoor_record, then
 * the selector is set. There is no occlusion test outdoors; every cell is tried, so one monster may stand in several.
 * The party's place and facing are read once (2126C keeps them in DI, SI and [bp-12h]; nothing here writes them).
 * Original: 2126C's first loop.
 */
static void assign_outdoor_slots(xeen_movement *m)
{
    unsigned i, v, count = m->ds[XEEN_DS_MAP_MONSTER_COUNT];
    int px = xeen_signed_byte(m->ds[XEEN_DS_PARTY_X]), py = xeen_signed_byte(m->ds[XEEN_DS_PARTY_Y]);
    uint16_t facing = m->ds[XEEN_DS_PARTY_DIRECTION];
    memset(m->ds + XEEN_DS_VIEW_MONSTERS, XEEN_VIEW_NO_MONSTER, XEEN_VIEW_MONSTER_SLOTS);
    for (i = 0; i < count; i++) {
        uint16_t entity = (uint16_t)XEEN_MAP_MONSTER(i);
        for (v = 0; v < 12; v++) {
            const monster_view *view = &monster_views[v];
            int k;
            if (!at_view_cell(m, entity, px, py, facing, view->cell)) {
                continue;
            }
            m->ds[(uint16_t)(entity + XEEN_MAP_MONSTER_ACTIVE)] = 1;
            k = free_slot(m, view);
            if (k >= 0) {
                xeen_write16(m, (uint16_t)(view_record(OUTDOOR_LIST, view->outdoor[k]) + XEEN_VIEW_RECORD_FRAME),
                             (uint16_t)i);
                m->ds[slot_selector(view, (unsigned)k)] = (uint8_t)i;
            }
        }
    }
}
/* Outdoor view record K of the 26 a monster can use (XEEN_DS_OUTDOOR_MONSTER_RECORDS), when assign_outdoor_slots gave
 * it one: the monster's flags, frame and sprite, its row's y (21963) and, for a flier, the bob added to x and y.
 * Returns 0 when the monster's database row lies outside the SIZE bytes (the record already partly written). */
static int fill_outdoor_record(xeen_movement *m, const uint8_t *database, size_t size, unsigned k)
{
    uint16_t record = (uint16_t)(XEEN_DS_OUTDOOR_VIEW_LIST +
                                 xeen_read16(m, (uint16_t)(XEEN_DS_OUTDOOR_MONSTER_RECORDS + 2u * k)) *
                                         XEEN_VIEW_RECORD_BYTES);
    uint16_t monster = xeen_read16(m, (uint16_t)(record + XEEN_VIEW_RECORD_FRAME)), entity, frame, sprite;
    size_t row;
    if (monster == XEEN_VIEW_RECORD_UNUSED) {
        return 1;
    }
    clear_monster_flags(m, record);
    entity = (uint16_t)XEEN_MAP_MONSTER(monster);
    /* the original stores the whole frame before it reads the effect flags (an effect address can land on this
     * word), then the frame within the sprite */
    xeen_write16(m, (uint16_t)(record + XEEN_VIEW_RECORD_FRAME), m->ds[(uint16_t)(entity + XEEN_MAP_MONSTER_FRAME)]);
    add_effect_flags(m, record, entity);
    sprite = monster_sprite(m, entity, &frame);
    xeen_write16(m, (uint16_t)(record + XEEN_VIEW_RECORD_FRAME), frame);
    xeen_write16(m, record, sprite);
    xeen_write16(m, (uint16_t)(record + XEEN_VIEW_RECORD_Y),
                 xeen_read16(m, (uint16_t)(XEEN_DS_OUTDOOR_MONSTER_VIEW_Y + 2u * k)));
    row = (size_t)m->ds[(uint16_t)(entity + XEEN_MAP_MONSTER_ID)] * XEEN_MONSTER_RECORD_BYTES;
    if (row + XEEN_MONSTER_FLYING + 1u > size) {
        return 0;
    }
    if (database[row + XEEN_MONSTER_FLYING]) {
        uint8_t phase = m->ds[XEEN_DS_SCENE_PHASE];
        monster_shift(m, (uint16_t)(record + XEEN_VIEW_RECORD_X),
                      xeen_signed_byte(m->ds[(uint16_t)(XEEN_DS_MONSTER_BOB_X + phase)]));
        monster_shift(m, (uint16_t)(record + XEEN_VIEW_RECORD_Y),
                      xeen_signed_byte(m->ds[(uint16_t)(XEEN_DS_MONSTER_BOB_Y + phase)]));
    }
    return 1;
}
/* The outdoor monsters' view: slots, positions, then each record. Original: 2126C. Runs on a private copy so a
 * missing database row (an out-of-range far read in DOS) rejects with DS unchanged. */
static int outdoor_monsters(xeen_movement *m, const uint8_t *database, size_t size)
{
    unsigned k;
    assign_outdoor_slots(m);
    place_monster_records(m, OUTDOOR_LIST);
    for (k = 0; k < 26; k++) {
        if (!fill_outdoor_record(m, database, size, k)) {
            return 0;
        }
    }
    return 1;
}
xeen_status xeen_scene_outdoor_monsters(xeen_movement *m, const uint8_t *database, size_t size)
{
    xeen_movement *trial;
    if (!m || !database || !xeen_read16(m, XEEN_DS_OUTDOORS)) {
        return XEEN_BAD_INPUT;
    }
    trial = malloc(sizeof *trial);
    if (!trial) {
        return XEEN_BAD_INPUT;
    }
    *trial = *m;
    if (!outdoor_monsters(trial, database, size)) {
        free(trial);
        return XEEN_BAD_INPUT;
    }
    *m = *trial;
    free(trial);
    return XEEN_OK;
}
/* The outdoor view's objects (place_objects; m->status is left as it was). Original: 219E4. */
xeen_status xeen_scene_outdoor_objects(xeen_movement *m)
{
    if (!m || !xeen_read16(m, XEEN_DS_OUTDOORS)) {
        return XEEN_BAD_INPUT;
    }
    place_objects(m, OUTDOOR_LIST);
    return XEEN_OK;
}
