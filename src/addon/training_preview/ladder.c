/* Add-on training_preview (owner-approved, NOT original behavior): the level
 * ladder. Each row is one press of Train (2549B) replayed on a scratch copy
 * of the member record with the game's own rules (town.c): the top level
 * first (271AE: at or above it "learned all"), then the experience still
 * needed (28C2D; nonzero ends the row), the price level * level * 10 and the
 * experience step (28BCF, xeen_train_level). The party's gold (29281, an
 * unsigned compare) only marks the rows it covers; nothing is paid. */
#include "xeen/addon/training_preview.h"
#include "xeen/town.h"
#include "xeen/character_record.h"
#include "xeen/party.h"
#include <string.h>

#define MEMBER_SIZE XEEN_CHARACTER_BYTES

int xeen_training_ladder_build(const xeen_movement *m,const uint8_t *member,unsigned top,uint32_t gold,
    xeen_training_ladder *out)
{
    uint8_t copy[MEMBER_SIZE];uint32_t total=0;
    if(!m || !member || !out)return 0;
    memset(out,0,sizeof *out);
    memcpy(copy,member,MEMBER_SIZE);
    out->level=member[XEEN_CHARACTER_LEVEL];out->top=top;out->gold=gold;out->afford=member[XEEN_CHARACTER_LEVEL];
    out->refused=xeen_training_refused(member);out->condition=xeen_character_worst_condition(member);
    while(out->count<XEEN_TRAINING_ROWS_MAX){
        xeen_training_row *r=&out->rows[out->count++];
        unsigned level=copy[XEEN_CHARACTER_LEVEL];
        r->level=level+1u;r->cost=xeen_training_cost(copy);
        if(level>=top){r->status=XEEN_TRAIN_ROW_CAP;break;}
        if(xeen_experience_needed(m,copy)){r->status=XEEN_TRAIN_ROW_XP;break;}
        total+=r->cost;r->total=total;
        if(total<=gold){r->status=XEEN_TRAIN_ROW_OK;out->afford=r->level;}
        else r->status=XEEN_TRAIN_ROW_GOLD;
        xeen_train_level(m,copy);
    }
    return 1;
}
