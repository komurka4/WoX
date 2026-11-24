/* Phase52 contracts on SYNTHETIC zero-filled states (rule inputs only; the
 * genuine route and the original-byte rule cases are in
 * test_phase52_connected.py): the 30004 sky service and its stops, the
 * 30CF3 calendar (day 100 wrap, signed day), the 30D41 blacksmith wares and
 * bank interest, the 30D73 morning (rested, unrested with the 30DB3 stop and
 * its resumption, sleeping, modes 9/17), and the shared 305E8 generator. */
#include "xeen/gameplay_tick.h"
#include "xeen/items.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"phase52 time check line %d: %s\n",__LINE__,#x);return 1;}}while(0)
#define CH(i,off) (0xb41eu+(unsigned)(i)*0x162u+(off))

/* Counts RNG calls; raw 0 makes every 504DA(low,high) return low. */
typedef struct {unsigned calls;uint16_t last_al;} counter;
static uint16_t low_rng(void *user,uint16_t incoming)
{counter *c=user;++c->calls;c->last_al=incoming;return 0;}
/* Test-only 2A0FB stand-in: the clock part of the reload (8C92), success. */
typedef struct {unsigned calls;int fail;} sky;
static int sky_stub(void *user,xeen_movement *m)
{
    sky *s=user;uint16_t minute=xeen_read16(m,0xeff4);++s->calls;
    if(s->fail)return 0;
    xeen_write16(m,0x8c92,(uint16_t)(minute<300u || minute>=1260u));return 1;
}
static void party(xeen_movement *m,unsigned n,uint16_t minute,uint16_t day)
{
    memset(m->ds,0,65536);m->ds[0xed8c]=(uint8_t)n;m->ds[0x9a1c]=1;xeen_write16(m,0xf608,1);
    xeen_write16(m,0xeff4,minute);xeen_write16(m,0xeff0,day);xeen_write16(m,0xeff2,612);
    xeen_write16(m,0x8c92,(uint16_t)(minute<300u || minute>=1260u));
    for(unsigned i=0;i<n;++i){m->ds[CH(i,0x26)]=25;m->ds[CH(i,0x23)]=10;
        for(unsigned k=0;k<7u;++k)m->ds[CH(i,0x14u+2u*k)]=15;}
}
static uint32_t rd32(const xeen_movement *m,uint16_t a)
{return (uint32_t)xeen_read16(m,a)|(uint32_t)xeen_read16(m,(uint16_t)(a+2u))<<16;}
static void wr32(xeen_movement *m,uint16_t a,uint32_t v)
{xeen_write16(m,a,(uint16_t)v);xeen_write16(m,(uint16_t)(a+2u),(uint16_t)(v>>16));}
static uint16_t item_low(void *user,uint16_t low,uint16_t high,uint16_t al)
{counter *c=user;(void)high;++c->calls;c->last_al=al;return low;}
typedef struct {uint16_t high,al;} seen;
static uint16_t item_seen(void *user,uint16_t low,uint16_t high,uint16_t al)
{seen *s=user;if(!s->high){s->high=1;return 1;} if(s->high==1){s->high=high;s->al=al;}return low;}

