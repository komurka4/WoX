#ifndef XEEN_ADDON_TRAINING_PREVIEW_H
#define XEEN_ADDON_TRAINING_PREVIEW_H
#include <stdint.h>
#include "xeen/movement.h"
/* Add-on training_preview (owner-approved in Phase 190, NOT original
 * behavior; off by default): at the training grounds (2547B) a Prev button
 * between Train and ESC (P) opens a level ladder over the trainer's view:
 * every level the selected member can reach by training in a row, its cost,
 * the running total and what stops the row (the party's gold, the town's
 * top level, experience). Read-only: the ladder is computed on a scratch copy
 * of the member record with the game's own training rules (town.c); the DS,
 * the saves, the RNG and the clock never see it. */

/* A ladder row's status. */
enum {
    XEEN_TRAIN_ROW_OK,      /* trainable now: the experience allows it and the gold covers the total */
    XEEN_TRAIN_ROW_GOLD,    /* the experience allows it, the party's gold does not cover the total */
    XEEN_TRAIN_ROW_CAP,     /* above the town's top level (271AE's "learned all"): the row ends */
    XEEN_TRAIN_ROW_XP       /* experience still needed (28C2D): the row ends */
};
typedef struct { unsigned level; uint32_t cost, total; uint8_t status; } xeen_training_row;
#define XEEN_TRAINING_ROWS_MAX 256u
typedef struct {
    unsigned level, top;            /* the member's level (+23h), the town's top level (271AE's F64C) */
    uint32_t gold;                  /* the party's gold (F00A) */
    unsigned afford;                /* the highest level the row reaches with status OK (= level: none) */
    int refused;                    /* 28CFC refuses the member (condition 8 or 11..15) */
    unsigned condition;             /* 2AB96's worst condition (16 = good) */
    unsigned count;                 /* the rows: every trainable level, then the one that stops the row */
    xeen_training_row rows[XEEN_TRAINING_ROWS_MAX];
} xeen_training_ladder;
/* MEMBER is a 162h-byte party record. 1 on success, 0 on bad input. */
int xeen_training_ladder_build(const xeen_movement *,const uint8_t *member,unsigned top,uint32_t gold,
    xeen_training_ladder *out);

/* The add-on's state (ephemeral, never saved). */
typedef struct {
    int open;                       /* the ladder is shown */
    int relocated;                  /* the panel was drawn with the three buttons */
} xeen_training_preview;
#endif
