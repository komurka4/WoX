#include "combat_fixture.h"
#include <stdlib.h>

/* Phase 39/40 lifecycle on the general engine (synthetic fixture): death of
 * one of two attackers (1ED46 compaction, 38D25 rebuild and 97FE re-point),
 * the 36EF2 XP split excluding a dead member, a monster turn, the 37923
 * round end (an incapacitated member counts as gone), the 3935A..393D3
 * reset and its minute, a second-round kill, the 393F0 victory minute and
 * 39430 cleanup, then the 2EA68 treasure-dialog boundary. */
static void party(xeen_movement *m,uint8_t *db,uint16_t second_hp,uint16_t gold)
{
    static const uint8_t strong[7]={21,10,10,10,30,25,10},quick[7]={10,10,10,10,25,10,10},
        dead[7]={10,10,10,10,10,10,10};
    uint8_t *row;
    fixture_world(m,db);
    fixture_member(m,0,"Strong",0,1,strong,3,0,50);
    fixture_member(m,1,"Dead",0,1,dead,3,0,0)[0x143+13]=1;
    fixture_member(m,2,"Quick",0,1,quick,3,0,50);
    fixture_weapon(m,3,2,4);
    row=fixture_monster(m,db,4,7,10,10,20,1,1,4,8,300);
    row[0x2a]=(uint8_t)gold;row[0x2b]=(uint8_t)(gold>>8);
    fixture_monster(m,db,5,8,second_hp,10,20,1,1,4,8,200);
}

int main(void)
{
    xeen_movement *m=malloc(sizeof *m);xeen_combat c={0};uint32_t boundary=0;
    uint8_t db[FIXTURE_DB_ROWS*60u];
    static const uint8_t results[11]={4,4,10, 1,1,2, 0,1, 4,4,10};
    static const uint8_t lows[11]={1,1,1, 1,1,1, 0,1, 1,1,1};
    static const uint8_t al[11]={4,4,1, 4,4,1, 2,0, 4,4,1};
    fixture_rng rng={results,lows,11,0,{0}};
    uint16_t first=0xe530+4*20,second=0xe530+5*20;
    CHECK(m);
    party(m,db,25,0);CHECK(fixture_contact(m,db) && m->ds[0x98b4]==4 && m->ds[0x98b5]==5);
    CHECK(xeen_combat_initialize(&c,m,db,sizeof db,fixture_draw,&rng,NULL)==XEEN_SERVICE_REQUIRED);
    CHECK(!memcmp(m->ds+0x9713,(uint8_t[]){0,2,3,4,1,255},6) && m->ds[0x97ff]==0);
    /* Strong kills record 4 (6+5+1+10 = 22 hits; 24 + Might bonus 5). */
    CHECK(xeen_combat_command(&c,m,db,sizeof db,fixture_draw,&rng,NULL,'A',&boundary)==XEEN_SERVICE_REQUIRED);
    CHECK(boundary==0x3912c && c.monster_deaths==1 && c.last_damage==29);
    CHECK(m->ds[first]==0x80 && m->ds[first+1]==0x80 && !xeen_read16(m,first+18));
    CHECK(!memcmp(m->ds+0x98b4,(uint8_t[]){5,255,255},3) && m->ds[0x306a]==5 && m->ds[0x9804]==0);
    CHECK(!memcmp(m->ds+0x9713,(uint8_t[]){0,2,3,1,255},5));
    CHECK(m->ds[0x97fe]==1 && m->ds[0x97ff]==2 && m->ds[0x9707]==1);
    CHECK(xeen_read16(m,0xb41e + 0x15c)==300 && !xeen_read16(m,0xb580+0x15c) && xeen_read16(m,0xb6e2+0x15c)==300);
    /* Quick misses; record 5 attacks (party of three: target 0..2 with AL 2,
     * then its d20 1 misses); the dead member counts as gone, so the round
     * ends and the original reset runs. */
    CHECK(xeen_combat_command(&c,m,db,sizeof db,fixture_draw,&rng,NULL,'A',&boundary)==XEEN_SERVICE_REQUIRED);
    CHECK(boundary==0x3912c && c.monster_actions==1 && c.monster_hits[0]==0 && c.rounds==1);
    CHECK(!memcmp(m->ds+0x9707,(uint8_t[12]){0},12) && !memcmp(m->ds+0x96fb,(uint8_t[12]){0},12));
    CHECK(m->ds[0x97fe]==0 && m->ds[0x97ff]==0 && xeen_read16(m,0xeff4)==101);
    CHECK(!memcmp(m->ds+0x98b4,(uint8_t[]){5,255,255},3));
    /* Round 2: Strong kills record 5; victory minute and cleanup. */
    CHECK(xeen_combat_command(&c,m,db,sizeof db,fixture_draw,&rng,NULL,'A',&boundary)==XEEN_SERVICE_REQUIRED);
    CHECK(boundary==0x39430 && c.phase==XC_INACTIVE && rng.at==11 && !memcmp(rng.incoming,al,11));
    CHECK(c.monster_deaths==2 && c.rewards_applied==2 && c.cleanup_complete==1);
    CHECK(m->ds[second]==0x80 && xeen_read16(m,0xeff4)==102 && m->ds[0x9a1c]==1);
    CHECK(xeen_read16(m,0xb41e + 0x15c)==500 && xeen_read16(m,0xb6e2+0x15c)==500);
    CHECK(m->ds[0x306a]==0xff && xeen_read16(m,0x199c)==0xec && m->ds[0xfaf8]==1);
    CHECK(m->ds[0x971f]==3 && !memcmp(m->ds+0x9720,(uint8_t[]){0,1,2},3));

    /* Pending gold reaches the 2EA68 treasure dialog, which is explicit. */
    party(m,db,25,50);CHECK(fixture_contact(m,db));
    rng=(fixture_rng){results,lows,11,0,{0}};c=(xeen_combat){0};
    m->ds[(uint16_t)(second)]=9; /* move record 5 away: one attacker */
    CHECK(fixture_contact(m,db) && m->ds[0x98b5]==0xff);
    CHECK(xeen_combat_initialize(&c,m,db,sizeof db,fixture_draw,&rng,NULL)==XEEN_SERVICE_REQUIRED);
    CHECK(xeen_combat_command(&c,m,db,sizeof db,fixture_draw,&rng,NULL,'A',&boundary)==XEEN_SERVICE_REQUIRED);
    CHECK(boundary==0x2ea8e && c.phase==XC_AWAITING_TREASURE && xeen_read16(m,0x9a16)==50);
    free(m);puts("phase39/40 death compaction, XP split, round reset, victory and treasure boundary passed");return 0;
}
