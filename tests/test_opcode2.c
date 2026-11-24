#include "xeen/event_exec.h"
#include "xeen/gameplay_tick.h"
#include "xeen/program_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"opcode2 line%d: %s\n",__LINE__,#c);exit(1);}}while(0)
typedef struct {unsigned copies,scenes,length,selector;int ready,missing;} Service;
static int service(void *user,const xeen_event_request *r,uint8_t *ds,size_t n,uint16_t *ax)
{
    Service *s=user;(void)n;(void)ax;
    if(r->address==0x15616){
        ++s->copies;s->selector=r->argument;if(s->missing)return 0;
        memset(ds+r->destination,'A',s->length);ds[r->destination+s->length]=0;return 1;
    }
    if(r->address==0x1b503){CHECK(r->argument==1 && !r->second_argument);++s->scenes;return s->ready;}
    if(r->address==0x4a1b7)return 1;
    return 0;
}
int main(void)
{
    /* Phase 295: each DS holds the program data (the sign formats, DS 6326.., are read from it as the original
     * does), then the case's state. */
    static uint8_t ds[65536],held[65536];xeen_event_execution e;xeen_event_index idx;
    for(unsigned selector=0;selector<256;++selector){
        uint8_t record[]={6,13,4,3,255,2,(uint8_t)selector};Service s={0,0,18,0,0,0};
        CHECK(xeen_events_decode(record,sizeof record,NULL,0,&idx)==XE_OK);
        memset(ds,0,sizeof ds);memcpy(ds,xeen_program_ds_image,XEEN_PROGRAM_DS_SIZE);ds[0xed97]=13;ds[0xed98]=4;ds[0xed96]=3;ds[0x98b4]=255;
        CHECK(xeen_event_execution_begin(&e,ds,sizeof ds)==XEE_DONE);e.state.sequence=255;
        CHECK(xeen_event_execute_interactive(record,sizeof record,&idx,ds,sizeof ds,&e,1,service,&s,NULL,NULL)==XEE_EXTERNAL);
        CHECK(s.copies==1 && s.selector==selector && s.scenes==1 && e.phase==6 && !e.state.steps);
        memcpy(held,ds,sizeof ds);
        for(unsigned i=0;i<8;++i){
            CHECK(xeen_event_execute_interactive(record,sizeof record,&idx,ds,sizeof ds,&e,1,service,&s,NULL,NULL)==XEE_EXTERNAL);
            CHECK(s.copies==1 && !memcmp(held,ds,sizeof ds) && e.state.sequence==255 && !e.state.steps);
        }
        s.ready=1;
        CHECK(xeen_event_execute_interactive(record,sizeof record,&idx,ds,sizeof ds,&e,1,service,&s,NULL,NULL)==XEE_EXTERNAL);
        CHECK(e.state.steps==1 && e.state.sequence==256 && s.copies==1 && e.state.event_flag);
    }
    /* Phase157: 318AE's room is the original's 98DA..996F (150 bytes; op 2's
     * format adds 20), not 64: 130 fits, 131 is the explicit stop. */
    for(unsigned length=130;length<=131;++length){
        uint8_t record[]={6,13,4,3,0,2,33};Service s={0,0,length,0,0,0};
        CHECK(xeen_events_decode(record,sizeof record,NULL,0,&idx)==XE_OK);
        memset(ds,0,sizeof ds);memcpy(ds,xeen_program_ds_image,XEEN_PROGRAM_DS_SIZE);ds[0xed97]=13;ds[0xed98]=4;ds[0xed96]=3;
        CHECK(xeen_event_execution_begin(&e,ds,sizeof ds)==XEE_DONE);
        CHECK(xeen_event_execute_interactive(record,sizeof record,&idx,ds,sizeof ds,&e,1,service,&s,NULL,NULL)==XEE_EXTERNAL);
        CHECK(e.state.boundary==(length==130?0x1b503u:0x318aeu));
        CHECK(!e.state.steps && s.scenes==(length==130?1u:0u));
    }
    {uint8_t record[]={6,13,4,3,0,2,255};Service s={0,0,0,0,0,1};
        CHECK(xeen_events_decode(record,sizeof record,NULL,0,&idx)==XE_OK);
        CHECK(xeen_event_execution_begin(&e,ds,sizeof ds)==XEE_DONE);
        CHECK(xeen_event_execute_interactive(record,sizeof record,&idx,ds,sizeof ds,&e,1,service,&s,NULL,NULL)==XEE_EXTERNAL);
        CHECK(e.state.boundary==0x15616 && !s.scenes && !e.state.steps);
    }
    puts("Opcode2 unsigned selectors, record sequence255, suspension ownership, missing copy and overlay capacity passed");return 0;
}
