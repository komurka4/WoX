#include "combat_fixture.h"
#include <stdlib.h>

/* Phase 48 rule contracts on the synthetic fixture (not an authentic session):
 * the original Block command 39291 (38B45 sets 96FB[CBW 97FF]; 38633; 3934F),
 * its effect on the 38049 melee defense (AC + level/2 + 15 instead of AC + 10)
 * per combat index, the outdoor round reset 3935A..393D3 (9707/96FB cleared,
 * 38D25, 38633, 2A701, the indoor selector 1ED46 even outdoors, 2FC7E(1)), the
 * 2126C reselection at the next loop top and a second-round action. The genuine
 * slot-3 Ogre encounter is covered by test_phase48_connected.py. */
static const uint8_t fast[7]={15,10,10,10,25,10,10};

/* Outdoor party cell: Zippo (class 5) and Other (class 0), both level 5 and
 * speed 25, AC 6; Fixture Beast record 3 (90 HP, armor 10, speed 5, one 1d8
 * physical attack, hit chance 10). Order [0,1,2]: the monster acts last. */
static int prepare(xeen_movement *m,uint8_t *db,xeen_combat *c,fixture_rng *rng)
{
    fixture_world(m,db);
    xeen_write16(m,0xf608,0x8000);
    fixture_member(m,0,"Zippo",5,5,fast,24,0,60);
    fixture_member(m,1,"Other",0,5,fast,24,0,60);
    fixture_weapon(m,24,2,4);
    fixture_monster(m,db,3,11,90,10,5,1,1,8,10,200);
    CHECK(xeen_scene_outdoor_monsters(m,db,FIXTURE_DB_ROWS*60u)==XEEN_OK && m->ds[0x98b4]==3u);
    *c=(xeen_combat){0};
    CHECK(xeen_combat_initialize(c,m,db,FIXTURE_DB_ROWS*60u,fixture_draw,rng,NULL)==XEEN_SERVICE_REQUIRED);
    CHECK(c->phase==XC_AWAITING_COMMAND && !memcmp(m->ds+0x9713,(uint8_t[]){0,1,2,255},4));
    CHECK(m->ds[0x97ff]==0 && xeen_combat_armor_class(m,m->ds+0xb41e,0)==6u);
    return 0;
}

static int command(xeen_combat *c,xeen_movement *m,uint8_t *db,fixture_rng *rng,uint16_t key,uint32_t expect)
{
    uint32_t boundary=0;
    CHECK(xeen_combat_command(c,m,db,FIXTURE_DB_ROWS*60u,fixture_draw,rng,NULL,key,&boundary)==XEEN_SERVICE_REQUIRED);
    CHECK(boundary==expect);
    return 0;
}

/* Zippo blocks, Other attacks and misses twice (2d4 + d20 = 1 each: score 10
 * vs 20), then the monster's target draw picks `target`: d20 10 + 10/4 +
 * hit roll 5 = 17 against AC 6 + 10 (unblocked) or 6 + 17 (blocked). */
static int one_blocker(xeen_movement *m,uint8_t *db,unsigned target,int expect_hit)
{
    static const uint8_t lows[11]={1,1,1,1,1,1,0,1,1,1,1};
    uint8_t results[11]={1,1,1,1,1,1,0,10,5,4,1};
    fixture_rng rng={results,lows,11,0,{0}};xeen_combat c;uint8_t *other;
    results[6]=(uint8_t)target;
    CHECK(prepare(m,db,&c,&rng)==0);
    CHECK(command(&c,m,db,&rng,'B',0x3912cu)==0);
    CHECK(m->ds[0x96fb]==1 && m->ds[0x96fc]==0 && m->ds[0x97ff]==1 && !rng.at && c.blocks==1);
    CHECK(command(&c,m,db,&rng,'A',0x3912cu)==0);
    other=m->ds+0xb41eu+0x162u;
    CHECK(c.monster_actions==1 && c.monster_targets[0]==target && c.monster_attack_scores[0]==17u);
    CHECK(c.monster_defenses[0]==(target?16u:23u) && c.monster_hits[0]==(uint8_t)expect_hit);
    CHECK(xeen_read16(m,(uint16_t)(0xb41eu+target*0x162u+0x156u))==(expect_hit?56u:60u));
    CHECK(rng.at==(expect_hit?10u:9u) && (target || xeen_read16(m,0xb41eu+0x162u+0x156u)==60u) && other);
    /* The reset cleared both flag arrays and charged one minute. */
    CHECK(c.rounds==1 && !m->ds[0x96fb] && !m->ds[0x96fc] && xeen_read16(m,0xeff4)==101u);
    return 0;
}

