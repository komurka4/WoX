/* Phase 126 synthetic rule contracts (zero-filled DS, no original bytes):
 * 2E68D split at its 2E8B3 30E2D(2) for the callers that run that scene as a
 * full exploration scene (the surface callers, operation 22). The damage part
 * (xeen_combat_party_damage_hits) and the tail's 2F869
 * (xeen_combat_party_check) give the members, the RNG draws and F60A of the
 * whole semantic routine, and the hits leave F60A to the tail; the explicit
 * stops are the routine's. Original-byte evidence: xeen_dat_evidence/phase126
 * (the phase126 family: draw calls, NPC windows and scene calls). */
#include "xeen/combat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"check failed line %d: %s\n",__LINE__,#x);return 1;}}while(0)
#define CH(m,i) ((m)->ds+0xb41eu+(unsigned)(i)*0x162u)

static const uint16_t *draws;static unsigned next_draw;
static uint16_t scripted(void *user,uint16_t incoming){(void)user;(void)incoming;return draws[next_draw++];}

static void party(xeen_movement *m,unsigned n,int16_t points)
{
    unsigned i;
    memset(m->ds,0,sizeof m->ds);m->ds[0xed8c]=(uint8_t)n;m->ds[0x9a1c]=1;
    memset(m->ds+0x98b4,0xff,3);
    for(i=0;i<n;++i){
        CH(m,i)[0x23]=10;CH(m,i)[0x14b]=1;CH(m,i)[0x137]=(uint8_t)(5u*i);
        xeen_write16(m,(uint16_t)(0xb41eu+i*0x162u+0x156u),(uint16_t)points);
    }
}
/* The members' records and F60A after the semantic routine and after the
 * split one, from the same state and draws. */
static int same(xeen_movement *a,xeen_movement *b,unsigned n,uint32_t damage,unsigned type,unsigned target,
    uint8_t mode,const uint16_t *script)
{
    static uint8_t db[60*4];xeen_combat c;uint32_t boundary=1;unsigned da,i;
    party(a,n,60);a->ds[0x98d7]=mode;memcpy(b->ds,a->ds,sizeof a->ds);
    memset(&c,0,sizeof c);draws=script;next_draw=0;
    CHECK(xeen_combat_party_damage(&c,a,db,sizeof db,damage,type,target,scripted,NULL,NULL,&boundary)==XEEN_OK && !boundary);
    da=next_draw;
    memset(&c,0,sizeof c);next_draw=0;
    CHECK(xeen_combat_party_damage_hits(&c,b,db,sizeof db,damage,type,target,scripted,NULL,NULL,NULL,&boundary)==XEEN_OK && !boundary);
    CHECK(next_draw==da && b->ds[0xf60a]==0);                       /* 2F869 is the tail's */
    xeen_combat_party_check(b);
    for(i=0;i<n;++i)CHECK(!memcmp(CH(a,i),CH(b,i),0x162));
    CHECK(a->ds[0xf60a]==b->ds[0xf60a]);
    return 0;
}

int main(void)
{
    static const uint16_t fail_all[16]={39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39};
    static const uint16_t save_some[8]={0,0,39,39,0,39,39,39};
    static uint8_t db[60*4];
    xeen_movement *a=calloc(1,sizeof *a),*b=calloc(1,sizeof *b);xeen_combat c;uint32_t boundary=0;
    CHECK(a && b);

    CHECK(!same(a,b,3,100,0,0,0,fail_all));       /* the fall: 100 type 0 to every member (all down: F60A) */
    CHECK(a->ds[0xf60a]==1);
    CHECK(!same(a,b,3,20,0,0,0,fail_all));        /* survivable: F60A stays 0 */
    CHECK(a->ds[0xf60a]==0);
    CHECK(!same(a,b,2,50,3,0,0,save_some));       /* op 22's type 3 with saves (halvings) */
    CHECK(!same(a,b,3,30,2,1,1,fail_all));        /* 98D7 = 1: the target only */
    CHECK(!same(a,b,3,30,0,0,2,fail_all));        /* 98D7 = 2: the first two able members */
    CHECK(!same(a,b,1,5,7,0,0,fail_all));         /* type 7: current HP, dead */

    /* The routine's explicit stops, before any write. */
    party(a,2,50);memset(&c,0,sizeof c);
    CHECK(xeen_combat_party_damage_hits(&c,a,db,sizeof db,10,8,0,scripted,NULL,NULL,NULL,&boundary)==XEEN_SERVICE_REQUIRED);
    CHECK(boundary==0x2e74fu && (int16_t)xeen_read16(a,0xb41eu+0x162u+0x156u)==50);
    party(a,2,50);a->ds[0x98d7]=1;memset(&c,0,sizeof c);
    CHECK(xeen_combat_party_damage_hits(&c,a,db,sizeof db,10,0,5,scripted,NULL,NULL,NULL,&boundary)==XEEN_BAD_INPUT);
    CHECK(boundary==0x2e72bu);
    CHECK(xeen_combat_party_damage_hits(NULL,a,db,sizeof db,10,0,0,scripted,NULL,NULL,NULL,&boundary)==XEEN_BAD_INPUT);
    free(a);free(b);
    puts("phase126 presentation rules: ok");
    return 0;
}
