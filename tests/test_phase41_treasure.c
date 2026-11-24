#include "combat_fixture.h"
#include "xeen/treasure.h"
#include <stdlib.h>

static void one_monster(xeen_movement *m,uint8_t *db,uint8_t drop)
{
    static const uint8_t stats[7]={21,10,10,10,30,25,10};
    uint8_t *row;
    fixture_world(m,db);fixture_member(m,0,"Collector",0,1,stats,3,0,50);
    fixture_weapon(m,3,2,4);
    row=fixture_monster(m,db,4,7,10,10,20,1,1,4,8,300);
    row[0x2a]=25;row[0x2c]=2;row[0x2d]=drop;
}

int main(void)
{
    xeen_movement *m=malloc(sizeof *m);uint8_t db[FIXTURE_DB_ROWS*60u];
    xeen_combat c={0};uint32_t boundary=0;uint16_t rec=(uint16_t)(0xe530u+4u*20u);
    static const uint8_t miss_results[]={4,4,10,100};
    static const uint8_t miss_lows[]={1,1,1,1};
    static const uint8_t hit_results[]={4,4,10,1,1,1,1,1};
    static const uint8_t hit_lows[]={1,1,1,1,1,1,1,1};
    static const uint8_t misc_results[]={4,4,10,1,86,1,9,50,15,8};
    static const uint8_t misc_lows[]={1,1,1,1,1,1,1,1,1,1};
    fixture_rng rng;
    CHECK(m);

    /* 36CBA level-1 chance failure consumes only the 1..100 drop roll. */
    one_monster(m,db,1);CHECK(fixture_contact(m,db));
    rng=(fixture_rng){miss_results,miss_lows,4,0,{0}};
    CHECK(xeen_combat_initialize(&c,m,db,sizeof db,fixture_draw,&rng,NULL)==XEEN_SERVICE_REQUIRED);
    CHECK(xeen_combat_command(&c,m,db,sizeof db,fixture_draw,&rng,NULL,'A',&boundary)==XEEN_SERVICE_REQUIRED);
    CHECK(boundary==0x2ea8e && c.phase==XC_AWAITING_TREASURE && rng.at==4);
    CHECK(rng.incoming[3]==10); /* threshold from original ES:[level-1] */
    CHECK(!m->ds[0x9a10] && xeen_read16(m,0x9a16)==25 && !memcmp(m->ds+0x9970,(uint8_t[160]){0},160));
    CHECK(m->ds[rec]==0x80);

    /* The caller loads a general level-indexed threshold, never a selected
     * encounter constant. Phase52: the successful level 4..6 rolls (100 <=
     * 100) generate through the shared 305E8. With the script exhausted
     * every draw returns its low bound: a weapon id 1 of material row 0,
     * whose level column low is 4/5/8 (57C5:A), stored as low + 24h. */
    for(unsigned level=1;level<=6u;++level){
        static const uint8_t thresholds[6]={10,20,50,100,100,100};
        one_monster(m,db,(uint8_t)level);CHECK(fixture_contact(m,db));c=(xeen_combat){0};
        rng=(fixture_rng){miss_results,miss_lows,4,0,{0}};
        CHECK(xeen_combat_initialize(&c,m,db,sizeof db,fixture_draw,&rng,NULL)==XEEN_SERVICE_REQUIRED);
        CHECK(xeen_combat_command(&c,m,db,sizeof db,fixture_draw,&rng,NULL,'A',&boundary)==XEEN_SERVICE_REQUIRED);
        CHECK(rng.at==4 && rng.incoming[3]==thresholds[level-1u]);
        CHECK(boundary==0x2ea8eu);
        if(level>=4u){
            CHECK(m->ds[0x9a10]==1);
            static const uint8_t low[3]={4,5,8};
            CHECK(!memcmp(m->ds+0x99e8,(uint8_t[]){(uint8_t)(0x24u+low[level-4u]),1,0},3));
            memset(m->ds+0x9970,0,0xaa);
        }
    }

    /* Successful roll enters 305E8: category, subcategory, weapon id and
     * enchant rolls. It inserts the generated level-1 weapon exactly once. */
    one_monster(m,db,1);CHECK(fixture_contact(m,db));c=(xeen_combat){0};
    rng=(fixture_rng){hit_results,hit_lows,8,0,{0}};
    CHECK(xeen_combat_initialize(&c,m,db,sizeof db,fixture_draw,&rng,NULL)==XEEN_SERVICE_REQUIRED);
    CHECK(xeen_combat_command(&c,m,db,sizeof db,fixture_draw,&rng,NULL,'A',&boundary)==XEEN_SERVICE_REQUIRED);
    CHECK(boundary==0x2ea8e && c.phase==XC_AWAITING_TREASURE && rng.at==8);
    CHECK(m->ds[0x9a10]==1 && !memcmp(m->ds+0x99e8,(uint8_t[]){0,1,0},3));
    xeen_write16(m,0xf00a,100);xeen_write16(m,0xf00e,5);
    CHECK(xeen_treasure_collect(m,0)==XEEN_OK);
    CHECK(xeen_read16(m,0xf00a)==125 && xeen_read16(m,0xf00e)==7);
    /* Slot zero contains the fixture weapon, so collection uses slot one. */
    CHECK(!memcmp(m->ds + 0xb41e + 0xaa,(uint8_t[]){0,1,0,0},4));
    CHECK(!m->ds[0x9a10] && !memcmp(m->ds+0x9970,(uint8_t[160]){0},160));
    CHECK(xeen_combat_finish_treasure(&c,m,&boundary)==XEEN_SERVICE_REQUIRED);
    CHECK(boundary==0x39430 && c.phase==XC_INACTIVE && c.cleanup_complete==1);
    CHECK(xeen_treasure_collect(m,0)==XEEN_OK); /* cleared state cannot duplicate */
    CHECK(xeen_read16(m,0xf00a)==125 && xeen_read16(m,0xf00e)==7);

    /* Original-byte probe also fixes the misc byte order: initial type,
     * usable subtype, then charges (the final random 8 is stored as 10). */
    one_monster(m,db,1);CHECK(fixture_contact(m,db));c=(xeen_combat){0};
    rng=(fixture_rng){misc_results,misc_lows,10,0,{0}};
    CHECK(xeen_combat_initialize(&c,m,db,sizeof db,fixture_draw,&rng,NULL)==XEEN_SERVICE_REQUIRED);
    CHECK(xeen_combat_command(&c,m,db,sizeof db,fixture_draw,&rng,NULL,'A',&boundary)==XEEN_SERVICE_REQUIRED);
    CHECK(boundary==0x2ea8e && rng.at==10 && m->ds[0x9a10]==1);
    CHECK(rng.incoming[4]==1 && rng.incoming[5]==100 && rng.incoming[6]==1 &&
          rng.incoming[7]==9 && rng.incoming[8]==1 && rng.incoming[9]==15);
    CHECK(!memcmp(m->ds+0x9970,(uint8_t[]){9,15,10},3));

    /* Full weapon pack. Phase50 correction from original 2EEA7 (rules case
     * weapon-category-full): with no free last slot member 0's slot 8 is
     * overwritten (byte 3 kept) and the currency settles. */
    one_monster(m,db,0);
    for(unsigned i=0;i<9u;++i)m->ds[0xb41e + 0xa7u + 4u*i]=1;
    memcpy(m->ds+0x99e8,(uint8_t[]){0,2,0},3);m->ds[0x9a10]=1;
    xeen_write16(m,0x9a16,9);xeen_write16(m,0xf00a,50);
    CHECK(xeen_treasure_collect(m,0)==XEEN_OK);
    CHECK(m->ds[0xb41e + 0xc7]==2 && m->ds[0xb41e + 0xc3]==1 && !m->ds[0x9a10] && !m->ds[0x99e9]);
    CHECK(!xeen_read16(m,0x9a16) && xeen_read16(m,0xf00a)==59);

    free(m);puts("phase41 drop chance, level-1 generation, collection, capacity and exactly-once settlement passed");return 0;
}
