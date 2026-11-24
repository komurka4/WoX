#include "combat_fixture.h"
#include <stdlib.h>

/* Phase 49 rule contracts on the synthetic fixture (not an authentic session):
 * 39721 decides shooting from the character's missile slot (location +A9 == 4)
 * and its worst condition through the 3976A table (8 asleep and 11..15 cannot,
 * 9 depressed and 10 confused can); xeen_combat_shoot rejects unusable inputs
 * before any write. Genuine volleys, the tier walk and the 38F5F-first contact
 * are covered by test_phase49_connected.py against original-byte evidence. */
int main(void)
{
    xeen_movement *m=malloc(sizeof *m),*before=malloc(sizeof *m);uint8_t db[FIXTURE_DB_ROWS*60u];
    static const uint8_t stats[7]={15,10,10,10,15,10,10};
    xeen_combat c;xeen_sprite_cache cache;xeen_sprite_archives none={{NULL,NULL},NULL};
    fixture_rng rng={NULL,NULL,0,0,{0}};uint32_t boundary=0;unsigned condition;uint8_t *ch;
    CHECK(m && before);
    fixture_world(m,db);
    ch=fixture_member(m,0,"Archer",8,5,stats,24,0,40);
    CHECK(!xeen_combat_has_missile(ch)); /* melee hand slot only */
    ch[0xa6u+4u*3u+3u]=4;                /* a bow equipped in the fourth weapon slot */
    CHECK(xeen_combat_has_missile(ch));
    for(condition=0;condition<16u;++condition){
        int able=condition!=8u && (condition<11u || condition>15u);
        memset(ch+0x143,0,16);ch[0x143u+condition]=1;
        CHECK(xeen_combat_has_missile(ch)==able);
    }
    /* The worst (highest) condition decides: depressed plus asleep cannot. */
    memset(ch+0x143,0,16);ch[0x143u+9u]=1;ch[0x143u+8u]=1;
    CHECK(xeen_combat_has_missile(ch));
    ch[0x143u+12u]=1;CHECK(!xeen_combat_has_missile(ch));
    CHECK(!xeen_combat_has_missile(NULL));
    /* Rejections leave DS untouched: no archives, more than eight members. */
    xeen_sprite_cache_init(&cache);memset(&c,0,sizeof c);*before=*m;
    CHECK(xeen_combat_shoot(&c,m,db,FIXTURE_DB_ROWS*60u,&cache,NULL,fixture_draw,&rng,NULL,&boundary)==XEEN_BAD_INPUT);
    m->ds[0xed8c]=9;*before=*m;
    CHECK(xeen_combat_shoot(&c,m,db,FIXTURE_DB_ROWS*60u,&cache,&none,fixture_draw,&rng,NULL,&boundary)==XEEN_BAD_INPUT);
    CHECK(!memcmp(m,before,sizeof *m) && !rng.at);
    free(m);free(before);
    puts("phase49 39721 shooter conditions and Shoot input rejection passed");
    return 0;
}
