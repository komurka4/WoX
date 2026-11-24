#include "xeen/game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do {if(!(c)){fprintf(stderr,"line%d: %s\n",__LINE__,#c);exit(1);}}while(0)
static size_t presentations;
static int present(void *u,const xeen_ui_request *q)
{(void)u;CHECK(q->kind==XUI_DISMISS);++presentations;return 1;}
static uint8_t *read_file(const char *name,size_t *size)
{
    FILE *f=fopen(name,"rb");long n;uint8_t *p;CHECK(f);
    CHECK(!fseek(f,0,SEEK_END));n=ftell(f);CHECK(n>0);rewind(f);
    *size=(size_t)n;p=malloc(*size);CHECK(p);CHECK(fread(p,1,*size,f)==*size);fclose(f);return p;
}
static void synthetic(void)
{
    uint8_t ds[65536]={0},before[65536];xeen_event_dispatch c;xeen_event_index idx;
    uint8_t row[]={6,1,2,4,0,18,0};
    CHECK(xeen_events_decode(row,sizeof row,NULL,0,&idx)==XE_OK);
    /* Phase72: a pending treasure (31111) is stashed for the events and
     * copied back at 33862: the currency and bytes 0..2 of every record. */
    {
        xeen_event_resources resources={0};uint8_t resident[sizeof row];
        static const uint8_t text[]="test";
        memcpy(resident,row,sizeof row);
        resources.backing=row;resources.resident=resident;
        resources.record_bytes=sizeof row;resources.record_count=1;
        CHECK(xeen_event_text_bind(&resources.texts,text,sizeof text)==XE_OK);
        CHECK(xeen_event_resources_prepare(&resources,ds,sizeof ds)==XE_OK);
        ds[0xed97]=1;ds[0xed98]=2;ds[0x98b4]=255;ds[0xed8c]=1;
        ds[0x9a10]=1;ds[0x9a12]=40;ds[0x9a16]=0xb0;ds[0x9a17]=4;
        ds[0x99c0+8]=0x32;ds[0x99c0+9]=0x0b;ds[0x99c0+11]=0x77;
        CHECK(xeen_event_dispatch_begin(&c,ds,sizeof ds,0x581d)==XD_RUNNING);
        CHECK(xeen_event_dispatch_run(&c,row,sizeof row,&idx,ds,sizeof ds,256,
            xeen_event_resource_service,&resources)==XD_RETURNED);
        CHECK(c.boundary==0x33b36);
        CHECK(ds[0x9a10]==1 && ds[0x9a12]==40 && ds[0x9a16]==0xb0 && ds[0x9a17]==4);
        CHECK(ds[0x99c0+8]==0x32 && ds[0x99c0+9]==0x0b && ds[0x99c0+11]==0x77);
        CHECK(c.treasure_stash[0x50+8]==0x32 && c.treasure_stash[0x50+11]==0 && c.treasure_stash[0xa0]==1);
        /* The items stay in place during the events; a cleared record
         * comes back without its byte 3 (never stashed). */
        CHECK(xeen_event_dispatch_begin(&c,ds,sizeof ds,0x581d)==XD_RUNNING);
        CHECK(xeen_event_dispatch_run(&c,row,sizeof row,&idx,ds,sizeof ds,256,
            xeen_event_resource_service,&resources)==XD_RETURNED);
        memset(ds+0x99c0,0,40);memset(ds+0x9a10,0,10);
        CHECK(xeen_event_dispatch_begin(&c,ds,sizeof ds,0x581d)==XD_RUNNING);
        CHECK(!c.treasure_stash[0xa0]);
        CHECK(xeen_event_dispatch_run(&c,row,sizeof row,&idx,ds,sizeof ds,256,
            xeen_event_resource_service,&resources)==XD_RETURNED);
        CHECK(!ds[0x9a10] && !ds[0x99c0+8]);
    }
    /* Raw synthetic records test the reused Phase1 accessor, not party setup. */
    for(unsigned condition=0;condition<=16;++condition){
        xeen_event_resources resources={0};uint8_t resident[sizeof row];
        static const uint8_t text[]="test";
        memset(ds,0,sizeof ds);memcpy(resident,row,sizeof row);
        resources.backing=row;resources.resident=resident;
        resources.record_bytes=sizeof row;resources.record_count=1;
        CHECK(xeen_event_text_bind(&resources.texts,text,sizeof text)==XE_OK);
        CHECK(xeen_event_resources_prepare(&resources,ds,sizeof ds)==XE_OK);
        ds[0xed97]=1;ds[0xed98]=2;ds[0x98b4]=255;ds[0xed8c]=1;
        if(condition<16) ds[0xb41eu+0x143u+condition]=1;
        CHECK(xeen_event_dispatch_begin(&c,ds,sizeof ds,0x581d)==XD_RUNNING);
        CHECK(xeen_event_dispatch_run(&c,row,sizeof row,&idx,ds,sizeof ds,256,
            xeen_event_resource_service,&resources)==
            (condition>=11 && condition<=15?XD_RETURNED_EXIT:XD_RETURNED));
        CHECK(c.boundary==0x33b36 && c.return_ax==0);
        memcpy(before,ds,sizeof ds);
        CHECK(xeen_event_dispatch_run(&c,row,sizeof row,&idx,ds,sizeof ds,256,
            xeen_event_resource_service,&resources)==c.status);
        CHECK(!memcmp(before,ds,sizeof ds));
    }
    /* Phase72: op 14 Remove (32253): the cell's object (98D9) to (80h,80h),
     * every record at the party's cell gets opcode 0 (3227B) in the copy and
     * the 4A24C store keeps it (the next dispatch finds nothing to run). */
    {
        uint8_t rows[]={6,1,2,4,0,14,0, 6,1,2,4,1,18,0, 6,5,5,4,0,18,0};
        uint8_t resident[sizeof rows],backing[sizeof rows];
        xeen_event_resources resources={0};xeen_event_index index;
        static const uint8_t text[]="test";
        memcpy(backing,rows,sizeof rows);memcpy(resident,rows,sizeof rows);
        CHECK(xeen_events_decode(backing,sizeof backing,NULL,0,&index)==XE_OK);
        memset(ds,0,sizeof ds);
        resources.backing=backing;resources.resident=resident;
        resources.record_bytes=sizeof rows;resources.record_count=3;
        CHECK(xeen_event_text_bind(&resources.texts,text,sizeof text)==XE_OK);
        CHECK(xeen_event_resources_prepare(&resources,ds,sizeof ds)==XE_OK);
        ds[0xed97]=1;ds[0xed98]=2;ds[0x98b4]=255;ds[0xed8c]=1;ds[0x98d9]=2;ds[0xe008]=1;ds[0xe009]=2;
        CHECK(xeen_event_dispatch_begin(&c,ds,sizeof ds,0x581d)==XD_RUNNING);
        CHECK(xeen_event_dispatch_run(&c,resident,sizeof resident,&index,ds,sizeof ds,256,
            xeen_event_resource_service,&resources)==XD_RETURNED);
        CHECK(ds[0xe008]==0x80 && ds[0xe009]==0x80);
        CHECK(backing[5]==0 && backing[12]==0 && backing[19]==18);
        CHECK(c.execution.state.event_flag);
    }
    CHECK(xeen_event_dispatch_begin(NULL,ds,sizeof ds,0)==XD_INPUT_ERROR);
    CHECK(xeen_event_dispatch_begin(&c,ds,100,0)==XD_INPUT_ERROR);
}
int main(int argc,char **argv)
{
    size_t sn,an;uint8_t *state,*assets,before[65536];char error[128];
    xeen_game *g;xeen_game_view v;xeen_game_start start;unsigned world;
    if(argc==1){synthetic();puts("dispatcher bounded entry contracts passed");return 0;}
    CHECK(argc==4);world=(unsigned)strtoul(argv[3],NULL,10);CHECK(world<2);
    state=read_file(argv[1],&sn);assets=read_file(argv[2],&an);
    start=(xeen_game_start){(uint8_t)world,(uint8_t)(world?14:6),(uint8_t)(world?4:5),0,(uint16_t)(world?29:49)};
    g=xeen_game_create(state,sn,assets,an,start,present,NULL,error,sizeof error);CHECK(g);
    CHECK(xeen_game_enable_lifecycle(g));CHECK(xeen_game_interact(g));
    v=xeen_game_inspect(g);CHECK(v.command_status==XGC_PENDING_INPUT && v.lifecycle_boundary==0x3000d && presentations==1);
    CHECK(!xeen_game_enable_lifecycle(g));
    memcpy(before,xeen_game_ds(g),sizeof before);
    CHECK(!xeen_game_respond(g,0));CHECK(xeen_game_navigate(g,0xf2)==XSN_INVALID);
    CHECK(!memcmp(before,xeen_game_ds(g),sizeof before));
    CHECK(xeen_game_respond(g,0x1b));v=xeen_game_inspect(g);
    CHECK(v.phase==XG_STOPPED && v.command_status==XGC_ORIGINAL_EXIT && v.lifecycle_boundary==0x33b36);
    CHECK(v.dispatcher_ax==0 && v.sequence==2 && v.completions==1 && presentations==1);
    CHECK(xeen_game_ds(g)[0xf60a]==1 && xeen_game_ds(g)[0x9a1c]==0 && xeen_game_ds(g)[0x96c2]==0);
    CHECK(xeen_game_ds(g)[0xf466]==2 && xeen_game_ds(g)[0xf467]==2);
    CHECK(!v.safe_to_continue && !xeen_game_interact(g));
    memcpy(before,xeen_game_ds(g),sizeof before);xeen_game_leave_event(g);
    CHECK(xeen_game_navigate(g,0xf3)==XSN_INVALID && !xeen_game_pending(g));
    CHECK(!memcmp(before,xeen_game_ds(g),sizeof before));xeen_game_destroy(g);
    --start.y;g=xeen_game_create(state,sn,assets,an,start,NULL,NULL,error,sizeof error);CHECK(g);
    CHECK(xeen_game_enable_lifecycle(g));CHECK(xeen_game_navigate(g,0xf2)==XSN_MOVED);
    v=xeen_game_inspect(g);CHECK(v.position.y==start.y+1 && v.clock==1);
    CHECK(v.lifecycle_boundary==0x30d27 && v.command_status==XGC_EXTERNAL && v.phase==XG_STOPPED);
    CHECK(!v.safe_to_continue && !xeen_game_interact(g));
    memcpy(before,xeen_game_ds(g),sizeof before);xeen_game_leave_event(g);
    CHECK(xeen_game_navigate(g,0xf2)==XSN_INVALID && !memcmp(before,xeen_game_ds(g),sizeof before));
    xeen_game_destroy(g);free(state);free(assets);
    printf("world%u dispatcher return/exit gate, UI continuation, retained clock prefix and unsafe-command exclusion passed\n",world);
    return 0;
}