int main(void)
{
    xeen_movement *m=calloc(1,sizeof *m);counter c;sky k;xeen_gameplay_tick t;
    xeen_gameplay_services services;uint8_t item[3];
    CHECK(m);
    /* Nightfall 1250 + 10: the sky service runs once after 2FF59 and the
     * charge completes; without it the charge stops at 30004 with the clock
     * committed and 8C92 untouched; a failing reload stops the same way. */
    party(m,1,1250,50);k=(sky){0,0};services=(xeen_gameplay_services){low_rng,&c,sky_stub,&k};
    t=xeen_gameplay_time_conditions_services(m,10,&services);
    CHECK(t.status==XT_INTERNAL && !t.boundary && k.calls==1 && xeen_read16(m,0x8c92)==1);
    party(m,1,1250,50);t=xeen_gameplay_time_conditions_rng(m,10,low_rng,&c);
    CHECK(t.boundary==0x30004u && xeen_read16(m,0xeff4)==1260u && !xeen_read16(m,0x8c92));
    party(m,1,1250,50);k=(sky){0,1};
    CHECK(xeen_gameplay_time_conditions_services(m,10,&services).boundary==0x30004u && k.calls==1);
    /* One step short and a repeated charge at night: no reload. */
    party(m,1,1249,50);k=(sky){0,0};
    CHECK(!xeen_gameplay_time_conditions_services(m,10,&services).boundary && !k.calls);
    party(m,1,1300,50);k=(sky){0,0};
    CHECK(!xeen_gameplay_time_conditions_services(m,10,&services).boundary && !k.calls);

    /* Midnight into day 51 (ends in 1): without an RNG the 30D41 tail stops
     * after the 30CF3 rollover; with one, 41D20 makes 160 items (raw 0: every
     * item a weapon, id 1; levels 1 take 4 draws, higher levels 7: 985) and
     * 30DD2 adds value/100 unsigned to bank gold and gems. 9842 is set. */
    party(m,1,1435,50);c=(counter){0,0};
    t=xeen_gameplay_time_conditions(m,10);
    CHECK(t.boundary==0x30d41u && xeen_read16(m,0xeff0)==51u && xeen_read16(m,0xeff4)==5u && !m->ds[0x9842]);
    party(m,1,1435,50);c=(counter){0,0};k=(sky){0,0};
    wr32(m,0xf012,0x12345678u);wr32(m,0xf016,0xffffffffu);memset(m->ds+0xeda8,0x77,0x90);
    t=xeen_gameplay_time_conditions_services(m,10,&services);
    CHECK(!t.boundary && c.calls==985u && m->ds[0x9842]==1 && !k.calls);
    CHECK(rd32(m,0xf012)==0x1262f0eeu && rd32(m,0xf016)==0x028f5c27u);
    /* Clouds slot 0: 15 level-1 then 5 level-2 weapons, only 8 kept. */
    for(unsigned i=0;i<8u;++i)CHECK(!memcmp(m->ds+0xeda8+16u*i,(uint8_t[]){0,1,0,0},4));
    CHECK(!memcmp(m->ds+0xeda8+16u*8u,(uint8_t[]){0,0,0,0},4));
    /* Clouds slot 3: 5 level-1, then level-2 material row 0 low 1 (+24h). */
    CHECK(!memcmp(m->ds+0xeda8+12u+16u*5u,(uint8_t[]){0x25,1,0},3));
    /* Darkside slot 2 uses level idx+3 and has no idx-0 items (5815:14):
     * the first is level 4, material low 4 (57C5:A). */
    CHECK(!memcmp(m->ds+0xf0cc+8u,(uint8_t[]){0x28,1,0},3));
    CHECK(!m->ds[0xee38] && !m->ds[0xef58+1]);
    /* No blacksmith when the day is unchanged, or on a day not ending in 1. */
    party(m,1,700,51);c=(counter){0,0};
    CHECK(!xeen_gameplay_time_conditions_services(m,10,&services).boundary && !c.calls);
    party(m,1,1435,51);c=(counter){0,0};
    CHECK(!xeen_gameplay_time_conditions_services(m,10,&services).boundary && !c.calls &&
          xeen_read16(m,0xeff0)==52u);
    /* Calendar: day 99 -> 0 and year + 1; day 100 -> 1 (signed >= 100) and
     * year + 1, ending in 1; day 7FFF -> 8000 (signed: no wrap; IDIV gives
     * remainder -8, so no blacksmith even without an RNG). */
    party(m,1,1435,99);CHECK(!xeen_gameplay_time_conditions(m,10).boundary &&
        !xeen_read16(m,0xeff0) && xeen_read16(m,0xeff2)==613u);
    party(m,1,1435,100);c=(counter){0,0};
    CHECK(!xeen_gameplay_time_conditions_services(m,10,&services).boundary && xeen_read16(m,0xeff0)==1u &&
          xeen_read16(m,0xeff2)==613u && c.calls==985u);
    party(m,1,1435,0x7fff);CHECK(!xeen_gameplay_time_conditions(m,10).boundary && xeen_read16(m,0xeff0)==0x8000u);

    /* Morning of a new day, rested: 29168 clears the temporaries, F01E is
     * cleared, 9842 too; the night change follows in the same charge. */
    party(m,2,295,52);m->ds[0x9842]=1;m->ds[0xf01e]=1;k=(sky){0,0};
    m->ds[CH(1,0x15)]=3;m->ds[CH(1,0x21)]=3;m->ds[CH(0,0x22)]=4;m->ds[CH(1,0x24)]=5;
    m->ds[CH(0,0x138)]=6;m->ds[CH(1,0x142)]=7;xeen_write16(m,0xeffc,8);xeen_write16(m,0xeff8,9);
    m->ds[0xeda4]=2;m->ds[0xed9e]=1;m->ds[CH(0,0x14)]=15;
    t=xeen_gameplay_time_conditions_services(m,10,&services);
    CHECK(!t.boundary && !m->ds[0x9842] && !m->ds[0xf01e] && k.calls==1 && !xeen_read16(m,0x8c92));
    CHECK(!m->ds[CH(1,0x15)] && !m->ds[CH(1,0x21)] && !m->ds[CH(0,0x22)] && !m->ds[CH(1,0x24)] &&
          !m->ds[CH(0,0x138)] && !m->ds[CH(1,0x142)] && !xeen_read16(m,0xeffc) && !xeen_read16(m,0xeff8) &&
          !m->ds[0xeda4] && !m->ds[0xed9e] && m->ds[CH(0,0x14)]==15);
    /* Before 05:00 nothing happens; 9842 stays set. */
    party(m,1,280,52);m->ds[0x9842]=1;m->ds[0xf01e]=1;
    CHECK(!xeen_gameplay_time_conditions_services(m,10,&services).boundary && m->ds[0x9842]==1 && m->ds[0xf01e]==1);
    /* Unrested: weak + 1 below FFh, then the 30DB3 stop (18F2F scroll)
     * before 9842, 2FF59 and the night test; resumption completes them. */
    party(m,2,295,52);m->ds[0x9842]=1;m->ds[CH(0,0x145)]=0xff;m->ds[CH(1,0x145)]=0xfe;k=(sky){0,0};
    t=xeen_gameplay_tick_wait_services(m,NULL,0,&services);
    CHECK(t.boundary==0x30db3u && m->ds[CH(0,0x145)]==0xff && m->ds[CH(1,0x145)]==0xff);
    CHECK(m->ds[0x9842]==1 && !k.calls && xeen_read16(m,0x8c92)==1 && xeen_read16(m,0xeff4)==305u);
    t=xeen_gameplay_tick_resume_rest(m,t,NULL,0,&services);
    CHECK(t.boundary==0x17264u && !m->ds[0x9842] && k.calls==1 && !xeen_read16(m,0x8c92) && m->ds[0x96c0]==3);
    CHECK(xeen_gameplay_tick_resume_rest(m,t,NULL,0,&services).status==XT_INPUT_ERROR);
    /* Asleep (mode 5): temporaries reset, no weak, no scroll, F01E kept 0. */
    party(m,1,295,52);m->ds[0x9842]=1;m->ds[0x9a1c]=5;m->ds[CH(0,0x15)]=3;
    CHECK(!xeen_gameplay_time_conditions_services(m,10,&services).boundary && !m->ds[CH(0,0x145)] &&
          !m->ds[CH(0,0x15)] && !m->ds[0x9842]);
    /* Modes 9 and 17: no reset, no weak; 9842 is still cleared. */
    party(m,1,295,52);m->ds[0x9842]=1;m->ds[0x9a1c]=17;m->ds[CH(0,0x15)]=3;
    CHECK(!xeen_gameplay_time_conditions_services(m,10,&services).boundary && m->ds[CH(0,0x15)]==3 &&
          !m->ds[CH(0,0x145)] && !m->ds[0x9842]);

    /* 305E8: level 0 draws nothing; level 6 draws the subcategory from
     * 1..80 with AX 80; level 1 misc range 1..15 with AX = low. */
    c=(counter){0,0};party(m,1,600,50);
    CHECK(xeen_item_generate(m,0,item_low,&c,item)==0u && !c.calls);
    {seen s={0,0};(void)xeen_item_generate(m,6,item_seen,&s,item);CHECK(s.high==80u && s.al==80u);}
    {seen s={0,0};(void)xeen_item_generate(m,5,item_seen,&s,item);CHECK(s.high==100u && s.al==100u);}
    free(m);
    puts("phase52 time-of-day, calendar, blacksmith, morning and generator contracts passed");
    return 0;
}
