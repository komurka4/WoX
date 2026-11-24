#include "xeen/timer.h"
#include <stdio.h>
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#expr); return 1; } } while (0)
#include <string.h>
int main(void) {
    xeen_timer t, before;
    xeen_timer_registers r={0xffff,0xffff,0xffff,0xffff}, old;
    xeen_bios_ticks b={XEEN_BIOS_TICKS_PER_DAY-1,255};
    memset(&t,0xa5,sizeof(t)); before=t; old=r;
    CHECK(xeen_timer_sample(&t,0x4ea0d,&r,0,0)==XEEN_SERVICE_REQUIRED);
    CHECK(!memcmp(&t,&before,sizeof(t)) && !memcmp(&r,&old,sizeof(r)));
    CHECK(xeen_timer_advance(&b,2)==XEEN_OK && b.ticks==1 && b.midnight==0);
    b.midnight=3;
    CHECK(xeen_timer_sample(&t,0x4ea0d,&r,xeen_timer_deterministic_bios,&b)==XEEN_OK);
    CHECK(r.ax==3 && r.cx==0 && r.dx==1 && b.midnight==0);
    CHECK(r.flags==((0xffffu & ~0x8d5u)|0x44u));
    CHECK(t.cs[0xc12]==1 && t.cs[0xc13]==0 && t.cs[0xc16]==0xa5);
    b.ticks=0x12345;
    CHECK(xeen_timer_sample(&t,0x4ea35,&r,xeen_timer_deterministic_bios,&b)==XEEN_OK);
    CHECK(t.cs[0xc16]==0x45 && t.cs[0xc17]==0x23 && t.cs[0xc18]==1);
    CHECK(xeen_timer_advance(&b,UINT64_MAX)==XEEN_OK);
    before=t; old=r;
    CHECK(xeen_timer_sample(0,0x4ea0d,&r,0,0)==XEEN_BAD_INPUT);
    CHECK(xeen_timer_sample(&t,0x4ea0d,0,0,0)==XEEN_BAD_INPUT);
    CHECK(xeen_timer_sample(&t,0x4ea1c,&r,0,0)==XEEN_BAD_INPUT);
    CHECK(xeen_timer_sample(&t,0x4ea0d,&r,xeen_timer_deterministic_bios,0)==XEEN_BAD_INPUT);
    b.ticks=XEEN_BIOS_TICKS_PER_DAY; b.midnight=7;
    CHECK(xeen_timer_sample(&t,0x4ea0d,&r,xeen_timer_deterministic_bios,&b)==XEEN_BAD_INPUT);
    CHECK(!memcmp(&t,&before,sizeof(t)) && !memcmp(&r,&old,sizeof(r)));
    CHECK(b.ticks==XEEN_BIOS_TICKS_PER_DAY && b.midnight==7);
    CHECK(xeen_timer_advance(&b,1)==XEEN_BAD_INPUT);
    CHECK(xeen_timer_advance(0,1)==XEEN_BAD_INPUT);
    CHECK(xeen_timer_deterministic_bios(&b,0)==XEEN_BAD_INPUT);
    {
        xeen_timer_elapsed_state state={0,0x4ea1c};
        xeen_timer clean={ {0},0,0 }; xeen_timer snapshot;
        xeen_bios_ticks ticks={65536,0};
        xeen_timer_registers regs={7,8,9,2};
        CHECK(xeen_timer_elapsed(&clean,&state,&regs,0,0,0,0)==XEEN_SERVICE_REQUIRED);
        CHECK(state.phase==1);
        snapshot=clean;
        CHECK(xeen_timer_elapsed(&clean,&state,&regs,0,0,0,0)==XEEN_SERVICE_REQUIRED);
        CHECK(!memcmp(&snapshot,&clean,sizeof(clean)));
        CHECK(xeen_timer_elapsed(&clean,&state,&regs,0,0,xeen_timer_deterministic_bios,&ticks)==XEEN_OK);
        CHECK(state.phase==2 && regs.ax==65535 && regs.cx==1);
        CHECK(xeen_timer_elapsed(&clean,&state,&regs,0,0,xeen_timer_deterministic_bios,&ticks)==XEEN_BAD_INPUT);
        state.phase=0;clean.cs[0x16c8]=1;snapshot=clean;
        CHECK(xeen_timer_elapsed(&clean,&state,&regs,0,0,0,0)==XEEN_SERVICE_REQUIRED);
        CHECK(state.phase==0 && !memcmp(&snapshot,&clean,sizeof(clean)));
    }
    return 0;
}