int main(void)
{
    xeen_movement *m=malloc(sizeof *m),*before=malloc(sizeof *m);uint8_t db[FIXTURE_DB_ROWS*60u];
    xeen_combat c;uint32_t boundary=0;unsigned i;
    /* Both block; the monster picks Other, d20 10 + 2 + 5 = 17 < 23: miss. */
    static const uint8_t lows[6]={0,1,1,1,1,1},results[6]={1,10,5,2,2,15};
    fixture_rng rng={results,lows,6,0,{0}};
    CHECK(m && before);
    CHECK(prepare(m,db,&c,&rng)==0);
    CHECK(command(&c,m,db,&rng,'B',0x3912cu)==0);
    CHECK(m->ds[0x96fb]==1 && m->ds[0x97fe]==1 && m->ds[0x97ff]==1 && m->ds[0x9707]==1 && !m->ds[0x9708]);
    CHECK(!rng.at && !c.rng_draws && c.commands==1 && c.actions==1 && c.blocks==1 && !c.rounds);
    CHECK(command(&c,m,db,&rng,'B',0x3912cu)==0);
    /* 38633 ran the monster's 38049 turn, then 37923 found everyone gone:
     * 3935A/3936A cleared the arrays, 38D25 rebuilt [0,1,2], 38633 chose
     * Zippo, 2A701 moved nothing, 1ED46 kept record 3 in the party cell,
     * 2FC7E(1) charged one minute and the loop-top 2126C reselected it. */
    CHECK(c.monster_actions==1 && c.monster_targets[0]==1 && c.monster_hits[0]==0);
    CHECK(c.monster_attack_scores[0]==17u && c.monster_defenses[0]==23u && rng.at==3);
    CHECK(xeen_read16(m,0xb41eu+0x162u+0x156u)==60u && xeen_read16(m,0xe530u+3u*20u+18u)==90u);
    CHECK(c.rounds==1 && xeen_read16(m,0xeff4)==101u && m->ds[0x9a1c]==2);
    for(i=0;i<12u;++i)CHECK(!m->ds[0x96fbu+i] && !m->ds[0x9707u+i]);
    CHECK(!memcmp(m->ds+0x9713,(uint8_t[]){0,1,2,255},4) && m->ds[0x97fe]==0 && m->ds[0x97ff]==0);
    CHECK(!memcmp(m->ds+0x98b4,(uint8_t[]){3,255,255},3) && !memcmp(c.attackers,(uint8_t[]){3,255,255},3));
    /* Second round: Zippo's genuine attack (2d4, d20 15: 21 vs 20, hit). */
    CHECK(command(&c,m,db,&rng,'A',0x3912cu)==0);
    CHECK(rng.at==6 && c.last_hit==1 && xeen_read16(m,0xe530u+3u*20u+18u)<90u);
    CHECK(c.monster_actions==1 && c.rounds==1 && m->ds[0x97ff]==1 && m->ds[0x9707]==1);
    /* Block flags index the combat entry: blocking only protects the blocker. */
    CHECK(one_blocker(m,db,1,1)==0);
    CHECK(one_blocker(m,db,0,0)==0);
    /* 38B45 never runs for a non-party 97FF: explicit boundary, DS kept. */
    rng=(fixture_rng){results,lows,6,0,{0}};
    CHECK(prepare(m,db,&c,&rng)==0);
    m->ds[0x97ff]=2;*before=*m;
    CHECK(xeen_combat_command(&c,m,db,FIXTURE_DB_ROWS*60u,fixture_draw,&rng,NULL,'B',&boundary)==XEEN_SERVICE_REQUIRED);
    CHECK(boundary==0x39291u && c.phase==XC_UNSUPPORTED_CONTINUATION && !memcmp(m,before,sizeof *m));
    free(m);free(before);
    puts("phase48 Block, blocked defense, outdoor round reset and second-round action passed");
    return 0;
}
