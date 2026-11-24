#include "combat_fixture.h"
#include <stdlib.h>

/* Phase 35/36 rules on the general engine (synthetic fixture): Knight
 * attack count level/5+1, the 38049 random target chain (1d6 with AL
 * party-1, then 0..4 with AL equal to the 1d6 result), the monster d20 AL
 * (normalized row offset), the natural-20 quirk (an automatic 37B6D hit and
 * then the ordinary roll, which can hit again) and the natural 1 miss. */
static void party(xeen_movement *m,uint8_t *db,int16_t target_hp)
{
    static const uint8_t knight[7]={14,10,10,10,30,18,10},slow[7]={14,10,10,10,10,10,10};
    unsigned i;
    fixture_world(m,db);
    fixture_member(m,0,"Tyro",0,6,knight,10,0,133);
    for(i=1;i<6u;++i)fixture_member(m,i,"Member",0,5,slow,10,0,i==5u?target_hp:60);
    fixture_weapon(m,10,2,3);
    fixture_monster(m,db,18,11,40,10,20,2,2,12,10,400);
}

int main(void)
{
    xeen_movement *m=malloc(sizeof *m);xeen_combat c={0};uint32_t boundary=0;
    uint8_t db[FIXTURE_DB_ROWS*60u];
    static const uint8_t results[17]={2,3,2, 3,1,2, 6,20,7,5,4,9,2, 3,2,1, 0};
    static const uint8_t lows[17]={1,1,1, 1,1,1, 1,1,1,1,1,1,1, 1,0,1, 0};
    static const uint8_t al[16]={3,3,6, 3,3,6, 5,4,12,12,10,12,12, 5,3,4};
    fixture_rng rng={results,lows,16,0,{0}};
    CHECK(m);
    party(m,db,60);CHECK(fixture_contact(m,db));
    CHECK(xeen_combat_initialize(&c,m,db,sizeof db,fixture_draw,&rng,NULL)==XEEN_SERVICE_REQUIRED);
    CHECK(!memcmp(m->ds+0x9713,(uint8_t[]){0,6,1,2,3,4,5,255},8) && m->ds[0x97ff]==0);
    CHECK(xeen_combat_command(&c,m,db,sizeof db,fixture_draw,&rng,NULL,'A',&boundary)==XEEN_SERVICE_REQUIRED);
    CHECK(boundary==0x3912c && c.phase==XC_AWAITING_COMMAND && rng.at==16);
    CHECK(!memcmp(rng.incoming,al,16));
    CHECK(c.player_attacks==2 && !c.last_hit && c.monster_actions==1);
    /* Natural 20 against member 5: 7+5 automatic, then 20+2+4=26 against
     * AC 0+10 hits again for 9+2. The second attack's natural 1 misses. */
    CHECK(c.monster_targets[0]==5 && c.monster_hits[0]==1 && c.monster_attack_scores[0]==26);
    CHECK(c.monster_defenses[0]==10 && c.monster_targets[1]==2 && c.monster_hits[1]==0);
    CHECK(xeen_read16(m,0xb41e + 5*0x162+0x156)==60-12-11);
    CHECK(xeen_read16(m,0xb41e + 2*0x162+0x156)==60);
    CHECK(m->ds[0xb41e + 5*0x162+0x14b]==0); /* 37B6D wakes the target */
    CHECK(m->ds[0x97ff]==1 && m->ds[0x9707]==1 && m->ds[0x970d]==1 && c.actions==2);
    CHECK(xeen_read16(m,0xe530+18*20+18)==40);

    /* Phase51 2A17D (its branches are original-byte rule cases in
     * xeen_dat_evidence/phase51/special-rules.json): the automatic hit drops
     * member 5 to -2, unconscious (12); the natural-20 quirk's ordinary roll
     * hits the same member again to -13, below -max HP: dead (13). The second
     * attack re-targets an able member and the round reaches the next poll. */
    party(m,db,10);CHECK(fixture_contact(m,db));
    rng=(fixture_rng){results,lows,16,0,{0}};c=(xeen_combat){0};
    CHECK(xeen_combat_initialize(&c,m,db,sizeof db,fixture_draw,&rng,NULL)==XEEN_SERVICE_REQUIRED);
    CHECK(xeen_combat_command(&c,m,db,sizeof db,fixture_draw,&rng,NULL,'A',&boundary)==XEEN_SERVICE_REQUIRED);
    CHECK(boundary==0x3912c && c.phase==XC_AWAITING_COMMAND && rng.at==16 && c.party_downed==2);
    CHECK((int16_t)xeen_read16(m,0xb41e + 5*0x162+0x156)==-13);
    CHECK(m->ds[0xb41e + 5*0x162+0x14f]==1 && m->ds[0xb41e + 5*0x162+0x150]==1);
    CHECK(c.monster_targets[0]==5 && c.monster_targets[1]==2 && c.monster_hits[1]==0);
    free(m);puts("phase35/36 attack count, monster targeting, natural-20 quirk and 2A17D unconscious/death passed");return 0;
}
