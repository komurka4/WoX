#ifndef XEEN_ADDON_MONSTER_DIFFICULTY_ASSESS_H
#define XEEN_ADDON_MONSTER_DIFFICULTY_ASSESS_H
/* monster_difficulty (Phase 201 add-on, NOT original behavior): the tier
 * estimate shared by overlay.c. Reads DS only. */
#include <stdint.h>
#include "xeen/movement.h"

typedef struct {
    unsigned cls, level;          /* class (+13h), level (+23h + +24h) */
    int hp;                       /* maximum HP (2AAC2) */
    unsigned ac;                  /* 4660F armor class */
    int to_hit;                   /* the fixed part of 38818's score: base accuracy bonus + weapon hit + level/divisor */
    double damage;                /* expected damage of one hit: Might bonus + 3891F's prepared value */
    int bypass_resist;            /* a melee weapon of type 22h (36738 skips physical resistance) */
} md_member;
typedef struct {
    unsigned n;                   /* members (every one, whatever its condition) */
    md_member m[8];
    double hp;                    /* the members' maximum HP */
    int weakest;                  /* the lowest maximum HP among them */
} md_party;

/* The party at full health without temporary effects (owner option 3):
 * every member record (ED8Ch of them), maximum HP, base stats and AC.
 * 0 when there is none. */
int md_party_read(const xeen_movement *m,md_party *p);
/* The tier 0..4 of monster INDEX (E530h + 20 * INDEX) with its database ROW
 * (60 bytes) for party P. */
unsigned md_tier(const xeen_movement *m,const md_party *p,const uint8_t *row,unsigned index);
#endif
