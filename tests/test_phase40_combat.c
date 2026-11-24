#include "xeen/character_record.h"
#include "combat_fixture.h"
#include <stdlib.h>

/* Phase 40 rule helpers (synthetic fixtures): 2BD94 stat with 2BAC7 age
 * rows, 2BAFA item scan and 2A2E8 condition modifiers; 4660F armor class;
 * 2AB96 worst condition; 38D25 order construction, ties, packed attacker
 * entries and the 97FE re-point. */
int main(void)
{
    static const uint8_t stats[7]={20,20,20,20,20,20,20};
    xeen_movement *m=malloc(sizeof *m);uint8_t db[FIXTURE_DB_ROWS*60u];uint8_t *ch;
    CHECK(m);
    fixture_world(m,db);
    ch=fixture_member(m,0,"Rules",0,5,stats,1,0,50);
    /* Age 18: bucket 4 adjusts by zero. */
    CHECK(xeen_combat_stat(m,ch,0,0)==20 && xeen_combat_stat(m,ch,1,0)==20);
    /* Age 60 (birth 550): bucket 6 is -5 on row 0 and +5 on row 1 (Int,
     * Per); Luck has no age row. */
    xeen_write16(m,0xb41e + 0x15a,550);
    CHECK(xeen_combat_stat(m,ch,0,0)==15 && xeen_combat_stat(m,ch,1,0)==25 &&
          xeen_combat_stat(m,ch,2,0)==25 && xeen_combat_stat(m,ch,6,0)==20);
    /* The year difference caps at 254 (bucket 9, -50): the stat clamps. */
    xeen_write16(m,0xb41e + 0x15a,0);
    CHECK(xeen_combat_stat(m,ch,0,0)==0 && xeen_combat_stat(m,ch,1,0)==70);
    xeen_write16(m,0xb41e + 0x15a,592);
    /* Temporary bytes are added unless base-only. */
    ch[0x15]=3;CHECK(xeen_combat_stat(m,ch,0,0)==23 && xeen_combat_stat(m,ch,0,1)==20);ch[0x15]=0;
    /* 2BAFA: material 79 is attribute category 2 (Personality); 89 is
     * category 3, shifted past Endurance to Speed. Broken items do not count. */
    m->ds[0x211b+79]=7;m->ds[0x211b+89]=(uint8_t)-4;
    ch[0xa6+4]=79;ch[0xa9+4]=1;ch[0xa6+8]=89;ch[0xa9+8]=1;
    CHECK(xeen_combat_stat(m,ch,2,0)==27 && xeen_combat_stat(m,ch,4,0)==16 && xeen_combat_stat(m,ch,3,0)==20);
    ch[0xa8+4]=0x40;CHECK(xeen_combat_stat(m,ch,2,0)==20);ch[0xa8+4]=0;ch[0xa9+4]=0;ch[0xa9+8]=0;
    /* 2A2E8: Weak lowers all seven; Drunk mixes signs; Dead/Stone/Eradicated
     * suppress every modifier. Base-only ignores conditions. */
    ch[0x143+2]=2;CHECK(xeen_combat_stat(m,ch,0,0)==18 && xeen_combat_stat(m,ch,6,0)==18);
    CHECK(xeen_combat_stat(m,ch,0,1)==20);ch[0x143+2]=0;
    ch[0x143+7]=1;CHECK(xeen_combat_stat(m,ch,0,0)==19 && xeen_combat_stat(m,ch,2,0)==21 &&
                        xeen_combat_stat(m,ch,6,0)==21);
    ch[0x143+13]=1;CHECK(xeen_combat_stat(m,ch,0,0)==20);
    /* 2AB96 worst condition is the highest nonzero index, else 16. */
    CHECK(xeen_character_worst_condition(ch)==13);ch[0x143+7]=0;ch[0x143+13]=0;
    CHECK(xeen_character_worst_condition(ch)==16);
    ch[0x143+3]=1;ch[0x143+10]=2;CHECK(xeen_character_worst_condition(ch)==10);
    ch[0x143+3]=0;ch[0x143+10]=0;
    /* 4660F: Speed 20 bonus 4 + armor (2242 type + metal 2105) + EDA3 +
     * unsigned AC temporary. */
    CHECK(xeen_combat_armor_class(m,ch,0)==4);
    m->ds[0x2242+5]=3;m->ds[0x2105+40]=(uint8_t)-1;ch[0xca]=40;ch[0xcb]=5;ch[0xcd]=1;
    CHECK(xeen_combat_armor_class(m,ch,0)==6);
    m->ds[0xeda3]=2;ch[0x22]=0xf0;
    CHECK(xeen_combat_armor_class(m,ch,0)==248 && xeen_combat_armor_class(m,ch,1)==6);
    m->ds[0xeda3]=0;ch[0x22]=0;

    /* 38D25: party speeds 20/15 and attacker speeds 20/25/15. Ties keep the
     * lower entry; the three present slots occupy entries 2,3,4. */
    {
        static const uint8_t second[7]={20,20,20,20,15,20,20};
        fixture_member(m,1,"Second",0,5,second,1,0,50);
        fixture_monster(m,db,2,1,30,4,20,1,1,4,8,10);
        fixture_monster(m,db,3,2,30,4,25,1,1,4,8,10);
        fixture_monster(m,db,4,3,30,4,15,1,1,4,8,10);
        m->ds[0x98b4]=2;m->ds[0x98b5]=3;m->ds[0x98b6]=4;
        m->ds[0x971f]=2;m->ds[0x9720]=0;m->ds[0x9721]=1;m->ds[0x97fe]=0xff;
        CHECK(xeen_combat_build_order(m,db,sizeof db)==XEEN_OK);
        CHECK(!memcmp(m->ds+0x9713,(uint8_t[]){3,0,2,1,4,255},6));
        /* With entry 2 (index 2 in the old order) active, a rebuild that
         * moves it re-points 97FE to its new position. */
        m->ds[0x97fe]=2;db[1*60+0x17]=30;
        CHECK(xeen_combat_build_order(m,db,sizeof db)==XEEN_OK);
        CHECK(!memcmp(m->ds+0x9713,(uint8_t[]){2,3,0,1,4,255},6) && m->ds[0x97fe]==0);
        CHECK(xeen_combat_build_order(m,db,2u*60u)==XEEN_SERVICE_REQUIRED); /* missing row */
    }
    free(m);puts("phase40 stat, item, condition, armor, worst-condition and order rules passed");return 0;
}
