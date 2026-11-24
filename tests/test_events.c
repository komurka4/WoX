#include "xeen/events.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
int main(void)
{
    xeen_event_index index, before;
    const uint8_t records[] = {4,128,255,127,255,6,1,2,3,4,0xaa,0xbb};
    const uint8_t texts[] = {'a',0,0,'b','c'};
    const uint8_t *span; size_t length;
    CHECK(xeen_events_decode(records,sizeof records,texts,sizeof texts,&index)==XE_OK);
    CHECK(index.record_count==2 && index.records[0].field1==-128);
    CHECK(index.records[0].field2==-1 && index.records[0].field3==127 && index.records[0].field4==255);
    CHECK(index.records[1].offset==5 && index.records[1].field4==4);
    CHECK(index.text_terminators==2 && index.text_offsets[1]==2 && index.text_offsets[2]==3);
    CHECK(xeen_event_text(&index,texts,sizeof texts,2,&span,&length)==XE_OK && length==2 && span[0]=='b');
    before=index;
    CHECK(xeen_events_decode(records,sizeof records-1,texts,sizeof texts,&index)==XE_TRUNCATED);
    CHECK(memcmp(&before,&index,sizeof index)==0);
    { const uint8_t bad[]={128}; CHECK(xeen_events_decode(bad,1,NULL,0,&index)==XE_BAD_LENGTH); }
    { uint8_t zeros[280]={0};
      CHECK(xeen_events_decode(NULL,0,zeros,279,&index)==XE_OK);
      CHECK(index.text_count==23 && index.text_terminators==279 && index.text_offsets[279]==279);
      CHECK(xeen_event_text(&index,zeros,279,279,&span,&length)==XE_OK && length==0);
      CHECK(xeen_events_decode(NULL,0,zeros,280,&index)==XE_CAPACITY); }
    CHECK(xeen_events_decode(NULL,1,NULL,0,&index)==XE_BAD_INPUT);
    CHECK(xeen_events_decode(NULL,0,NULL,0,&index)==XE_OK && index.record_count==0);
    { uint8_t *many=malloc(5u*(XEEN_EVENT_MAX_RECORDS+1u)); CHECK(many);
      for(size_t i=0;i<XEEN_EVENT_MAX_RECORDS+1u;++i) { many[i*5u]=4; memset(many+i*5u+1,0,4); }
      CHECK(xeen_events_decode(many,5u*XEEN_EVENT_MAX_RECORDS,NULL,0,&index)==XE_OK);
      CHECK(index.record_count==XEEN_EVENT_MAX_RECORDS);
      CHECK(xeen_events_decode(many,5u*(XEEN_EVENT_MAX_RECORDS+1u),NULL,0,&index)==XE_CAPACITY); free(many); }
    puts("event record/text indexing passed"); return 0;
}
