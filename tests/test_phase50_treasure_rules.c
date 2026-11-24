/* Phase50 contracts on SYNTHETIC zero-filled states (rule inputs only; the
 * genuine route and the original-byte rule cases are in
 * test_phase50_connected.py): the 2EA68 gate, the open dialog's writes, the
 * settlement, the 2EEA7/2EC59 collection quirks and the 2FCAB loop. */
#include "xeen/gameplay_tick.h"
#include "xeen/treasure.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"phase50 treasure check line %d\n",__LINE__);return 1;}}while(0)
#define CH(i,off) (0xb41eu+(unsigned)(i)*0x162u+(off))
static uint32_t r32(const xeen_movement *m,unsigned a)
{return (uint32_t)m->ds[a]|(uint32_t)m->ds[a+1u]<<8|(uint32_t)m->ds[a+2u]<<16|(uint32_t)m->ds[a+3u]<<24;}
static void party(xeen_movement *m,unsigned n)
{
    memset(m->ds,0,65536);m->ds[0xed8c]=(uint8_t)n;m->ds[0x9a1c]=1;memset(m->ds+0x98b4,0xff,26);
    /* Age byte 25: 2BD94's age table adds 0 (age 0 would subtract 250). */
    for(unsigned i=0;i<n;++i){m->ds[CH(i,0x26)]=25;for(unsigned k=0;k<7u;++k)m->ds[CH(i,0x14u+2u*k)]=15;}
}
int main(void)
{
    xeen_movement *m=calloc(1,sizeof *m);uint8_t *before=malloc(65536);unsigned placed=9,i;
    CHECK(m && before);
    /* Gate: nothing pending; any of the 26 visible bytes defers; mode 9 does not. */
    party(m,2);CHECK(xeen_treasure_check(m)==XEEN_TREASURE_NONE);
    m->ds[0x9a10]=0x80;CHECK(xeen_treasure_check(m)==XEEN_TREASURE_DIALOG); /* signed byte */
    for(i=0;i<26u;++i){m->ds[0x98b4u+i]=0;CHECK(xeen_treasure_check(m)==XEEN_TREASURE_DEFERRED);m->ds[0x98b4u+i]=0xff;}
    m->ds[0x98c0]=4;m->ds[0x9a1c]=9;CHECK(xeen_treasure_check(m)==XEEN_TREASURE_DIALOG);
    party(m,2);m->ds[0x9a19]=1;CHECK(xeen_treasure_check(m)==XEEN_TREASURE_DIALOG); /* gold high word */
    /* Open: AC0F ends with the 2F14A line; 2EC30 sets 7 except in combat mode 2. */
    party(m,2);m->ds[0x9a12]=6;CHECK(xeen_treasure_open(m)==XEEN_OK && m->ds[0x9a1c]==7);
    CHECK(!strcmp((const char *)m->ds+0xac0f,"\003l\v120\t000\004077\003c\f37Hit a key\fd"));
    m->ds[0x9a1c]=2;CHECK(xeen_treasure_open(m)==XEEN_OK && m->ds[0x9a1c]==2);
    party(m,2);CHECK(xeen_treasure_open(m)==XEEN_BAD_INPUT);
    /* Settle: 2F19D mode 1, currency added once, pending cleared except 9A11. */
    party(m,2);m->ds[0x9a1c]=7;m->ds[0x9a12]=6;m->ds[0x9a16]=0xf4;m->ds[0x9a17]=1;m->ds[0x9a11]=0x5a;
    m->ds[0xf00e]=0xf9;m->ds[0xf00f]=7;xeen_write16(m,0xf5fe,13);
    CHECK(xeen_treasure_settle(m,0,&placed)==XEEN_OK && placed==0 && m->ds[0x9a1c]==1);
    CHECK(r32(m,0xf00e)==2047u && r32(m,0xf00a)==500u && !r32(m,0x9a12) && !r32(m,0x9a16) && m->ds[0x9a11]==0x5a);
    CHECK(!xeen_read16(m,0xf5fe) && m->ds[0x98d7]==1 && xeen_treasure_check(m)==XEEN_TREASURE_NONE);
    CHECK(xeen_treasure_settle(m,0,&placed)==XEEN_OK && r32(m,0xf00e)==2047u); /* nothing twice */
    /* 2EEA7: no member has a free last weapon slot -> member 0 slot 8 overwritten. */
    party(m,2);m->ds[CH(0,0xc6)]=1;m->ds[CH(0,0xc7)]=7;m->ds[CH(0,0xc9)]=0x33;m->ds[CH(1,0xc7)]=8;
    m->ds[0x99e8]=3;m->ds[0x99e9]=5;m->ds[0x99ea]=2;m->ds[0x9a10]=1;
    CHECK(xeen_treasure_collect_placed(m,0,&placed)==XEEN_OK && placed==1);
    /* 43428 then compacts member 0's only weapon into slot 0, byte 3 included. */
    CHECK(m->ds[CH(0,0xa6)]==3 && m->ds[CH(0,0xa7)]==5 && m->ds[CH(0,0xa8)]==2 && m->ds[CH(0,0xa9)]==0x33);
    CHECK(!m->ds[CH(0,0xc6)] && !m->ds[CH(0,0xc7)] && !m->ds[CH(0,0xc9)]);
    CHECK(m->ds[CH(1,0xc7)]==8 && !m->ds[0x99e9] && !m->ds[0x9a10]);
    /* 2EC59: every last slot of every category full -> pending items discarded, currency kept. */
    party(m,2);
    for(i=0;i<2u;++i){m->ds[CH(i,0xc7)]=1;m->ds[CH(i,0xeb)]=1;m->ds[CH(i,0x10f)]=1;m->ds[CH(i,0x132)]=1;}
    m->ds[0x99c1]=4;m->ds[0x9a10]=1;m->ds[0x9a12]=2;memcpy(before,m->ds,65536);
    CHECK(xeen_treasure_collect_placed(m,0,&placed)==XEEN_OK && placed==0 && r32(m,0xf00e)==2u);
    CHECK(!memcmp(m->ds+0xb41e,before+0xb41e,2u*0x162u) && !m->ds[0x99c1]);
    /* ... unless the weapon slot at that index holds 22h: member 0's slot 8 is freed first. */
    memcpy(m->ds,before,65536);m->ds[0x99c1]=0;m->ds[0x99e9]=0x22;m->ds[CH(0,0xc9)]=9;
    CHECK(xeen_treasure_collect_placed(m,0,&placed)==XEEN_OK && placed==1);
    CHECK(m->ds[CH(0,0xa7)]==0x22 && m->ds[CH(0,0xa9)]==0 && !m->ds[CH(0,0xc7)] && m->ds[CH(1,0xc7)]==1);
    /* Quest items (52h+, 2EED7): a recipient pass, the " * name" text, no insertion. */
    party(m,2);m->ds[0x99e9]=0x52;m->ds[0x9a10]=1;m->ds[0x9a1c]=7;m->ds[0x9a12]=1;
    xeen_write16(m,0x2384u+4u*0x52u,0x3000);memcpy(m->ds+0x3000,"Deed",5);memcpy(before,m->ds,65536);
    CHECK(xeen_treasure_settle(m,0,&placed)==XEEN_OK && placed==1 && m->ds[0x9a1c]==1);
    CHECK(!memcmp(before+0xb41e,m->ds+0xb41e,2u*0x162u) && !m->ds[0x99e9] && !m->ds[0x9a10] && r32(m,0xf00e)==1u);
    CHECK(!strcmp((const char *)m->ds+0xa827,"\f04 * \fdDeed"));
    /* An ordinary item's name is copied as is (2EF29). */
    party(m,2);m->ds[0x99e9]=5;m->ds[0x9a10]=1;xeen_write16(m,0x2384u+4u*5u,0x3000);memcpy(m->ds+0x3000,"cutlass ",9);
    CHECK(xeen_treasure_collect(m,0)==XEEN_OK && !strcmp((const char *)m->ds+0xa827,"cutlass "));
    /* 2FCAB: a 480-minute bucket change ages conditions for every member. */
    party(m,2);xeen_write16(m,0xf608,1);xeen_write16(m,0xeff4,470);xeen_write16(m,0x8c92,0);
    m->ds[CH(0,0x144)]=11;m->ds[CH(0,0x149)]=11;m->ds[CH(0,0x14a)]=3;m->ds[CH(0,0x14d)]=1;
    m->ds[CH(1,0x14c)]=3;m->ds[CH(1,0x150)]=0xff;m->ds[CH(1,0x148)]=2;
    {xeen_gameplay_tick t=xeen_gameplay_time_conditions(m,10);
     CHECK(t.boundary!=0x2fcabu && xeen_read16(m,0xeff4)==480u);}
    CHECK(m->ds[CH(0,0x144)]==1 && m->ds[CH(0,0x14c)]==2 && m->ds[CH(0,0x149)]==0); /* love -> broken -> depressed */
    CHECK(m->ds[CH(0,0x145)]==3 && m->ds[CH(0,0x14a)]==0 && m->ds[CH(0,0x14d)]==0); /* drunk -> weak; confusion clears */
    CHECK(m->ds[CH(1,0x14c)]==0 && m->ds[CH(1,0x150)]==0xff && m->ds[CH(1,0x148)]==3);
    /* Within one bucket nothing changes. */
    memcpy(before,m->ds,65536);(void)xeen_gameplay_time_conditions(m,10);
    CHECK(!memcmp(before+0xb41e,m->ds+0xb41e,2u*0x162u));
    /* Poison/disease (504DA + 37A02) need the session RNG: without one 2FCAB
     * stops before any write (Phase51 _rng variants run the draws). */
    party(m,2);xeen_write16(m,0xf608,1);xeen_write16(m,0xeff4,470);m->ds[CH(1,0x146)]=1;memcpy(before,m->ds,65536);
    CHECK(xeen_gameplay_time_conditions(m,10).boundary==0x2fcabu && !memcmp(before,m->ds,65536));
    /* Phase51 (original-byte bucket rule cases): a stat below 1 sets dead at
     * 2FCCF and the same pass ages it to 2; dead members skip the stat check. */
    party(m,2);xeen_write16(m,0xf608,1);xeen_write16(m,0xeff4,470);m->ds[CH(0,0x14)]=0;
    CHECK(xeen_gameplay_time_conditions(m,10).boundary!=0x2fcabu && m->ds[CH(0,0x150)]==2);
    xeen_write16(m,0xeff4,470);m->ds[CH(0,0x150)]=1;
    CHECK(xeen_gameplay_time_conditions(m,10).boundary!=0x2fcabu && m->ds[CH(0,0x150)]==2);
    free(m);free(before);puts("phase50 treasure gate, settlement, collection quirks and 2FCAB passed");return 0;
}
