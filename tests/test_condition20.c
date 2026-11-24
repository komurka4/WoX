#include "xeen/event_exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"condition20 line%d: %s\n",__LINE__,#c);exit(1);}}while(0)
static int service(void *u,const xeen_event_request *r,uint8_t *ds,size_t n,uint16_t *ax)
{(void)u;(void)n;(void)ax;if(r->address==0x15616){ds[r->destination]=0;return 1;}return r->address==0x4a1b7;}
static uint8_t ds[65536];
static void run(unsigned world,unsigned value,unsigned set,unsigned count,unsigned target,xeen_exec_status expected)
{
    uint8_t record[]={8,14,5,3,0,9,20,(uint8_t)value,(uint8_t)target};
    xeen_event_index idx;xeen_event_execution e;
    CHECK(xeen_events_decode(record,sizeof record,NULL,0,&idx)==XE_OK);
    memset(ds,0,sizeof ds);ds[0x94]=(uint8_t)world;ds[0xed8c]=(uint8_t)count;
    ds[0xed97]=14;ds[0xed98]=5;ds[0xed96]=3;ds[0x98b4]=255;
    unsigned off=0xf01fu+(world*256u+value)/8u;   /* 2C0CF: world * 256 + n, any world */
    ds[off]=(uint8_t)(set?(0x80u>>(value&7u)):0);
    CHECK(xeen_event_execution_begin(&e,ds,sizeof ds)==XEE_DONE);
    CHECK(xeen_event_execute_interactive(record,sizeof record,&idx,ds,sizeof ds,&e,1,service,NULL,NULL,NULL)==expected);
    if(expected==XEE_EXTERNAL || expected==XEE_BUDGET){CHECK(e.state.steps==1);CHECK(e.state.sequence==(set && count?target:1));CHECK(e.state.event_flag);}
    else {CHECK(!e.state.steps && e.state.boundary==0x2c0cf);}
    CHECK(ds[off]==(set?(0x80u>>(value&7u)):0));
}
int main(void)
{
    for(unsigned world=0;world<2;++world)for(unsigned value=0;value<256;++value)for(unsigned set=0;set<2;++set)
        run(world,value,set,6,255,XEE_EXTERNAL);
    run(0,9,1,0,10,XEE_EXTERNAL);run(0,9,1,6,0,XEE_BUDGET);
    /* Phase93: the original has no world or party-size guard (2C0CF reads
     * bit world * 256 + n; 31B5C loops over ED8C members). */
    run(2,9,1,6,10,XEE_EXTERNAL);run(0,9,1,9,10,XEE_EXTERNAL);
    /* Other conditions/comparisons and reward branches must never become
     * favorable successful operations as a side effect of condition20 support. */
    for(unsigned kind=0;kind<3;++kind){
        uint8_t record[]={8,14,5,3,0,9,20,9,10};xeen_event_index idx;xeen_event_execution e;
        /* Phase93: every 2BF6E condition is native; 2F2FF with a value
         * other than 0 or 1 still stops (31590). */
        if(kind==0)record[6]=44;
        else if(kind==1){record[5]=8;record[6]=44;}
        else record[5]=12;
        memset(ds,0,sizeof ds);ds[0xed97]=14;ds[0xed98]=5;ds[0xed96]=3;ds[0xed8c]=6;
        CHECK(xeen_events_decode(record,sizeof record,NULL,0,&idx)==XE_OK);
        CHECK(xeen_event_execution_begin(&e,ds,sizeof ds)==XEE_DONE);
        xeen_exec_status expected=kind==2?XEE_MALFORMED:XEE_UNSUPPORTED;
        CHECK(xeen_event_execute_interactive(record,sizeof record,&idx,ds,sizeof ds,&e,1,service,NULL,NULL,NULL)==expected);
        CHECK((kind==2 || e.state.boundary==0x31590) && !e.state.steps && e.state.sequence==0);
        CHECK(!ds[0xf020]);
    }
    puts("Condition20: all256 byte operands in both worlds, MSB indexing, false/true, empty party, target0/255 and invalid state passed");return 0;
}
