#include "xeen/event_exec.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)
static uint8_t ds[65536], replay[65536];
static xeen_event_index idx;
static int copy(void *u, uint32_t address, uint8_t selector, uint8_t *d, size_t n)
{ (void)address; (void)selector; (void)d; (void)n; return u ? *(int *)u : 1; }
static int no_query(void *u, uint32_t a, uint8_t v, uint8_t *d, size_t n)
{ (void)u; (void)v; (void)d; (void)n; return a==0x1c1c1u ? 0 : 1; }
/* Phase 324: operation 5's host: every request performed, the NPC dialogue (3000D) answering *U (1 yes, 0 no). */
static int npc_host(void *u, const xeen_event_request *r, uint8_t *d, size_t n, uint16_t *ax)
{ (void)d; (void)n; if (r->address == XEEN_REQUEST_NPC_DIALOGUE) *ax = (uint16_t)*(int *)u; return 1; }
/* Operation 5's yes reads its line at p10 (BP-4 = CBW p10 - 1, 318C9): a record of 10 bytes (length byte 9) has none,
 * so p10 is the next record's length byte, read as the original does; as the store's last record, p10 lies past the
 * store and the native refuses the branch (XEE_MALFORMED, the event flag not set). A no needs no p10. */
static int npc_store_end(void)
{
    uint8_t last[]={9,1,2,4,0,5,0,0,3,0}, next[]={9,1,2,4,0,5,0,0,3,0, 6,1,2,4,6,18,0};
    xeen_event_execution ex; int yes=1, no=0;
    CHECK(xeen_events_decode(last,sizeof last,NULL,0,&idx)==XE_OK);
    CHECK(xeen_event_execution_begin(&ex,ds,sizeof ds)==XEE_DONE);
    CHECK(xeen_event_execute_expanded(last,sizeof last,&idx,ds,sizeof ds,&ex,10,npc_host,&yes,NULL,NULL)==XEE_MALFORMED);
    CHECK(ex.state.event_flag==0 && ex.state.sequence==0 && ex.state.steps==0);
    CHECK(xeen_event_execution_begin(&ex,ds,sizeof ds)==XEE_DONE);
    CHECK(xeen_event_execute_expanded(last,sizeof last,&idx,ds,sizeof ds,&ex,10,npc_host,&no,NULL,NULL)==XEE_EXTERNAL);
    CHECK(ex.state.boundary==0x33690 && ex.state.steps==1 && ex.state.sequence==1 && ex.state.event_flag==1);
    CHECK(xeen_events_decode(next,sizeof next,NULL,0,&idx)==XE_OK);
    CHECK(xeen_event_execution_begin(&ex,ds,sizeof ds)==XEE_DONE);
    CHECK(xeen_event_execute_expanded(next,sizeof next,&idx,ds,sizeof ds,&ex,10,npc_host,&yes,NULL,NULL)==XEE_EXTERNAL);
    CHECK(ex.state.boundary==0x33690 && ex.state.steps==2 && ex.state.sequence==6); /* line 6 (p10 = 6) ran: its 18 ends */
    return 0;
}
int main(void)
{
    /* All operations and widths anchored to 3143A..315A6, 33317..33326,
     * 33472..3349D and 33670..33690; text copy explicitly substituted. */
    uint8_t b[]={6,1,2,4,0,45,255, 10,1,2,3,1,60,128,0x34,0x12,0xcd,0xab,
        6,1,2,3,2,59,0x81, 6,1,2,3,3,0,0, 6,1,2,3,4,18,0};
    xeen_event_state s, again; int service=0;
    CHECK(xeen_events_decode(b,sizeof b,NULL,0,&idx)==XE_OK);
    ds[0x98b4]=255; ds[0xed97]=1; ds[0xed98]=2; ds[0xed96]=3;
    CHECK(xeen_event_begin(&s,ds,sizeof ds)==XEE_DONE);
    CHECK(xeen_event_execute(b,sizeof b,&idx,ds,sizeof ds,&s,10,NULL,NULL,NULL,NULL)==XEE_EXTERNAL);
    CHECK(s.boundary==0x15616 && ds[0x9a1c]==9 && s.steps==0);
    CHECK(xeen_event_execute(b,sizeof b,&idx,ds,sizeof ds,&s,10,copy,&service,NULL,NULL)==XEE_EXTERNAL);
    service=-1;
    CHECK(xeen_event_execute(b,sizeof b,&idx,ds,sizeof ds,&s,10,copy,&service,NULL,NULL)==XEE_INTERNAL);
    CHECK(xeen_event_execute(b,sizeof b,&idx,ds,sizeof ds,&s,1,copy,NULL,NULL,NULL)==XEE_BUDGET);
    CHECK(s.sequence==1 && s.steps==1 && ds[0x8c74]==255 && ds[0x8c75]==255);
    memcpy(replay,ds,sizeof ds); again=s;
    CHECK(xeen_event_execute(b,sizeof b,&idx,ds,sizeof ds,&s,10,copy,NULL,NULL,NULL)==XEE_EXTERNAL);
    CHECK(s.boundary==0x33690 && s.logical_result==XEE_DONE && s.steps==5 && s.sequence==4);
    CHECK(ds[0x84e0]==128 && ds[0x84e1]==255 && ds[0x84e2]==0x34 && ds[0x84e3]==0x12 && ds[0x84e4]==0xcd && ds[0x84e5]==0xab && ds[0x96e2]==0x81);
    CHECK(xeen_event_execute(b,sizeof b,&idx,replay,sizeof replay,&again,10,copy,NULL,NULL,NULL)==XEE_EXTERNAL);
    CHECK(!memcmp(&s,&again,sizeof s) && !memcmp(ds,replay,sizeof ds));
    CHECK(xeen_event_begin(&s,ds,sizeof ds)==XEE_DONE); ds[0xf60a]=1;
    CHECK(xeen_event_execute(b,sizeof b,&idx,ds,sizeof ds,&s,10,copy,NULL,NULL,NULL)==XEE_EXTERNAL);
    CHECK(s.sequence==0 && s.steps==1 && s.boundary==0x33690); ds[0xf60a]=0;
    CHECK(xeen_event_begin(&s,ds,sizeof ds)==XEE_DONE); ds[0xed96]=2;
    CHECK(xeen_event_execute(b,sizeof b,&idx,ds,sizeof ds,&s,10,copy,NULL,NULL,NULL)==XEE_EXTERNAL);
    CHECK(s.direction_mismatch && s.sequence==1 && s.steps==1);
    b[5]=1; CHECK(xeen_event_begin(&s,ds,sizeof ds)==XEE_DONE);
    CHECK(xeen_event_execute(b,sizeof b,&idx,ds,sizeof ds,&s,10,copy,NULL,NULL,NULL)==XEE_UNSUPPORTED);
    CHECK(s.steps==0 && s.operation==1);
    idx.records[0].offset=65535;
    CHECK(xeen_event_execute(b,sizeof b,&idx,ds,sizeof ds,&s,10,copy,NULL,NULL,NULL)==XEE_MALFORMED);
    CHECK(xeen_events_decode(b,sizeof b,NULL,0,&idx)==XE_OK);
    CHECK(xeen_event_execute(b,sizeof b-1,&idx,ds,sizeof ds,&s,10,copy,NULL,NULL,NULL)==XEE_MALFORMED);
    CHECK(xeen_event_execute(b,sizeof b,&idx,ds,100,&s,10,copy,NULL,NULL,NULL)==XEE_MALFORMED);
    { uint8_t shortop[]={6,1,2,4,0,60,1};
      CHECK(xeen_events_decode(shortop,sizeof shortop,NULL,0,&idx)==XE_OK);
      CHECK(xeen_event_begin(&s,ds,sizeof ds)==XEE_DONE);
      CHECK(xeen_event_execute(shortop,sizeof shortop,&idx,ds,sizeof ds,&s,10,copy,NULL,NULL,NULL)==XEE_MALFORMED);
    }
    { uint8_t shortop[]={4,1,2,4,0};
      CHECK(xeen_events_decode(shortop,sizeof shortop,NULL,0,&idx)==XE_OK);
      CHECK(xeen_event_begin(&s,ds,sizeof ds)==XEE_DONE);
      CHECK(xeen_event_execute(shortop,sizeof shortop,&idx,ds,sizeof ds,&s,10,copy,NULL,NULL,NULL)==XEE_MALFORMED);
    }
    /* Common text selector is a resource-relative read even for a six-byte
     * opcode-zero row. It can be the next record's length byte. */
    { uint8_t cross[]={5,1,2,4,0,0, 6,1,2,4,1,18,0};
      CHECK(xeen_events_decode(cross,sizeof cross,NULL,0,&idx)==XE_OK);
      CHECK(xeen_event_begin(&s,ds,sizeof ds)==XEE_DONE);
      CHECK(xeen_event_execute(cross,sizeof cross,&idx,ds,sizeof ds,&s,0,copy,NULL,NULL,NULL)==XEE_BUDGET);
      CHECK(s.steps==0);
      CHECK(xeen_event_execute(cross,sizeof cross,&idx,ds,sizeof ds,&s,10,copy,NULL,NULL,NULL)==XEE_EXTERNAL);
      CHECK(s.steps==2 && s.boundary==0x33690);
      ds[0x98b4]=0;
      CHECK(xeen_event_begin(&s,ds,sizeof ds)==XEE_DONE);
      CHECK(xeen_event_execute(cross,sizeof cross,&idx,ds,sizeof ds,&s,10,copy,NULL,NULL,NULL)==XEE_EXTERNAL);
      CHECK(s.steps==1 && ds[0x9756]==1 && s.sequence==1);
      ds[0x98b4]=255;
      CHECK(xeen_event_begin(&s,ds,sizeof ds)==XEE_DONE);
      CHECK(xeen_event_execute(cross,sizeof cross,&idx,ds,sizeof ds,&s,1,copy,NULL,NULL,NULL)==XEE_BUDGET);
      s.reload_pending=1;
      CHECK(xeen_event_execute(cross,sizeof cross,&idx,ds,sizeof ds,&s,1,NULL,NULL,NULL,NULL)==XEE_EXTERNAL);
      CHECK(s.boundary==0x4a1b7);
    }
    { uint8_t branch[]={7,1,2,4,0,54,0,0};
      CHECK(xeen_events_decode(branch,sizeof branch,NULL,0,&idx)==XE_OK);
      CHECK(xeen_event_begin(&s,ds,sizeof ds)==XEE_DONE);
      CHECK(xeen_event_execute(branch,sizeof branch,&idx,ds,sizeof ds,&s,10,no_query,NULL,NULL,NULL)==XEE_EXTERNAL);
      CHECK(s.boundary==0x1c1c1 && s.steps==0);
      CHECK(xeen_event_execute(branch,sizeof branch,&idx,ds,sizeof ds,&s,3,copy,NULL,NULL,NULL)==XEE_BUDGET);
      CHECK(s.steps==3 && s.sequence==0 && s.event_flag==1);
      branch[6]=1;
      CHECK(xeen_event_begin(&s,ds,sizeof ds)==XEE_DONE);
      CHECK(xeen_event_execute(branch,sizeof branch,&idx,ds,sizeof ds,&s,3,copy,NULL,NULL,NULL)==XEE_EXTERNAL);
      CHECK(s.steps==1 && s.sequence==1 && s.boundary==0x33690);
      branch[6]=255; branch[7]=128; ds[0xf490]=255; ds[0xf491]=255;
      CHECK(xeen_event_begin(&s,ds,sizeof ds)==XEE_DONE);
      CHECK(xeen_event_execute(branch,sizeof branch,&idx,ds,sizeof ds,&s,3,copy,NULL,NULL,NULL)==XEE_EXTERNAL);
      CHECK(s.sequence==65408u); /* signed -128 target, modulo 65536 */
    }
    CHECK(xeen_events_decode(NULL,0,NULL,0,&idx)==XE_OK);
    CHECK(xeen_event_begin(&s,ds,sizeof ds)==XEE_DONE);
    CHECK(xeen_event_execute(NULL,0,&idx,ds,sizeof ds,&s,0,copy,NULL,NULL,NULL)==XEE_EXTERNAL);
    CHECK(s.logical_result==XEE_NO_MATCH);
    if (npc_store_end()) return 1;
    puts("Phase 6 selection, control, mutations, boundaries and bounds passed"); return 0;
}
