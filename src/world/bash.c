#include "xeen/party.h"
#include "xeen/ds_globals.h"
#include "xeen/bash.h"
#include "xeen/character_record.h"
#include "xeen/minimap.h"
#include <string.h>

#define MEMBER(m, i) ((m)->ds + XEEN_PARTY_RECORDS + (unsigned)(i) * XEEN_CHARACTER_BYTES)

/** Returns nonzero when a member can bash: its worst condition is neither Asleep nor Paralyzed..Eradicated. */
static int member_able(const uint8_t *ch)
{
    return !xeen_condition_cannot_act(xeen_character_worst_condition(ch));
}

/*
 * The bashers: the first two able members, in party order (FFh each when there is none). With no able member the
 * first basher is the record before the party (an original quirk, below).
 */
static void choose_bashers(const xeen_movement *m, uint8_t *first, uint8_t *second)
{
    unsigned i, party = m->ds[XEEN_DS_PARTY_COUNT];
    *first = *second = 0xffu;
    for (i = 0; i < party; ++i) {
        if (member_able(MEMBER(m, i))) {
            if (*first == 0xffu) {
                *first = (uint8_t)i;
            } else {
                *second = (uint8_t)i;
                return;
            }
        }
    }
}

/* Whether the cell ahead exists: 1, 0 for the edge of the maps (no such cell), -1 when the lookup fails (m->status:
 * XEEN_BAD_INPUT for a facing above 3, else XEEN_SERVICE_REQUIRED). Original: 3D76D: 1C680 of the cell ahead (the
 * DS:655/715 offsets of the facing, mask FFFFh, shift 0); 8888h ends the bash at 3D88F. */
static int cell_ahead_exists(xeen_movement *m)
{
    uint8_t d = m->ds[XEEN_DS_PARTY_DIRECTION];
    uint16_t ahead = 0;
    int ax = xeen_signed_byte(m->ds[XEEN_DS_PARTY_X]) + xeen_signed_byte(m->ds[XEEN_DS_STEP_X(d, XEEN_STEP_AHEAD)]);
    int ay = xeen_signed_byte(m->ds[XEEN_DS_PARTY_Y]) + xeen_signed_byte(m->ds[XEEN_DS_STEP_Y(d, XEEN_STEP_AHEAD)]);
    if (d > 3u) {
        m->status = XEEN_BAD_INPUT;
        return -1;
    }
    if (xeen_maze_lookup(m, (int16_t)ax, (int16_t)ay, 0xffffu, 0u, &ahead) != XEEN_OK) {
        m->status = XEEN_SERVICE_REQUIRED;
        return -1;
    }
    return ahead != XEEN_CELL_OUTSIDE_MAPS;
}

/* The bashed wall opens on both sides: the party's own cell on its facing, then the cell ahead on the opposite side,
 * each through the wall setter 2DE1D (xeen_maze_set_wall: 1C680 first, so the store leaves that lookup's scratch
 * F48E, F490, F492 and the slot F494, as the original's). Returns XEEN_OK, XEEN_BAD_INPUT for a facing above 3 (the
 * native bound; cell_ahead_exists refuses it first), or XEEN_SERVICE_REQUIRED for a store whose map is not cached
 * (the original's search has no bound; both lookups repeat ones that found their slot). Original: 3D839..3D88C
 * (3D844 2DE1D(x, y, facing, 3); the CS:1F59 table: north y + 1 side 2, east x + 1 side 3, south y - 1 side 0, west
 * x - 1 side 1; 3D887 2DE1D(nx, ny, side, 3)); Phase 332 kit xeen_dat_evidence/phase332/a. */
static xeen_status open_bashed_wall(xeen_movement *m)
{
    unsigned facing = m->ds[XEEN_DS_PARTY_DIRECTION];
    int x = xeen_signed_byte(m->ds[XEEN_DS_PARTY_X]), y = xeen_signed_byte(m->ds[XEEN_DS_PARTY_Y]), nx = x, ny = y;
    if (!xeen_direction_step(facing, &nx, &ny)) {
        return m->status = XEEN_BAD_INPUT;
    }
    if (xeen_maze_set_wall(m, (int16_t)x, (int16_t)y, facing, XEEN_INDOOR_WALL_BASHED) != XEEN_OK ||
        xeen_maze_set_wall(m, (int16_t)nx, (int16_t)ny, facing ^ 2u, XEEN_INDOOR_WALL_BASHED) != XEEN_OK) {
        return m->status = XEEN_SERVICE_REQUIRED;
    }
    return XEEN_OK;
}

/*
 * Bash (indoors): the first two able members hit the wall ahead together. The bash sound plays and each basher
 * loses 2 HP (at 2 or less a basher falls unconscious or dies). At the edge of the maps nothing more happens. A
 * face ahead of kind 7, 14 or 15 ends the bash without a roll. Otherwise the bashers' Might plus a random 1..30
 * against the map's bash threshold (the locked grate's own threshold for that wall) opens the wall on both sides
 * when it reaches it (compared signed). RESULT records the bashers, the wall, the roll, the total and whether the
 * wall opened.
 */
