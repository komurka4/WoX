/* Phase 54 synthetic rule contracts (zero-filled DS, no original bytes):
 * the 2E68D party damage (resistance word, halving saves, power shield,
 * type 7, single target, explicit stops), the 186C0 window reset and the
 * 2F869 defeat predicate. Original-byte evidence: xeen_dat_evidence/phase54. */
#include "xeen/combat.h"
#include "xeen/modal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"check failed line %d: %s\n",__LINE__,#x);return 1;}}while(0)
#define CH(m,i) ((m)->ds+0xb41eu+(unsigned)(i)*0x162u)

/* Raw draws for the saving throws: random(1, high) = 1 + raw % high. */
static const uint16_t *draws;static unsigned next_draw;
static uint16_t scripted(void *user,uint16_t incoming){(void)user;(void)incoming;return draws[next_draw++];}

static int16_t hp(const xeen_movement *m,unsigned i){return (int16_t)xeen_read16(m,(uint16_t)(0xb41eu+i*0x162u+0x156u));}
static void party(xeen_movement *m,unsigned n,int16_t points)
{
    unsigned i;
    memset(m->ds,0,sizeof m->ds);m->ds[0xed8c]=(uint8_t)n;m->ds[0x9a1c]=1;
    memset(m->ds+0x98b4,0xff,3);
    for(i=0;i<n;++i){
        CH(m,i)[0x23]=10;CH(m,i)[0x14b]=1;
        xeen_write16(m,(uint16_t)(0xb41eu+i*0x162u+0x156u),(uint16_t)points);
    }
}

int main(void)
{
    static const uint16_t fail_all[16]={39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39};
    static const uint16_t save_twice[3]={0,0,39}; /* 1, 1 (<= 10: saved), 40 (failed) */
    static uint8_t db[60*4];
    xeen_movement *m=calloc(1,sizeof *m);xeen_combat c;uint32_t boundary=1;unsigned i;
    CHECK(m);

    /* Type 0 (physical): no saves, every member, asleep cleared; below 1
     * unconscious; F60A once nobody can act. */
    party(m,3,50);memset(&c,0,sizeof c);draws=fail_all;next_draw=0;
    CHECK(xeen_combat_party_damage(&c,m,db,sizeof db,60,0,0,scripted,NULL,NULL,&boundary)==XEEN_OK && !boundary);
    CHECK(next_draw==0);
    for(i=0;i<3;++i)CHECK(hp(m,i)==-10 && !CH(m,i)[0x14b] && (CH(m,i)[0x14f] || CH(m,i)[0x150]));
    CHECK(m->ds[0xf60a]==1);

    /* Type 2 (fire): the party fire word EFFC is subtracted first; each
     * failed save ends the halving. */
    party(m,2,200);memset(&c,0,sizeof c);xeen_write16(m,0xeffc,10);draws=fail_all;next_draw=0;
    CHECK(xeen_combat_party_damage(&c,m,db,sizeof db,100,2,0,scripted,NULL,NULL,&boundary)==XEEN_OK);
    CHECK(hp(m,0)==110 && hp(m,1)==110 && next_draw==2 && !m->ds[0xf60a]);

    /* A member's fire resistance (ch+137h) 10: rolls 1, 1 save (halve twice),
     * 40 fails: 100 -> 25 for member 0 only (98D7 = 1, target 0). */
    party(m,2,200);memset(&c,0,sizeof c);CH(m,0)[0x137]=10;m->ds[0x98d7]=1;draws=save_twice;next_draw=0;
    CHECK(xeen_combat_party_damage(&c,m,db,sizeof db,100,2,0,scripted,NULL,NULL,&boundary)==XEEN_OK);
    CHECK(hp(m,0)==175 && hp(m,1)==200 && next_draw==3 && CH(m,1)[0x14b]==1);

    /* Power shield EDA4 is subtracted after the saves; never below 0. */
    party(m,1,40);memset(&c,0,sizeof c);m->ds[0xeda4]=30;draws=fail_all;next_draw=0;
    CHECK(xeen_combat_party_damage(&c,m,db,sizeof db,20,0,0,scripted,NULL,NULL,&boundary)==XEEN_OK && hp(m,0)==40);

    /* Type 7: after its (failed) save the damage is the current HP and dead is set. */
    party(m,1,40);memset(&c,0,sizeof c);draws=fail_all;next_draw=0;
    CHECK(xeen_combat_party_damage(&c,m,db,sizeof db,5,7,0,scripted,NULL,NULL,&boundary)==XEEN_OK);
    CHECK(hp(m,0)==0 && CH(m,0)[0x150]==1 && m->ds[0xf60a]==1);

    /* Phase105: the 98D7 = 2 mode (2E6B6) hits the first two able members
     * (asleep is able; 2E73F clears it). Explicit stop before any write:
     * types above 7. */
    party(m,2,50);memset(&c,0,sizeof c);m->ds[0x98d7]=2;draws=fail_all;next_draw=0;
    CHECK(xeen_combat_party_damage(&c,m,db,sizeof db,10,0,0,scripted,NULL,NULL,&boundary)==XEEN_OK);
    CHECK(hp(m,0)==40 && hp(m,1)==40 && CH(m,0)[0x14b]==0 && CH(m,1)[0x14b]==0);
    party(m,2,50);memset(&c,0,sizeof c);
    CHECK(xeen_combat_party_damage(&c,m,db,sizeof db,10,8,0,scripted,NULL,NULL,&boundary)==XEEN_SERVICE_REQUIRED);
    CHECK(boundary==0x2e74fu && hp(m,1)==50);

    /* 186C0: every window named in the 9843 slots 28h..0 is closed and the
     * stack top becomes FFFFh (the slots themselves are kept). */
    memset(m->ds,0,sizeof m->ds);
    xeen_window_open(m,2);xeen_window_open(m,0x15);xeen_window_open(m,6);
    CHECK(m->ds[0x986e] && m->ds[0x9881] && m->ds[0x9872] && xeen_read16(m,0x3064)==3);
    xeen_window_close_all(m);
    CHECK(!m->ds[0x986e] && !m->ds[0x9881] && !m->ds[0x9872] && xeen_read16(m,0x3064)==0xffffu);
    CHECK(m->ds[0x9844]==2 && m->ds[0x9845]==0x15 && m->ds[0x9846]==6);

    /* 2F869 (CS:3F51 table): worst conditions 0..10 and none can act;
     * paralysed, unconscious, dead, stoned and eradicated cannot. */
    for(i=0;i<16;++i){
        party(m,2,10);CH(m,0)[0x150]=1;CH(m,1)[0x143+i]=1;
        xeen_combat_party_check(m);
        CHECK(m->ds[0xf60a]==(i>=11));
    }
    party(m,1,10);xeen_combat_party_check(m);CHECK(!m->ds[0xf60a]);
    free(m);
    puts("Phase54 party damage, window reset and defeat predicate contracts passed");
    return 0;
}
