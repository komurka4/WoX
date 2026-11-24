#include "xeen/event_exec.h"
#include "xeen/event_resources.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
typedef struct { uint8_t *backing,*resident;size_t size;unsigned writes;int suspend; } service_state;
static int service(void *user,const xeen_event_request *r,uint8_t *ds,size_t n,uint16_t *ax)
{
    service_state *s=user;(void)ds;(void)n;(void)ax;
    if(r->address==0x15616u || r->address==0x4a1b7u)return 1;
    if(r->address==0x4a24cu){
        ++s->writes;if(s->suspend){s->suspend=0;return 0;}
        memcpy(s->backing,s->resident,s->size);return 1;
    }
    return 0;
}
int main(void)
{
    /* line0 alters line2 to opcode0; line1 exits; line2 is initially opcode8. */
    uint8_t resident[]={7,1,2,3,0,24,2,0, 5,1,2,3,1,18,
                        8,1,2,3,2,8,9,0,4};
    uint8_t backing[sizeof resident],ds[65536]={0};xeen_event_index index;
    xeen_event_execution execution;service_state state={backing,resident,sizeof resident,0,1};
    memcpy(backing,resident,sizeof resident);ds[0x98b4]=255;ds[0xed97]=1;ds[0xed98]=2;ds[0xed96]=3;
    CHECK(xeen_events_decode(resident,sizeof resident,NULL,0,&index)==XE_OK);
    CHECK(xeen_event_execution_begin(&execution,ds,sizeof ds)==XEE_DONE);
    CHECK(xeen_event_execute_interactive(resident,sizeof resident,&index,ds,sizeof ds,&execution,8,service,&state,NULL,NULL)==XEE_EXTERNAL);
    CHECK(execution.state.boundary==0x4a24cu && execution.phase==7u && resident[index.records[2].offset+5u]==0u);
    CHECK(backing[index.records[2].offset+5u]==8u && state.writes==1u);
    CHECK(xeen_event_execute_interactive(resident,sizeof resident,&index,ds,sizeof ds,&execution,8,service,&state,NULL,NULL)==XEE_EXTERNAL);
    CHECK(execution.state.boundary==0x33690u && execution.state.logical_result==XEE_DONE);
    CHECK(backing[index.records[2].offset+5u]==0u && state.writes==2u && execution.state.steps==2u);

    /* Original32C00 quirk: altering an opcode60 target also changes the next row. */
    {uint8_t rows[]={7,1,2,3,0,24,2,13, 5,1,2,3,1,18,
                     10,1,2,3,2,60,1,2,3,4,5, 6,9,9,4,7,1,0};
     uint8_t backing2[sizeof rows];service_state ss={backing2,rows,sizeof rows,0,0};
     memcpy(backing2,rows,sizeof rows);
     CHECK(xeen_events_decode(rows,sizeof rows,NULL,0,&index)==XE_OK);
     CHECK(xeen_event_execution_begin(&execution,ds,sizeof ds)==XEE_DONE);
     CHECK(xeen_event_execute_interactive(rows,sizeof rows,&index,ds,sizeof ds,&execution,1,service,&ss,NULL,NULL)==XEE_BUDGET);
     CHECK(rows[index.records[2].offset+5u]==13u && rows[index.records[3].offset+5u]==13u && ss.writes==1u);
    }

    /* The actual resource adapter implements inverse handle copies. */
    {uint8_t a[]={1,2,3},b[]={4,5,6};xeen_event_resources r={0};
     xeen_event_request q={0x4a24cu,0,0,0,0};uint16_t ax=0;
     r.backing=a;r.resident=b;r.record_bytes=sizeof a;r.record_count=1;
     CHECK(xeen_event_resource_service(&r,&q,ds,sizeof ds,&ax)==1 && !memcmp(a,b,sizeof a));
     q.address=0x4a1b7u;b[0]=9;CHECK(xeen_event_resource_service(&r,&q,ds,sizeof ds,&ax)==1 && b[0]==4);
    }
    puts("Phase29 AlterEvent and mutable handle-store semantics passed");return 0;
}
