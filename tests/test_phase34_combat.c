#include "combat_fixture.h"
#include <stdlib.h>

/* Phase 34 melee rules on the general engine (synthetic fixture). The
 * Arturius-shaped actor reproduces the original score composition
 * 6 (Accuracy 25) + 5 (Adventurer hit) + floor(5/2) + d20 recorded at
 * xeen_dat_evidence/phase34 and the original AL value at every draw. */
static void party(xeen_movement *m,uint8_t *db)
{
    static const uint8_t arturius[7]={19,13,13,19,28,25,14},tyro[7]={31,10,8,27,25,18,16};
    fixture_world(m,db);
    fixture_member(m,0,"Arturius",1,5,arturius,22,0,85);
    fixture_member(m,1,"Tyro",0,6,tyro,10,0,133);
    fixture_weapon(m,22,4,5);fixture_weapon(m,10,2,3);
    fixture_monster(m,db,18,11,30,10,20,2,2,12,10,100);
}

int main(void)
{
    xeen_movement *m=malloc(sizeof *m);xeen_combat c={0};uint32_t boundary=0;
    uint8_t db[FIXTURE_DB_ROWS*60u];
    static const uint8_t miss[5]={1,3,5,3,3},lows[8]={1,1,1,1,1,1,1,1};
    static const uint8_t kill[6]={1,3,5,3,20,11};
    fixture_rng rng={miss,lows,5,0,{0}};
    CHECK(m);
    party(m,db);CHECK(fixture_contact(m,db));
    CHECK(xeen_combat_initialize(&c,m,db,sizeof db,fixture_draw,&rng,NULL)==XEEN_SERVICE_REQUIRED);
    CHECK(m->ds[0x97ff]==0 && !memcmp(m->ds+0x9713,(uint8_t[]){0,1,2,255},4));
    CHECK(xeen_combat_stat_bonus(m,xeen_combat_stat(m,m->ds+0xb41e,5u,0))==6);
    CHECK(xeen_combat_command(&c,m,db,sizeof db,fixture_draw,&rng,NULL,'A',&boundary)==XEEN_SERVICE_REQUIRED);
    CHECK(boundary==0x3912c && c.phase==XC_AWAITING_COMMAND && rng.at==5 && c.rng_draws==5);
    CHECK(!memcmp(rng.incoming,(uint8_t[]){5,5,5,5,2},5)); /* sides, then level/divisor */
    CHECK(c.last_attack_score==16 && c.last_attack_threshold==20 && !c.last_hit && c.player_attacks==1);
    CHECK(m->ds[0x96e4]==5 && m->ds[0x96e5]==4 && xeen_read16(m,0x96e6)==36 && xeen_read16(m,0x96e8)==5);
    CHECK(m->ds[0x96ed]==0 && m->ds[0x96ee]==22);
    CHECK(m->ds[0x9707]==1 && m->ds[0x97fe]==1 && m->ds[0x97ff]==1 && m->ds[0x96c3]==1);
    CHECK(xeen_read16(m,0xe530+18*20+18)==30 && c.actions==1 && c.commands==1);

    /* 3889A: a natural 20 is rolled again (AL 20) and added, giving 44; the
     * Might 19 bonus 4 plus prepared 36 is lethal. Death, XP, the 393F0
     * victory minute and 39430 cleanup follow in the same command. */
    party(m,db);CHECK(fixture_contact(m,db));
    rng=(fixture_rng){kill,lows,6,0,{0}};c=(xeen_combat){0};
    CHECK(xeen_combat_initialize(&c,m,db,sizeof db,fixture_draw,&rng,NULL)==XEEN_SERVICE_REQUIRED);
    CHECK(xeen_combat_command(&c,m,db,sizeof db,fixture_draw,&rng,NULL,'A',&boundary)==XEEN_SERVICE_REQUIRED);
    CHECK(boundary==0x39430 && c.phase==XC_INACTIVE && rng.at==6);
    CHECK(!memcmp(rng.incoming,(uint8_t[]){5,5,5,5,2,20},6));
    CHECK(c.last_attack_score==44 && c.last_damage==40 && c.monster_deaths==1 && c.rewards_applied==1);
    CHECK(m->ds[0xe530+18*20]==0x80 && m->ds[0xe531+18*20]==0x80 && !xeen_read16(m,0xe530+18*20+18));
    CHECK(xeen_read16(m,0xb41e + 0x15c)==100 && xeen_read16(m,0xb580 + 0x15c)==100);
    CHECK(xeen_read16(m,0xeff4)==101 && c.cleanup_complete==1);
    CHECK(!memcmp(m->ds+0x98b4,(uint8_t[]){255,255,255},3) && m->ds[0x9a1c]==1);
    CHECK(xeen_read16(m,0x199c)==0xec && m->ds[0x306a]==0xff && m->ds[0xfaf8]==1 && m->ds[0x96c3]==0xff);
    CHECK(xeen_read16(m,0xfafa)==40 && m->ds[0x96ef]==0);
    free(m);puts("phase34 melee score, AL sequence, natural-20 re-roll and victory passed");return 0;
}
