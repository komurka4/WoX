#include "combat_fixture.h"
#include <stdlib.h>

/* Phase 37 damage rules on the general engine (synthetic fixture): 38AAC
 * Might bonus with its zero clamp, 36738 physical resistance and hit-slot
 * state, the 371E2 slayer triple, and the 36FB5 elemental extra damage
 * (an explicit 36FE3 boundary until Phase 53). */
typedef struct {uint8_t might,dice,sides,material,slayer,resist,type;uint16_t hp;} setup;

static uint8_t *prepare(xeen_movement *m,uint8_t *db,setup s)
{
    uint8_t sorcerer[7]={0,26,13,17,30,15,15};static const uint8_t other[7]={10,10,10,10,25,10,10};
    uint8_t *ch,*row;
    sorcerer[0]=s.might;
    fixture_world(m,db);
    ch=fixture_member(m,0,"Seymour",4,5,sorcerer,24,s.material,50);
    ch[0xa8]=s.slayer;
    fixture_member(m,1,"Other",0,5,other,24,0,50);
    fixture_weapon(m,24,s.dice,s.sides);
    row=fixture_monster(m,db,9,11,s.hp,10,20,2,2,12,10,400);
    row[0x28]=s.resist;row[0x21]=s.type;
    return ch;
}

static unsigned scenes_before;  /* combat scenes before the command */
static int attack(xeen_movement *m,uint8_t *db,xeen_combat *c,const uint8_t *results,
    unsigned count,uint32_t *boundary)
{
    static const uint8_t lows[8]={1,1,1,1,1,1,1,1};
    fixture_rng rng={results,lows,count,0,{0}};
    *c=(xeen_combat){0};
    if(!fixture_contact(m,db) ||
       xeen_combat_initialize(c,m,db,FIXTURE_DB_ROWS*60u,fixture_draw,&rng,NULL)!=XEEN_SERVICE_REQUIRED ||
       m->ds[0x97ff]!=0)return 0;
    scenes_before=c->scenes;
    if(xeen_combat_command(c,m,db,FIXTURE_DB_ROWS*60u,fixture_draw,&rng,NULL,'A',boundary)!=XEEN_SERVICE_REQUIRED)return 0;
    return rng.at==count;
}

int main(void)
{
    xeen_movement *m=malloc(sizeof *m);xeen_combat c;uint32_t boundary=0;uint8_t db[FIXTURE_DB_ROWS*60u];
    static const uint8_t hit[3]={3,3,15};
    uint16_t record=0xe530+9*20;
    CHECK(m);
    /* Score 2+5+floor(5/4)+15=23 hits AC 20; 18 prepared + Might bonus -2 =
     * 16, halved by 50% physical resistance. */
    prepare(m,db,(setup){8,2,4,0,0,50,1,30});
    CHECK(attack(m,db,&c,hit,3,&boundary) && boundary==0x3912c);
    CHECK(c.last_attack_score==23 && c.last_hit==1 && c.last_damage==16);
    CHECK(xeen_read16(m,record+18)==22 && xeen_read16(m,0xfafa)==8);
    /* 36738 writes 96EF=3; the 1B503 effects stage (1BBDF) counts it down on
     * each following scene call, reaching 0 before 3912C (Phase 47 correction;
     * original countdown in phase47/indoor/pre-phase47-tiger-*.json). */
    /* 368B6 sets the hit animation (+2 = 0Bh, +10 = 5); Phase80: every
     * following combat scene runs 1B0F6 as the original's does, which keeps
     * frame 0Bh and counts +10 down once per pass. */
    CHECK(c.scenes>scenes_before && c.scenes-scenes_before<5u);
    CHECK(m->ds[0x96ef]==0 && m->ds[0x97b2]==1 && m->ds[record+2]==11 && m->ds[record+10]==5u-(c.scenes-scenes_before));
    CHECK(m->ds[0x97ff]==1);
    /* A slayer kind (a8 & 0F = 5 -> monster type 0) triples the total. */
    prepare(m,db,(setup){8,2,4,0,5,0,0,60});
    CHECK(attack(m,db,&c,hit,3,&boundary) && boundary==0x3912c);
    CHECK(c.last_damage==48 && xeen_read16(m,record+18)==12);
    /* 38AAC clamps a nonpositive result to zero (not one): 1d1*3 + Might 2
     * bonus -5. The zero transaction takes the 36AAC miss path. */
    prepare(m,db,(setup){2,1,1,0,0,0,1,30});
    CHECK(attack(m,db,&c,(const uint8_t[]){1,15},2,&boundary) && boundary==0x3912c);
    CHECK(c.last_attack_score==23 && c.last_damage==0 && c.last_hit==0);
    CHECK(xeen_read16(m,record+18)==30 && m->ds[0x96ef]==0);
    /* An elemental weapon material (96ED 5, category 0) adds the 20D9[5]
     * extra damage scaled by the monster's category-0 resistance (row 22h):
     * 20*(100-25)/100 = 15, marking the slot's element (97A6 = 1) and size
     * (9776 = 0). 36FB5, reconstructed in Phase 53 (original-byte verified
     * on the genuine Evil Ranger fight). */
    prepare(m,db,(setup){8,2,4,5,0,0,1,60});
    m->ds[0x20d9+5]=20;db[11u*60u+0x22u]=25;
    CHECK(attack(m,db,&c,hit,3,&boundary) && boundary==0x3912c);
    CHECK(m->ds[0x96ed]==5 && c.last_damage==16 && xeen_read16(m,0xfafa)==31);
    CHECK(xeen_read16(m,record+18)==29 && m->ds[0x97a6]==1 && xeen_read16(m,0x9776)==0);
    /* An immune monster (100) gets no extra; a melee hit clears 97A6. */
    prepare(m,db,(setup){8,2,4,5,0,0,1,60});
    m->ds[0x20d9+5]=20;db[11u*60u+0x22u]=100;
    CHECK(attack(m,db,&c,hit,3,&boundary) && boundary==0x3912c);
    CHECK(xeen_read16(m,0xfafa)==16 && xeen_read16(m,record+18)==44 && m->ds[0x97a6]==0);
    free(m);puts("phase37 Might, resistance, slayer, zero clamp and elemental damage passed");return 0;
}
