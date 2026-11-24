#ifndef XEEN_ADDON_DAMAGE_NUMBERS_STATE_H
#define XEEN_ADDON_DAMAGE_NUMBERS_STATE_H
/* damage_numbers (Phase 199 add-on, not original behavior): the state shared
 * by notes.c (the hooks' queue) and overlay.c (layout and drawing). */
#include <stdint.h>
#include "xeen/addon/damage_numbers.h"

enum { DN_MAX = 96 };
typedef struct {
    uint8_t used;
    uint8_t party;          /* 1: party member (index = record index 0..5), 0: monster */
    uint8_t miss;           /* the word "Miss" instead of a value */
    uint8_t stamped;        /* t0 set: first shown at t0 */
    uint8_t index;
    int32_t value;          /* negative: damage, positive: healing */
    uint64_t t0;            /* host clock (ns) of its first display */
    uint64_t seq;           /* order of the notes */
    /* monsters: the draw record (4ADB1 view record, 12 bytes) of the
     * monster's view slot when noted, refreshed while it stays visible */
    uint8_t have_record;
    uint8_t record[12];
} dn_entry;

extern dn_entry dn_entries[DN_MAX];
extern uint64_t dn_seq;

/* The event interpreter's HP hook (xeen_damage_numbers_event_hp while on). */
void dn_event_hp(const uint8_t *ds,uint16_t record,uint16_t value,int restore);
/* The monster's view slot draw record in DS (12 bytes), or 0. */
int dn_monster_record(const uint8_t *ds,unsigned index,uint8_t out[12]);
#endif
