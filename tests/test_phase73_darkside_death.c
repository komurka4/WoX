#include "combat_fixture.h"
#include <stdlib.h>

/* Phase73 (synthetic rule fixtures, see combat_fixture.h): the world branches
 * of the monster death 36AD1 and of 36738/36C4B. DS 94 nonzero jumps from
 * 36AD8 straight to the common 36B7A, so the Clouds quest kills (36ADD..
 * 36B77) and the Clouds-only id 59h rules (3674A/36787, 36C4B) never apply on
 * Darkside. The connected Darkside kills are original-byte verified in
 * xeen_dat_evidence/phase73; these cases pin the branches no route reaches. */
#define ROWS 96u

static uint16_t one_monster(xeen_movement *m,uint8_t *db,uint8_t world,uint8_t record,uint8_t id,uint8_t map)
{
    static const uint8_t stats[7]={21,10,10,10,30,25,10};
    uint8_t *row;
    fixture_world(m,db);memset(db,0,ROWS*60u);
    m->ds[0x94]=world;m->ds[0xed99]=map;xeen_write16(m,0xf46a+8,map);
    /* Phase109: both worlds present (DS 95) and the map's monster archive
     * 8C50 = the world, as on every genuine non-special map: combat's 38FD0
     * 191DE(8C50) keeps DS 94 (with DS 95 0, 191DE would select 1). */
    m->ds[0x95]=1;xeen_write16(m,0x8c50,world);
    fixture_member(m,0,"Collector",0,1,stats,3,0,50);
    fixture_weapon(m,3,2,4);
    row=fixture_monster(m,db,record,id,10,10,20,1,1,4,8,300);
    row[0x2a]=25;row[0x2c]=2;row[0x2d]=0;
    return (uint16_t)(0xe530u+20u*record);
}

static int kill(xeen_movement *m,uint8_t *db,uint32_t *boundary,xeen_combat *c)
{
    static const uint8_t results[]={4,4,10};
    static const uint8_t lows[]={1,1,1};
    fixture_rng rng={results,lows,3,0,{0}};
    *c=(xeen_combat){0};
    /* fixture_contact with this database's size (ids up to 59h). */
    CHECK(xeen_scene_monsters(m,db,ROWS*60u)==XEEN_OK && m->ds[0x98b4]!=0xffu);
    CHECK(xeen_combat_initialize(c,m,db,ROWS*60u,fixture_draw,&rng,NULL)==XEEN_SERVICE_REQUIRED);
    (void)xeen_combat_command(c,m,db,ROWS*60u,fixture_draw,&rng,NULL,'A',boundary);
    return 0;
}

int main(void)
{
    xeen_movement *m=malloc(sizeof *m);uint8_t *db=malloc(ROWS*60u);
    xeen_combat c;uint32_t boundary=0;uint16_t rec;
    CHECK(m && db);

    /* An ordinary Darkside kill: 36B7A's experience, gold and gems; no drop. */
    rec=one_monster(m,db,1,4,7,3);CHECK(!kill(m,db,&boundary,&c));
    CHECK(boundary==0x2ea8eu && c.monster_deaths==1 && m->ds[rec]==0x80);
    CHECK(xeen_read16(m,0x9a16)==25 && xeen_read16(m,0x9a12)==2 && !m->ds[0x9a10]);

    /* Phase123: the Clouds quest kills (36ADD..36B7A, original-byte compared
     * in xeen_dat_evidence/phase123): record 1Ch on map 29h sets flag 0Bh
     * (F01F bit 11) and dies as any monster; the same record and map on
     * Darkside is an ordinary death that leaves the flag alone. */
    rec=one_monster(m,db,0,0x1c,7,0x29);m->ds[0xf020]=0;CHECK(!kill(m,db,&boundary,&c));
    CHECK(boundary==0x2ea8eu && c.monster_deaths==1 && m->ds[rec]==0x80 && m->ds[0xf020]==0x10);
    rec=one_monster(m,db,1,0x1c,7,0x29);m->ds[0xf020]=0;CHECK(!kill(m,db,&boundary,&c));
    CHECK(boundary==0x2ea8eu && c.monster_deaths==1 && m->ds[rec]==0x80 && !m->ds[0xf020]);
    /* Record 8 on map 4Eh: flag 3Ch set, quest 17h cleared (F06F bit 23),
     * award 2Ah (+39h + 2Ah |= 1) for each of the ED8C members only. */
    rec=one_monster(m,db,0,8,7,0x4e);m->ds[0xf026]=0;m->ds[0xf071]=0xff;m->ds[0xed8c]=2;
    for(unsigned k=0;k<3u;++k)m->ds[0xb41eu+k*0x162u+0x63u]=0x40;
    CHECK(!kill(m,db,&boundary,&c));
    CHECK(boundary==0x2ea8eu && m->ds[rec]==0x80 && m->ds[0xf026]==0x08 && m->ds[0xf071]==0xfe);
    CHECK(m->ds[0xb41eu+0x63u]==0x41 && m->ds[0xb41eu+0x162u+0x63u]==0x41 && m->ds[0xb41eu+2u*0x162u+0x63u]==0x40);
    /* Record 1Bh on map 1Dh: flag 68h (F02C bit 0x08). */
    rec=one_monster(m,db,0,0x1b,7,0x1d);m->ds[0xf02c]=0;CHECK(!kill(m,db,&boundary,&c));
    CHECK(boundary==0x2ea8eu && m->ds[rec]==0x80 && m->ds[0xf02c]==0x80);

    /* Monster id 59h: on Clouds (Phase98) a melee hit takes 36787's +100
     * and the kill 36C65's weapon 5Ah in the first pending row, 9A10 set,
     * F07F incremented; Darkside takes neither 36787 nor 36C4B (no item 5Ah
     * pending, F07F untouched). The Clouds rules are original-byte verified
     * in xeen_dat_evidence/phase98 (Lord Xeen scenarios). */
    rec=one_monster(m,db,0,4,0x59,3);m->ds[0xf07f]=0;CHECK(!kill(m,db,&boundary,&c));
    CHECK(c.monster_deaths==1 && m->ds[rec]==0x80);
    CHECK(m->ds[0x9a10]==1 && m->ds[0x99e9]==0x5a && !m->ds[0x99e8] && !m->ds[0x99ea] && m->ds[0xf07f]==1);
    rec=one_monster(m,db,1,4,0x59,3);m->ds[0xf07f]=0;CHECK(!kill(m,db,&boundary,&c));
    CHECK(boundary==0x2ea8eu && c.monster_deaths==1 && m->ds[rec]==0x80);
    CHECK(!m->ds[0x9a10] && !m->ds[0x99e9] && !m->ds[0xf07f]);
    free(m);free(db);
    puts("phase73 Darkside death branches passed");
    return 0;
}
