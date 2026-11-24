#include "xeen/event_ui.h"
#include "xeen/program_data.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
/* Assertions remain active in Release: these are executable contracts. */
#ifdef NDEBUG
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)
#endif
static uint8_t ds[65536];
static int service(void *u, const xeen_event_request *r, uint8_t *d, size_t n, uint16_t *ax)
{
    if (r->address==0x15616 || r->address==0x4a1b7) return 1;
    return xeen_event_ui_service(u,r,d,n,ax);
}
static void entry(xeen_ui_script_entry *e, xeen_ui_kind kind, unsigned mode, unsigned input)
{
    memset(e,0,sizeof(*e)); e->request.kind=kind; e->request.mode=(uint16_t)mode;
    e->request.face=20; e->request.primary_size=2; e->request.secondary_size=2;
    memcpy(e->request.primary,"P",2); memcpy(e->request.secondary,"S",2);
    e->response.input=(uint16_t)input;
}
int main(void)
{
    xeen_event_ui ui; xeen_ui_script script; xeen_ui_script_entry e;
    xeen_event_request r={0x3000d,20,0,0,0}; uint16_t ax=99;
    /* Phase 295: the DS holds the program data first (operation 2 reads its sign format from it). */
    memcpy(ds,xeen_program_ds_image,XEEN_PROGRAM_DS_SIZE);
    memcpy(ds+0xa827,"P",2); memcpy(ds+0xa43f,"S",2);
    for (unsigned mode=0;mode<3;++mode) for (unsigned k=0;k<3;++k) {
        unsigned keys[]={0x59,0x4e,0x1b};
        entry(&e,mode==0?XUI_CONFIRM:mode==1?XUI_DISMISS:XUI_PRESENT,mode,mode==2?0:keys[k]);
        e.pending_polls=2; r.second_argument=(uint16_t)mode;
        xeen_ui_script_init(&script,&e,1);
        xeen_event_ui_init(&ui,xeen_ui_script_present,xeen_ui_script_poll,&script);
        assert(!xeen_event_ui_service(&ui,&r,ds,sizeof(ds),&ax));
        assert(!xeen_event_ui_service(&ui,&r,ds,sizeof(ds),&ax));
        assert(xeen_event_ui_service(&ui,&r,ds,sizeof(ds),&ax)==1);
        assert(ax==(mode==0 && k==0)); assert(ui.presentations==1 && ui.completions==1);
        assert(script.consumed==1);
        assert(xeen_event_ui_service(&ui,&r,ds,sizeof(ds),&ax)==-1);
        assert(script.status==XUI_SCRIPT_EXHAUSTED);
    }
    /* Phase103: a 3000D mode-0 page takes any key (AX 0); a keyless answer is an error. */
    r.second_argument=0; entry(&e,XUI_CONFIRM,0,0x20);
    xeen_ui_script_init(&script,&e,1); xeen_event_ui_init(&ui,xeen_ui_script_present,xeen_ui_script_poll,&script);
    assert(xeen_event_ui_service(&ui,&r,ds,sizeof(ds),&ax)==1 && ax==0);
    entry(&e,XUI_CONFIRM,0,0);
    xeen_ui_script_init(&script,&e,1); xeen_event_ui_init(&ui,xeen_ui_script_present,xeen_ui_script_poll,&script);
    assert(xeen_event_ui_service(&ui,&r,ds,sizeof(ds),&ax)==-1);
    entry(&e,XUI_CONFIRM,0,0x59); e.request.face=21;
    xeen_ui_script_init(&script,&e,1); xeen_event_ui_init(&ui,xeen_ui_script_present,xeen_ui_script_poll,&script);
    assert(xeen_event_ui_service(&ui,&r,ds,sizeof(ds),&ax)==-1 && script.status==XUI_SCRIPT_MISMATCH && !script.consumed);
    xeen_event_ui_init(&ui,NULL,NULL,NULL);
    assert(!xeen_event_ui_service(&ui,&r,ds,sizeof(ds),&ax));
    memset(ds+0xa827,1,1000); assert(xeen_event_ui_service(&ui,&r,ds,sizeof(ds),&ax)==-1);
    memcpy(ds+0xa827,"P",2);
    entry(&e,XUI_CONFIRM,0,0x59); e.pending_polls=1;
    xeen_ui_script_init(&script,&e,1); xeen_event_ui_init(&ui,xeen_ui_script_present,xeen_ui_script_poll,&script);
    assert(!xeen_event_ui_service(&ui,&r,ds,sizeof(ds),&ax));
    ds[0xa827]='X'; assert(xeen_event_ui_service(&ui,&r,ds,sizeof(ds),&ax)==-1 && !script.consumed);
    ds[0xa827]='P';
    {
        uint8_t records[]={10,1,2,4,0,5,0,0,20,0,2, 6,1,2,4,2,2,0};
        xeen_event_index idx; xeen_event_execution execution;
        assert(xeen_events_decode(records,sizeof(records),NULL,0,&idx)==XE_OK);
        ds[0xed97]=1;ds[0xed98]=2;ds[0x98b4]=255;
        entry(&e,XUI_CONFIRM,0,0x59); e.pending_polls=1;
        xeen_ui_script_init(&script,&e,1); xeen_event_ui_init(&ui,xeen_ui_script_present,xeen_ui_script_poll,&script);
        assert(xeen_event_execution_begin(&execution,ds,sizeof(ds))==XEE_DONE);
        assert(xeen_event_execute_interactive(records,sizeof(records),&idx,ds,sizeof(ds),&execution,0,service,&ui,NULL,NULL)==XEE_BUDGET);
        assert(!ui.presentations);
        assert(xeen_event_execute_interactive(records,sizeof(records),&idx,ds,sizeof(ds),&execution,1,service,&ui,NULL,NULL)==XEE_EXTERNAL);
        assert(execution.phase==2 && execution.state.steps==0);
        assert(xeen_event_execute_interactive(records,sizeof(records),&idx,ds,sizeof(ds),&execution,1,service,&ui,NULL,NULL)==XEE_BUDGET);
        assert(execution.state.sequence==2 && execution.state.steps==1 && ui.presentations==1);
        assert(xeen_event_execute_interactive(records,sizeof(records),&idx,ds,sizeof(ds),&execution,1,service,&ui,NULL,NULL)==XEE_EXTERNAL);
        assert(execution.state.operation==2 && execution.state.offset==11 && execution.state.boundary==0x1b503);
    }
    {
        uint8_t records[]={6,1,2,4,0,1,0};
        xeen_event_index idx; xeen_event_execution ex;
        assert(xeen_events_decode(records,sizeof(records),NULL,0,&idx)==XE_OK);
        entry(&e,XUI_TEXT,0,0); e.request.face=0;e.request.secondary_size=1;e.request.secondary[0]=0;
        xeen_ui_script_init(&script,&e,1);xeen_event_ui_init(&ui,xeen_ui_script_present,xeen_ui_script_poll,&script);
        assert(xeen_event_execution_begin(&ex,ds,sizeof(ds))==XEE_DONE);
        assert(xeen_event_execute_interactive(records,sizeof(records),&idx,ds,sizeof(ds),&ex,3,service,&ui,NULL,NULL)==XEE_EXTERNAL);
        assert(ex.state.steps==1 && ex.state.sequence==1 && ex.state.boundary==0x33690);
        assert(!memcmp(ds+0xac0f,"\r\3cP",5));
    }
    {
        uint8_t records[]={7,1,2,4,0,32,255,128};
        xeen_event_index idx; xeen_event_execution ex;
        assert(xeen_events_decode(records,sizeof(records),NULL,0,&idx)==XE_OK);
        ds[0xed8c]=0;assert(xeen_event_execution_begin(&ex,ds,sizeof(ds))==XEE_DONE);
        assert(xeen_event_execute_interactive(records,sizeof(records),&idx,ds,sizeof(ds),&ex,3,service,&ui,NULL,NULL)==XEE_EXTERNAL);
        assert(ex.state.boundary==0x2f9ba && !ex.state.steps);
        ds[0xed8c]=1;
        assert(xeen_event_execute_interactive(records,sizeof(records),&idx,ds,sizeof(ds),&ex,3,service,&ui,NULL,NULL)==XEE_EXTERNAL);
        assert(ex.state.steps==1 && ex.state.sequence==1 && ex.state.event_flag==1);
    }
    {
        xeen_event_request who={0x2f9ba,0,40,1,0};
        memset(ds,0,sizeof(ds));memcpy(ds+0xa827,"Tree",5);ds[0xed8c]=2;
        entry(&e,XUI_CHARACTER,1,0xca);e.request.face=0;
        e.request.primary_size=5;memcpy(e.request.primary,"Tree",5);
        e.request.secondary_size=1;e.request.secondary[0]=0;
        xeen_ui_script_init(&script,&e,1);xeen_event_ui_init(&ui,xeen_ui_script_present,xeen_ui_script_poll,&script);
        assert(xeen_event_ui_service(&ui,&who,ds,sizeof(ds),&ax)==1 && ax==2);
        /* Phase61: 2F9BA leaves the member + 1 in F5FE (2FBD9, 2FC05). */
        assert(ds[0xf5fe]==2 && ui.completions==1);

        entry(&e,XUI_CHARACTER,1,0x1b);e.request.face=0;
        e.request.primary_size=5;memcpy(e.request.primary,"Tree",5);
        e.request.secondary_size=1;e.request.secondary[0]=0;
        xeen_ui_script_init(&script,&e,1);xeen_event_ui_init(&ui,xeen_ui_script_present,xeen_ui_script_poll,&script);
        ax=99;assert(xeen_event_ui_service(&ui,&who,ds,sizeof(ds),&ax)==1 && ax==0);

        entry(&e,XUI_CHARACTER,1,0xcb);e.request.face=0;
        e.request.primary_size=5;memcpy(e.request.primary,"Tree",5);
        e.request.secondary_size=1;e.request.secondary[0]=0;
        xeen_ui_script_init(&script,&e,1);xeen_event_ui_init(&ui,xeen_ui_script_present,xeen_ui_script_poll,&script);
        ax=99;assert(xeen_event_ui_service(&ui,&who,ds,sizeof(ds),&ax)==0 && ax==99);

        ds[0xb41e + 0x143 + 12]=1;
        entry(&e,XUI_CHARACTER,1,0xc9);e.request.face=0;
        e.request.primary_size=5;memcpy(e.request.primary,"Tree",5);
        e.request.secondary_size=1;e.request.secondary[0]=0;
        xeen_ui_script_init(&script,&e,1);xeen_event_ui_init(&ui,xeen_ui_script_present,xeen_ui_script_poll,&script);
        ax=99;assert(xeen_event_ui_service(&ui,&who,ds,sizeof(ds),&ax)==0 && ax==99);
    }
    puts("Phase8 UI contracts passed"); return 0;
}
