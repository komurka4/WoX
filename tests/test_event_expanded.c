#include "xeen/event_exec.h"
#include "xeen/event_resources.h"
#include "xeen/event_text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static uint8_t ds[65536];
static xeen_event_index index_rows;
static xeen_event_execution execution;
static xeen_event_text_resource texts;
static struct { int ui, sound, text_calls, ui_calls, sound_calls, fail_text, check_state; uint32_t expected_address; uint8_t expected_state; xeen_event_request last; } services;
static int dispatch(void *u,const xeen_event_request *r,uint8_t *d,size_t n,uint16_t *ax)
{
    (void)u; services.last=*r;
    if(services.check_state && r->address==services.expected_address) {
        CHECK(d[0x9a1c]==services.expected_state);services.check_state=0;
    }
    if(r->address==0x15616u) { ++services.text_calls;
        if(services.fail_text)return 0;
        return xeen_event_text_copy(&texts,r->argument,d,n,r->destination,r->count)==XE_OK; }
    if(r->address==0x4a1b7u) return 1; /* declared controlled unit residency service */
    if(r->address==0x3000du) { ++services.ui_calls;
        if(services.ui<0)return services.ui==-1?0:-1;
        *ax=(uint16_t)services.ui;return 1; }
    if(r->address==0x50abbu) { ++services.sound_calls;return services.sound; }
    return 0;
}
static void begin(uint8_t *b,size_t n)
{
    static const uint8_t strings[]="first\0second\0third\0";
    memset(ds,0,sizeof ds); ds[0xed97]=1;ds[0xed98]=2;ds[0xed96]=3;ds[0x98b4]=255;
    memset(&services,0,sizeof services);
    CHECK(xeen_event_text_bind(&texts,strings,sizeof strings)==XE_OK);
    CHECK(xeen_events_decode(b,n,strings,sizeof strings,&index_rows)==XE_OK);
    CHECK(xeen_event_execution_begin(&execution,ds,sizeof ds)==XEE_DONE);
}
static xeen_exec_status run(uint8_t *b,size_t n,size_t budget)
{ return xeen_event_execute_expanded(b,n,&index_rows,ds,sizeof ds,&execution,budget,dispatch,NULL,NULL,NULL); }
int main(void)
{
    uint8_t choice[]={10,1,2,4,0,5,0,1,7,0,3, 6,1,2,4,1,59,1,
                     6,1,2,4,3,59,3, 6,1,2,4,4,18,0};
    begin(choice,sizeof choice);services.ui=-1;
    CHECK(run(choice,sizeof choice,20)==XEE_EXTERNAL);
    CHECK(execution.state.boundary==0x3000du && execution.state.steps==0 && execution.phase==2);
    CHECK(!memcmp(ds+0xa827,"first",6) && !memcmp(ds+0xa43f,"second",7));
    CHECK(services.text_calls==2 && services.last.argument==7 && services.last.second_argument==0);
    services.ui=1;ds[0x9a1c]=0x22;services.check_state=1;services.expected_address=0x3000d;services.expected_state=0x22;
    CHECK(run(choice,sizeof choice,20)==XEE_EXTERNAL);
    CHECK(execution.state.boundary==0x33690u && execution.state.steps==3 && ds[0x96e2]==3);
    CHECK(services.text_calls==4); /* only two later records, no repeated pending text */
    begin(choice,sizeof choice);services.ui=0;
    CHECK(run(choice,sizeof choice,20)==XEE_EXTERNAL && execution.state.steps==2 && ds[0x96e2]==1);
    begin(choice,sizeof choice);services.ui=-2;
    CHECK(run(choice,sizeof choice,20)==XEE_INTERNAL && execution.phase==2);
    choice[10]=128;
    begin(choice,sizeof choice);services.ui=1;
    CHECK(run(choice,sizeof choice,20)==XEE_EXTERNAL && execution.state.sequence==65408u);
    { uint8_t optional[]={9,1,2,4,0,5,0,1,7,2};
      begin(optional,sizeof optional);services.ui=0;
      CHECK(run(optional,sizeof optional,20)==XEE_EXTERNAL && execution.state.steps==1);
      begin(optional,sizeof optional);services.ui=1;
      CHECK(run(optional,sizeof optional,20)==XEE_MALFORMED && execution.state.steps==0);
    }
    { uint8_t loop[]={10,1,2,4,0,5,0,1,7,0,0};
      begin(loop,sizeof loop);services.ui=1;
      CHECK(run(loop,sizeof loop,3)==XEE_BUDGET && execution.state.steps==3 && execution.state.sequence==0);
    }
    { uint8_t sound[]={6,1,2,4,0,6,255,6,1,2,4,1,18,0};
      begin(sound,sizeof sound);
      CHECK(run(sound,sizeof sound,10)==XEE_EXTERNAL && execution.state.steps==2 && !services.sound_calls);
      begin(sound,sizeof sound);ds[0xed9a]=1;
      CHECK(run(sound,sizeof sound,10)==XEE_EXTERNAL && execution.state.boundary==0x50abbu);
      CHECK(services.last.argument==65535u && !execution.state.event_flag && services.text_calls==1);
      services.sound=1;ds[0xed9a]=0;ds[0x9a1c]=0x44;services.check_state=1;services.expected_address=0x50abb;services.expected_state=0x44;
      CHECK(run(sound,sizeof sound,10)==XEE_EXTERNAL && execution.state.steps==2 && services.text_calls==2);
    }
    { uint8_t call[]={8,1,2,4,0,25,3,4,0, 6,1,2,4,1,18,0,
                     8,3,4,4,0,34,255,170,85, 6,3,4,4,1,26,0};
      begin(call,sizeof call);
      CHECK(run(call,sizeof call,1)==XEE_BUDGET && execution.stack_index==0 && execution.state.x==3);
      CHECK(run(call,sizeof call,10)==XEE_EXTERNAL && execution.state.steps==4);
      CHECK(execution.stack_index==65535u && execution.state.x==1 && execution.state.y==2 && execution.state.sequence==1);
      CHECK(ds[0xdd20]==170 && ds[0xdd21]==85 && execution.return_x[0]==1);
    }
    { uint8_t push[]={8,1,2,4,0,25,1,2,0};
      begin(push,sizeof push);
      CHECK(run(push,sizeof push,5)==XEE_BUDGET && execution.stack_index==4);
      CHECK(run(push,sizeof push,1)==XEE_MALFORMED && execution.stack_index==4 && execution.state.steps==5);
    }
    { uint8_t pop[]={6,1,2,4,0,26,0};
      begin(pop,sizeof pop);
      CHECK(run(pop,sizeof pop,1)==XEE_MALFORMED && execution.stack_index==65535u);
    }
    { uint8_t swap[]={7,1,2,4,0,51,128,127};
      begin(swap,sizeof swap);ds[0xdc00]=1;ds[0xdc01]=2;ds[0xe3f8]=3;ds[0xe3f9]=4;
      CHECK(run(swap,sizeof swap,10)==XEE_EXTERNAL);
      CHECK(ds[0xdc00]==3 && ds[0xdc01]==4 && ds[0xe3f8]==1 && ds[0xe3f9]==2 && execution.state.event_flag);
    }
    { uint8_t short_operand[]={7,1,2,4,0,34,0,1};
      begin(short_operand,sizeof short_operand);
      CHECK(run(short_operand,sizeof short_operand,10)==XEE_MALFORMED && ds[0xdd28]==0);
    }
    { uint8_t unsupported[]={6,1,2,4,0,2,0};
      begin(unsupported,sizeof unsupported);
      services.fail_text=1;
      CHECK(run(unsupported,sizeof unsupported,10)==XEE_EXTERNAL && execution.phase==4);
      services.fail_text=0;ds[0x9a1c]=0x55;services.check_state=1;services.expected_address=0x15616;services.expected_state=0x55;
      CHECK(run(unsupported,sizeof unsupported,10)==XEE_UNSUPPORTED && execution.state.boundary==0x31590u);
      CHECK(xeen_event_execute_expanded(unsupported,sizeof unsupported,&index_rows,ds,10,&execution,1,dispatch,NULL,NULL,NULL)==XEE_MALFORMED);
    }
    { uint8_t original[]={6,1,2,4,0,18,0}, resident_copy[7]={0};
      xeen_event_resources resource={0};xeen_event_request req={0x4a1b7u,0,0,0,0};uint16_t ax=0;
      resource.backing=original;resource.resident=resident_copy;resource.record_bytes=sizeof original;
      CHECK(xeen_event_resource_service(&resource,&req,ds,sizeof ds,&ax)==1);
      CHECK(!memcmp(original,resident_copy,sizeof original));
      resident_copy[0]=0;
      CHECK(xeen_event_resource_service(&resource,&req,ds,sizeof ds,&ax)==1 && resident_copy[0]==6);
      resource.record_bytes=65536u;
      CHECK(xeen_event_resource_service(&resource,&req,ds,sizeof ds,&ax)==0 && resource.resource_status==XR_BAD_INPUT);
      { xeen_archive empty;uint8_t bytes[]={0,0}, a[10],b[10],c[10];
        xeen_event_resources saved=resource;
        CHECK(xeen_archive_open(&empty,bytes,sizeof bytes)==XR_OK);
        CHECK(xeen_event_resources_load(&resource,&empty,&empty,1,a,sizeof a,b,sizeof b,c,sizeof c,&index_rows)==XR_NOT_FOUND);
        CHECK(!memcmp(&resource,&saved,sizeof resource));
        CHECK(xeen_event_resources_load(&resource,&empty,&empty,1,a,sizeof a,a,sizeof a,c,sizeof c,&index_rows)==XR_BAD_INPUT);
      }
    }
    puts("expanded event operations, call/return, branches, suspension and safety passed");return 0;
}
