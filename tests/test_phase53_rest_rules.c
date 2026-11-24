/* Phase 53 synthetic rule contracts (zero-filled DS, no original bytes):
 * the 4152D wake test of a sleeping party after a ranged delivery, the
 * 1871B/18765 window bookkeeping, the shared 29168 reset and the 46470
 * maximum-SP formula. Original-byte evidence: xeen_dat_evidence/phase53. */
#include "xeen/combat.h"
#include "xeen/gameplay_tick.h"
#include "xeen/modal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"check failed line %d: %s\n",__LINE__,#x);return 1;}}while(0)
#define CH(m,i) ((m)->ds+0xb41eu+(unsigned)(i)*0x162u)

static void party(xeen_movement *m,unsigned n)
{
    unsigned i;
    memset(m->ds,0,sizeof m->ds);m->ds[0xed8c]=(uint8_t)n;
    for(i=0;i<n;++i)CH(m,i)[0x14b]=1; /* asleep, as Rest leaves them */
}

int main(void)
{
    xeen_movement *m=calloc(1,sizeof *m);uint32_t boundary=1;uint8_t *ch;
    CHECK(m);
    /* 4152D: every member asleep keeps mode 5; 97F6/97F8 are cleared. */
    party(m,3);m->ds[0x9a1c]=5;m->ds[0x97f6]=1;m->ds[0x97f8]=1;
    CHECK(xeen_combat_ranged_complete(m,&boundary)==XEEN_OK && boundary==0);
    CHECK(m->ds[0x9a1c]==5 && !m->ds[0x97f6] && !m->ds[0x97f8]);
    /* A hit member (asleep cleared, no condition: worst 16) wakes the party. */
    party(m,3);m->ds[0x9a1c]=5;CH(m,1)[0x14b]=0;
    CHECK(xeen_combat_ranged_complete(m,&boundary)==XEEN_OK && m->ds[0x9a1c]==1);
    /* Poisoned and awake (worst 3, below 8) wakes it too (ScummVM omits 0..7). */
    party(m,2);m->ds[0x9a1c]=5;CH(m,0)[0x14b]=0;CH(m,0)[0x146]=1;
    CHECK(xeen_combat_ranged_complete(m,&boundary)==XEEN_OK && m->ds[0x9a1c]==1);
    /* Depressed (9) or confused (10) outranks asleep and wakes it. */
    party(m,2);m->ds[0x9a1c]=5;CH(m,1)[0x14c]=1;
    CHECK(xeen_combat_ranged_complete(m,&boundary)==XEEN_OK && m->ds[0x9a1c]==1);
    party(m,2);m->ds[0x9a1c]=5;CH(m,1)[0x14d]=1;
    CHECK(xeen_combat_ranged_complete(m,&boundary)==XEEN_OK && m->ds[0x9a1c]==1);
    /* Paralysed, unconscious or dead members (11..15) do not. */
    party(m,3);m->ds[0x9a1c]=5;CH(m,0)[0x14b]=0;CH(m,0)[0x14e]=1;CH(m,1)[0x14f]=1;CH(m,2)[0x150]=1;
    CHECK(xeen_combat_ranged_complete(m,&boundary)==XEEN_OK && m->ds[0x9a1c]==5);
    /* Outside mode 5 nothing changes the mode. */
    party(m,1);m->ds[0x9a1c]=1;CH(m,0)[0x14b]=0;
    CHECK(xeen_combat_ranged_complete(m,&boundary)==XEEN_OK && m->ds[0x9a1c]==1);
    /* 2F869 in the same call: everyone at 11..15 sets F60A. */
    party(m,2);m->ds[0x9a1c]=1;CH(m,0)[0x150]=1;CH(m,1)[0x14e]=1;
    CHECK(xeen_combat_ranged_complete(m,&boundary)==XEEN_OK && m->ds[0xf60a]==1);

    /* 1871B/18765: the open flag, the 3064 stack top and the 9843 slot; a
     * second open or a close of a closed window changes nothing; 18765
     * leaves the slot. */
    memset(m->ds,0,sizeof m->ds);
    xeen_window_open(m,2);
    CHECK(m->ds[0x986e]==1 && xeen_read16(m,0x3064)==1 && m->ds[0x9844]==2);
    xeen_window_open(m,10);xeen_window_open(m,10);
    CHECK(m->ds[0x9876]==1 && xeen_read16(m,0x3064)==2 && m->ds[0x9845]==10);
    xeen_window_close(m,10);xeen_window_close(m,10);
    CHECK(!m->ds[0x9876] && xeen_read16(m,0x3064)==1 && m->ds[0x9845]==10);
    xeen_window_close(m,2);
    CHECK(!m->ds[0x986e] && xeen_read16(m,0x3064)==0);

    /* 29168, shared by the morning step and Rest. */
    memset(m->ds,0,sizeof m->ds);m->ds[0xed8c]=2;
    CH(m,1)[0x15]=3;CH(m,1)[0x22]=4;CH(m,1)[0x24]=5;CH(m,1)[0x142]=6;CH(m,1)[0x14]=9;
    m->ds[0xed9e]=1;m->ds[0xeda6]=2;xeen_write16(m,0xeffc,7);xeen_write16(m,0xf002,8);m->ds[0xeda7]=9;
    xeen_gameplay_reset_temps(m);
    CHECK(!CH(m,1)[0x15] && !CH(m,1)[0x22] && !CH(m,1)[0x24] && !CH(m,1)[0x142] && CH(m,1)[0x14]==9);
    CHECK(!m->ds[0xed9e] && !m->ds[0xeda6] && !xeen_read16(m,0xeffc) && !xeen_read16(m,0xf002) && m->ds[0xeda7]==9);

    /* 46470 with an all-zero DS (stat bonus table 0, stats 0, race table 0):
     * (0 + 3 [+2 skill]) per level, halved except classes 3, 4 and 8;
     * classes 8 and 9 average the Personality and Intellect passes; no
     * ch+A3 gives 0. */
    memset(m->ds,0,sizeof m->ds);m->ds[0xed8c]=1;ch=CH(m,0);
    ch[0xa3]=1;ch[0x23]=10;ch[0x13]=4;
    CHECK(xeen_combat_max_sp(m,ch)==30);
    ch[0x34]=1;CHECK(xeen_combat_max_sp(m,ch)==50);
    ch[0x13]=1;CHECK(xeen_combat_max_sp(m,ch)==15);  /* skill byte 33h unset: 3*10/2 */
    ch[0x33]=1;CHECK(xeen_combat_max_sp(m,ch)==25);
    ch[0x13]=9;ch[0x29]=1;CHECK(xeen_combat_max_sp(m,ch)==25); /* (25 + 25) / 2 */
    ch[0x13]=8;CHECK(xeen_combat_max_sp(m,ch)==50);
    ch[0xa3]=0;CHECK(xeen_combat_max_sp(m,ch)==0);
    free(m);puts("phase53 wake test, windows, temporaries and maximum SP passed");return 0;
}
