/* Phase51 contracts on SYNTHETIC zero-filled states (rule inputs only; the
 * genuine route and the original-byte rule cases are in
 * test_phase51_connected.py): the 2FCAB poison/disease and 2FF59
 * confusion/paralysis draws with a scripted RNG (cure, doubling, the SHL
 * wrap, the AL each draw sees), the RNG-less stops, the 37A02 bounds and
 * 2AAC2. */
#include "xeen/combat.h"
#include "xeen/gameplay_tick.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"phase51 condition check line %d\n",__LINE__);return 1;}}while(0)
#define CH(i,off) (0xb41eu+(unsigned)(i)*0x162u+(off))
typedef struct {uint16_t raw[16];uint16_t al[16];unsigned n;} script;
/* 504DA(1,high) returns 1 + (raw & 7FFF) % high: raw r-1 yields r. */
static uint16_t scripted(void *user,uint16_t incoming)
{
    script *s=user;s->al[s->n]=incoming;return s->raw[s->n++];
}
static void party(xeen_movement *m,unsigned n)
{
    memset(m->ds,0,65536);m->ds[0xed8c]=(uint8_t)n;m->ds[0x9a1c]=1;xeen_write16(m,0xf608,1);
    xeen_write16(m,0xeff4,470);
    /* Age byte 25: 2BD94's age table adds 0. Stats 15, level 10. */
    for(unsigned i=0;i<n;++i){m->ds[CH(i,0x26)]=25;m->ds[CH(i,0x23)]=10;
        for(unsigned k=0;k<7u;++k)m->ds[CH(i,0x14u+2u*k)]=15;}
}
int main(void)
{
    xeen_movement *m=calloc(1,sizeof *m);uint8_t *before=malloc(65536);script s;uint16_t high,threshold;
    xeen_gameplay_tick t;
    CHECK(m && before);
    /* 37A02 bounds: type 0 is level + 2C92E(2 x Luck) against +20; types 1..6
     * add the resistance pair and the 2BAFA item bonus against +40. */
    party(m,1);
    threshold=xeen_combat_save_threshold(m,m->ds+CH(0,0),0,&high);
    CHECK(threshold==(uint16_t)(10+xeen_combat_stat_bonus(m,30)) && high==threshold+20u);
    m->ds[CH(0,0x13b)]=3;m->ds[CH(0,0x13c)]=2;
    CHECK(xeen_combat_save_threshold(m,m->ds+CH(0,0),3,&high)==5u && high==45u);
    CHECK(xeen_combat_save_threshold(m,m->ds+CH(0,0),9,&high)==9u && high==49u); /* caller SI */
    /* No RNG: a poisoned member stops 2FCAB before any write. */
    party(m,2);m->ds[CH(1,0x146)]=1;memcpy(before,m->ds,65536);
    CHECK(xeen_gameplay_time_conditions(m,10).boundary==0x2fcabu && !memcmp(before,m->ds,65536));
    /* Roll 4: poison doubles; the draw sees AL 0 (2FD41). */
    party(m,2);m->ds[CH(1,0x146)]=1;s=(script){{3},{0},0};
    t=xeen_gameplay_time_conditions_rng(m,10,scripted,&s);
    CHECK(t.boundary!=0x2fcabu && s.n==1 && s.al[0]==0 && m->ds[CH(1,0x146)]==2 && xeen_read16(m,0xeff4)==480u);
    /* Roll 1 and a won save cure the poison; the disease draw then sees the
     * 37A02 return (AL 1). Roll 1 and a lost save double, AL 0. */
    party(m,1);m->ds[CH(0,0x146)]=3;m->ds[CH(0,0x147)]=5;m->ds[CH(0,0x13b)]=5;s=(script){{0,0,0,0},{0},0};
    threshold=xeen_combat_save_threshold(m,m->ds+CH(0,0),3,&high);
    s.raw[1]=0;                                  /* save roll 1 <= threshold */
    s.raw[2]=0;                                  /* disease: roll 1 */
    s.raw[3]=(uint16_t)(high-1u);                /* save roll = high > threshold */
    t=xeen_gameplay_time_conditions_rng(m,10,scripted,&s);
    CHECK(s.n==4 && s.al[0]==0 && s.al[1]==(uint16_t)(high&255u) && s.al[2]==1);
    CHECK(m->ds[CH(0,0x146)]==0 && m->ds[CH(0,0x147)]==10);
    /* Disease after a doubled poison sees the poison roll in AL. */
    party(m,1);m->ds[CH(0,0x146)]=1;m->ds[CH(0,0x147)]=1;s=(script){{6,8},{0},0};
    (void)xeen_gameplay_time_conditions_rng(m,10,scripted,&s);
    CHECK(s.n==2 && s.al[1]==7 && m->ds[CH(0,0x146)]==2 && m->ds[CH(0,0x147)]==2);
    /* SHL wrap: 80h becomes 0; FFh becomes FEh. */
    party(m,2);m->ds[CH(0,0x146)]=0x80;m->ds[CH(1,0x146)]=0xff;s=(script){{1,1},{0},0};
    (void)xeen_gameplay_time_conditions_rng(m,10,scripted,&s);
    CHECK(m->ds[CH(0,0x146)]==0 && m->ds[CH(1,0x146)]==0xfe);
    /* Within one bucket nothing is drawn. */
    party(m,1);xeen_write16(m,0xeff4,400);m->ds[CH(0,0x146)]=1;s=(script){{0},{0},0};
    (void)xeen_gameplay_time_conditions_rng(m,10,scripted,&s);
    CHECK(s.n==0 && m->ds[CH(0,0x146)]==1);
    /* 2FF59 on every charge: without RNG a confused member stops at 2FF6A.
     * With it: draws enter with AL = party size; roll 1 and a won type-0
     * save clear confusion (the paralysis draw would then see AL 1); a lost
     * save decrements; paralysis decrements on random(1,5) == 1. */
    party(m,2);xeen_write16(m,0xeff4,400);m->ds[CH(0,0x14d)]=2;
    CHECK(xeen_gameplay_time_conditions(m,10).boundary==0x2ff6au && m->ds[CH(0,0x14d)]==2);
    /* Phase 284: without RNG a paralysed (not confused) member stops at 2FFB8, its paralysis unchanged; the member
     * before it, neither, is passed. */
    party(m,2);xeen_write16(m,0xeff4,400);m->ds[CH(1,0x14e)]=3;
    t=xeen_gameplay_time_conditions(m,10);
    CHECK(t.boundary==0x2ffb8u && t.status==XT_EXTERNAL && m->ds[CH(1,0x14e)]==3 && m->ds[CH(0,0x14e)]==0);
    party(m,2);xeen_write16(m,0xeff4,400);m->ds[CH(0,0x14d)]=2;m->ds[CH(0,0x14e)]=1;m->ds[CH(1,0x14e)]=3;
    s=(script){{0,0,4,0},{0},0};
    t=xeen_gameplay_time_conditions_rng(m,10,scripted,&s);
    CHECK(t.status==XT_INTERNAL && s.n==4 && s.al[0]==2 && s.al[2]==1 && s.al[3]==2);
    CHECK(m->ds[CH(0,0x14d)]==0 && m->ds[CH(0,0x14e)]==1 && m->ds[CH(1,0x14e)]==2);
    party(m,1);xeen_write16(m,0xeff4,400);m->ds[CH(0,0x14d)]=2;s=(script){{0,0},{0},0};
    threshold=xeen_combat_save_threshold(m,m->ds+CH(0,0),0,&high);s.raw[1]=(uint16_t)(high-1u);
    (void)xeen_gameplay_time_conditions_rng(m,10,scripted,&s);
    CHECK(s.n==2 && m->ds[CH(0,0x14d)]==1);
    /* 2AAC2: at least 1 per level before the item bonus. */
    party(m,1);m->ds[CH(0,0x13)]=0;memset(m->ds+0x18b8,0,16);memset(m->ds+0x299a,0x80,8);
    CHECK(xeen_combat_max_hp(m,m->ds+CH(0,0))==10u);
    free(m);free(before);puts("phase51 2FCAB poison/disease and 2FF59 confusion/paralysis draws (2FF6A/2FFB8 stops), 37A02 bounds and 2AAC2 passed");return 0;
}