xeen_status xeen_bash_wall(xeen_movement *m, xeen_entity_random_fn random, void *user,
                           xeen_native_audio *audio, xeen_bash_result *result)
{
    uint16_t wall = 0, raw, roll, might1, might2;
    uint8_t first, second;
    uint8_t *ch1, *ch2 = NULL;
    int ahead;
    xeen_bash_result r;
    if (!m || !random || !result) {
        return XEEN_BAD_INPUT;
    }
    memset(&r, 0, sizeof r);
    r.first = r.second = 0xffu;
    m->status = XEEN_OK;
    if (xeen_read16(m, XEEN_DS_OUTDOORS)) {
        return m->status = XEEN_BAD_INPUT;
    }
    choose_bashers(m, &first, &second);
    /* With no able member the original's basher index stays 0, which makes the basher the record before the party
     * (its HP word is in the memory metrics' bytes) and skips the second. Only a dead party gets here (a key at the
     * post-defeat poll, e.g. after a defeated combat entry); the 2 HP are taken from that DS record, as the
     * original does. Original: [BP-2] 0 and DI 0: 3D6F8 makes the basher B2BC + 0 * 162h (HP word B412, in 18886's
     * B410..B41D), 3D72E skips the second; F60A at the 17B09 poll (xeen_dat_evidence/phase159, run orig-bash-w);
     * 2A152(B2BC, 2). */
    ch1 = first == 0xffu ? m->ds + XEEN_DS_NUMBERED_MEMBERS : MEMBER(m, first);
    if (second != 0xffu) {
        ch2 = MEMBER(m, second);
    }
    r.first = first;
    r.second = second;
    if (xeen_native_audio_effect(audio, m, 31u) != XEEN_OK) {
        return m->status = XEEN_SERVICE_REQUIRED; /* Original: 3D69B 1880E(1Fh) */
    }
    /* Original: 3D6FD and 3D75D, 2A152(member, 2) each. */
    if (xeen_character_subtract_hp(m, ch1, 2u, audio, NULL) != XEEN_OK ||
        (ch2 && xeen_character_subtract_hp(m, ch2, 2u, audio, NULL) != XEEN_OK)) {
        return m->status = XEEN_SERVICE_REQUIRED;
    }
    if ((ahead = cell_ahead_exists(m)) < 0) {
        return m->status;
    }
    if (!ahead) {
        *result = r;
        m->status = XEEN_OK;
        return XEEN_OK;
    }
    if (xeen_navigation_query(m, XEEN_STEP_HERE, &wall) != XEEN_OK) {
        return m->status; /* Original: 3D79F: 1C1C1(2) */
    }
    r.wall = (uint8_t)wall;
    /* A face ahead of kind 7, 14 or 15 ends the bash with no roll, adding to the view's face flags ahead: of kinds 7,
     * 14 and 15 for a 7; 14 and 15 for a 14; 15 for a 15 (the original's three increments fall through). Their only
     * reader is the centre surface, and the scene that follows scans the walls again first, clearing them: they are
     * never drawn. Kept as the original's writes. Original: 3D7B8..; the readers 24545, 24584, 24599; 3D894, 1BBDC. */
    switch (wall) {
        case 7u:
            ++m->ds[XEEN_DS_VIEW_FACE(7u, XEEN_VIEW_FACE_AHEAD)];
            /* fallthrough */
        case 14u:
            ++m->ds[XEEN_DS_VIEW_FACE(14u, XEEN_VIEW_FACE_AHEAD)];
            /* fallthrough */
        case 15u:
            ++m->ds[XEEN_DS_VIEW_FACE(15u, XEEN_VIEW_FACE_AHEAD)];
            *result = r;
            return XEEN_OK;
        default:
            break;
    }
    /* The random draw is entered with the second basher's Might (or 0) as AX; the generator masks the raw value's
     * sign bit before the range. Original: 3D7F3..3D806, 504E0. */
    might1 = xeen_combat_stat(m, ch1, XEEN_STAT_MIGHT, 0);
    might2 = ch2 ? xeen_combat_stat(m, ch2, XEEN_STAT_MIGHT, 0) : 0u;
    raw = random(user, might2);
    (void)xeen_entity_rng_range(raw, 1u, 30u, &roll);
    r.roll = (uint8_t)roll;
    r.might = (uint16_t)(might1 + might2 + r.roll);
    if (xeen_navigation_query(m, XEEN_STEP_HERE, &wall) != XEEN_OK) {
        return m->status; /* Original: 3D810: 1C1C1(2) */
    }
    /* Walls 4 and 13 are tested by name but take the map's bash threshold like every kind but the locked grate.
     * Original: 3D818. */
    r.threshold = m->ds[wall == XEEN_INDOOR_WALL_LOCKED_GRATE ? XEEN_DS_MAP_FIELD(BASH_GRATE) : XEEN_DS_MAP_FIELD(BASH)];
    if ((int16_t)r.might < (int16_t)r.threshold) {
        *result = r;
        return XEEN_OK;
    }
    if (open_bashed_wall(m) != XEEN_OK) {
        return m->status;
    }
    r.opened = 1;
    *result = r;
    return XEEN_OK;
}
