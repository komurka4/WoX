#include "xeen/caller.h"
#include "xeen/post_action.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"line%d:%s\n",__LINE__,#c); exit(1); } } while(0)
static xeen_movement m;
static void fixture(void) {
    xeen_movement_init(&m,NULL,NULL);
    m.ds[0xed97]=8; m.ds[0xed98]=8; m.ds[0xed99]=1;
    xeen_write16(&m,0xf46a,1); xeen_write16(&m,0xf5fe,0x1b);
}
int main(void) {
    xeen_caller_result r; uint8_t before[65536]; unsigned i;
    /* Exploratory initialized DS; authentic-session acceptance is differential. */
    fixture(); memcpy(before,m.ds,sizeof before);
    r=xeen_caller_after_dispatch(&m,0); CHECK(r.status==XEEN_SERVICE_REQUIRED && r.boundary==0x4ea0d);
    CHECK(!memcmp(before,m.ds,sizeof before));
    fixture(); m.ds[0xf60a]=1; r=xeen_caller_after_dispatch(&m,0);
    CHECK(r.boundary==0x17f28 && m.ds[0xf60a]==1);
    fixture(); r=xeen_caller_after_dispatch(&m,0xfffe);
    CHECK(r.boundary==0x17f28 && xeen_read16(&m,0xf64a)==0xfffe);
    fixture(); m.ds[0x8c85]=1; m.ds[0xf60a]=1; r=xeen_caller_after_dispatch(&m,0);
    CHECK(r.boundary==0x173a0 && m.ds[0xf60a]==1);
    fixture(); xeen_write16(&m,0xf5fe,0xf2); m.ds[0xf60a]=1;
    r=xeen_caller_after_dispatch(&m,0); CHECK(r.boundary==0x17cdf);
    fixture(); m.ds[0xbfcc+8*16+8]=16; r=xeen_caller_after_dispatch(&m,0);
    CHECK(r.boundary==0x17ae5 && xeen_read16(&m,0xf482)==16);
    /* 2EA68: pending treasure with no visible monster (98B4..98CD all FF)
     * is a dialog; any visible byte defers it to the no-effect epilogue. */
    for(i=0;i<5;i++) {
        fixture(); memset(m.ds+0x98b4,0xff,26); m.ds[i?0x9a10+i*2:0x9a10]=0x80;
        r=xeen_caller_after_dispatch(&m,0); CHECK(r.boundary==0x2ea8e);
        fixture(); memset(m.ds+0x98b4,0xff,26); m.ds[0x98b4+i*6]=3; m.ds[i?0x9a10+i*2:0x9a10]=0x80;
        memcpy(before,m.ds,sizeof before);
        r=xeen_caller_after_dispatch(&m,0); CHECK(r.boundary==0x4ea0d && !memcmp(before,m.ds,sizeof before));
    }
    fixture(); memset(m.ds+0x98b4,0xff,26); m.ds[0x98cd]=0; m.ds[0x9a12]=1; m.ds[0x9a1c]=9;
    r=xeen_caller_after_dispatch(&m,0); CHECK(r.boundary==0x2ea8e); /* 2EA8E: mode 9 skips the gate */
    r=xeen_caller_after_dispatch(NULL,0); CHECK(r.status==XEEN_BAD_INPUT);
    /* Phase 337: the post-action step (Space: no step sound) after its toggles reaches the same loop tail 17F07 and
     * join 17ACC (17D98 jmp 17F07): for each competing state both paths stop at the same boundary, the fall before
     * the exit (17F07 tests 8C85 first), the exit before the join. */
    {
        static const struct { uint16_t at; uint8_t value; uint32_t want; } cases[] = {
            {0, 0, 0x4ea0d}, {0x8c85, 1, 0x173a0}, {0xf60a, 1, 0x17f28}, {0xf64a, 1, 0x17f28},
            {0xbfcc + 8 * 16 + 8, 16, 0x17ae5}};
        unsigned k;
        for (k = 0; k < sizeof cases / sizeof cases[0]; ++k) {
            unsigned both;
            for (both = 0; both < 2; ++both) {
                uint32_t via_dispatch;
                fixture(); if (cases[k].at) m.ds[cases[k].at] = cases[k].value;
                if (both) { m.ds[0xf60a] = 1; xeen_write16(&m, 0xf64a, 1); m.ds[0xbfcc + 8 * 16 + 8] = 16; }
                via_dispatch = xeen_caller_after_dispatch(&m, 0).boundary;
                fixture(); if (cases[k].at) m.ds[cases[k].at] = cases[k].value;
                if (both) { m.ds[0xf60a] = 1; xeen_write16(&m, 0xf64a, 1); m.ds[0xbfcc + 8 * 16 + 8] = 16; }
                xeen_write16(&m, 0xf5fe, 0x20); m.ds[0x3066] = 1;
                r = xeen_post_action_after_surface_native(&m, NULL);
                /* the dispatch stores its AX in F64A first: an exit word alone is the post-action path's own */
                CHECK(r.status == XEEN_SERVICE_REQUIRED && (r.boundary == via_dispatch || (cases[k].at == 0xf64a && !both)));
                CHECK(r.boundary == (both ? (cases[k].at == 0x8c85 ? 0x173a0u : 0x17f28u) : cases[k].want));
                CHECK(m.ds[0x3066] == 0 && xeen_read16(&m, 0xf5e4) == 0x8000 && m.ds[0xf5df] == 1); /* toggles first */
            }
        }
    }
    puts("caller bounded continuation passed"); return 0;
}
