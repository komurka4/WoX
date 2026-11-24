/* Phase20 replays and contracts.
 * argv: minimap|border INPUT CONTROLLED DRAWS  (controlled original endpoints
 *       from genuine Phase16 captured inputs), or: units */
#include "xeen/minimap.h"
#include "xeen/interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do {if(!(c)){fprintf(stderr,"phase20 line%d: %s\n",__LINE__,#c);exit(1);}}while(0)
static size_t read_all(const char *p, uint8_t *b, size_t cap) {
    FILE *f=fopen(p,"rb");size_t n;CHECK(f);n=fread(b,1,cap,f);fclose(f);return n;
}
static void same_draws(const xeen_sprite_draw *d, size_t n, const uint8_t *raw, size_t bytes) {
    size_t i;
    CHECK(bytes==n*14u);
    for (i=0;i<n;++i) {
        const uint16_t f[7]={d[i].handle,d[i].frame,d[i].x,d[i].y,d[i].scale,d[i].flags,d[i].window};
        unsigned k;
        for (k=0;k<7;++k) CHECK(f[k]==(uint16_t)(raw[i*14u+2u*k]|raw[i*14u+2u*k+1u]<<8));
    }
}
static void replay(const char *kind, const char *in, const char *out, const char *draws) {
    xeen_movement *m=calloc(1,sizeof *m);
    uint8_t *expected=malloc(65536), *raw=malloc(14u*XEEN_MINIMAP_MAX_DRAWS);
    size_t n;
    CHECK(m && expected && raw);
    CHECK(read_all(in,m->ds,65536)==65536 && read_all(out,expected,65536)==65536);
    n=read_all(draws,raw,14u*XEEN_MINIMAP_MAX_DRAWS);
    if (!strcmp(kind,"minimap")) {
        xeen_minimap_draws *d=malloc(sizeof *d);
        CHECK(d && xeen_scene_minimap(m,d)==XEEN_OK);
        same_draws(d->draws,d->count,raw,n);
        printf("minimap replay: %zu draws\n",d->count);free(d);
    } else {
        xeen_border_ops o;
        CHECK(xeen_interface_border(m,&o)==XEEN_OK);
        same_draws(o.draws,o.count,raw,n);
        printf("border replay: %zu draws\n",o.count);
    }
    CHECK(!memcmp(m->ds,expected,65536));
    free(m);free(expected);free(raw);
}
static void units(void) {
    static xeen_presentation p;
    static uint8_t fb[XEEN_PRESENT_PIXELS];
    static const uint8_t pix[4]={1,2,3,4}, op[4]={1,1,1,1};
    xeen_movement *m=calloc(1,sizeof *m);
    uint16_t r;
    unsigned i;
    CHECK(m);
    /* 4A68A: level>=4 untouched, 0 fills viewport, outside untouched. */
    for (i=0;i<XEEN_PRESENT_PIXELS;++i) fb[i]=(uint8_t)(i|1u);
    p.framebuffer=fb;p.framebuffer_size=sizeof fb;
    CHECK(xeen_presentation_darken(&p,4)==XEEN_OK && fb[8*320+8]==(uint8_t)((8*320+8)|1));
    CHECK(xeen_presentation_darken(&p,0)==XEEN_OK && fb[8*320+8]==0 && fb[(8+131)*320+8+215]==0);
    CHECK(fb[7*320+8]==(uint8_t)((7*320+8)|1) && fb[8*320+224]==(uint8_t)((8*320+224)|1));
    p.framebuffer=NULL;CHECK(xeen_presentation_darken(&p,1)==XEEN_SERVICE_REQUIRED);
    /* 50BF6/4E33F pending-window machine. */
    p.framebuffer=fb;p.cursor_pixels=pix;p.cursor_opacity=op;p.cursor_size=4;p.cursor_width=2;p.cursor_height=2;
    p.old_x=p.cursor_x=p.sampled_x=100;p.old_y=p.cursor_y=p.sampled_y=100;p.enabled=1;p.dirty=0xffff;
    CHECK(xeen_presentation_update_window(&p,0)==XEEN_OK && p.page_selector==0x4000 && p.dirty==0);
    CHECK(p.op_count==1 && p.ops[0][0]==XEEN_PRESENT_OP_FULL);
    p.page_selector=0;p.dirty=0xffff; /* isolate historical rectangle cases */
    CHECK(xeen_presentation_update_window(&p,28)==XEEN_SERVICE_REQUIRED);                        /* runtime window */
    CHECK(xeen_presentation_update_window(&p,1)==XEEN_OK && p.dirty==2 && p.page_selector==0x4000);
    CHECK(p.op_count==2 && p.ops[0][0]==XEEN_PRESENT_OP_COPY && p.ops[0][1]==237 && p.ops[1][0]==XEEN_PRESENT_OP_FLIP);
    CHECK(xeen_presentation_update_window(&p,3)==XEEN_OK && p.dirty==6 && p.op_count==3);
    CHECK(p.ops[0][1]==237 && p.ops[1][1]==0 && p.ops[1][3]==149 && p.ops[1][4]==230);
    CHECK(xeen_presentation_present(&p)==XEEN_OK && p.dirty==0xffff && p.op_count==2 && p.ops[0][4]==230);
    p.dirty=0;CHECK(xeen_presentation_update_window(&p,3)==XEEN_OK && p.dirty==6 && p.op_count==1 && p.ops[0][0]==XEEN_PRESENT_OP_FULL);
    /* Recorded text ring and DAC ports. */
    for (i=0;i<10;++i) { char t[8]; snprintf(t,sizeof t,"t%u",i); CHECK(xeen_presentation_text(&p,(uint16_t)i,t)==XEEN_OK); }
    CHECK(p.text_count==XEEN_PRESENT_TEXTS && p.texts_total==10 && !strcmp(p.texts[0],"t2") && !strcmp(p.texts[7],"t9"));
    CHECK(xeen_presentation_port(&p,0x3c8,255)==XEEN_OK);
    CHECK(xeen_presentation_port(&p,0x3c9,1)==XEEN_OK && xeen_presentation_port(&p,0x3c9,2)==XEEN_OK && xeen_presentation_port(&p,0x3c9,3)==XEEN_OK);
    CHECK(p.palette[765]==1 && p.palette[767]==3 && p.dac_index==0 && xeen_presentation_port(&p,0x3c0,0)==XEEN_BAD_INPUT);
    /* 1C680 edges: out of range, missing cache slot, zero map id quirk. */
    CHECK(xeen_maze_lookup(m,32,0,0xffff,0,&r)==XEEN_OK && r==0x8888 && xeen_read16(m,0xf48e)==0x8888);
    m->ds[0xed99]=5;CHECK(xeen_maze_lookup(m,1,1,0xffff,0,&r)==XEEN_CACHE_MISS);
    m->ds[0xed99]=0;xeen_write16(m,0xf608,1);         /* slot id0 present (zeroed table) */
    CHECK(xeen_maze_lookup(m,1,1,0xffff,0,&r)==XEEN_OK && r==0x000f && xeen_read16(m,0xf492)==1);
    /* 2BEF9 policies. */
    memset(m->ds,0,65536);m->ds[0xed8c]=3;
    m->ds[0xb445+6]=1;CHECK(xeen_party_skill(m,6));
    m->ds[0xb445+9]=1;CHECK(!xeen_party_skill(m,9));m->ds[0xb445+9+0x162]=1;CHECK(xeen_party_skill(m,9));
    m->ds[0xb445+5]=m->ds[0xb445+5+0x162]=1;CHECK(!xeen_party_skill(m,5));m->ds[0xb445+5+2*0x162]=1;CHECK(xeen_party_skill(m,5));
    m->ds[0xb445+18]=1;CHECK(!xeen_party_skill(m,18));
    m->ds[0xed96]=4;{xeen_border_ops o;CHECK(xeen_interface_border(m,&o)==XEEN_BAD_INPUT);}
    puts("Phase20 contracts: darken, window/present machine, text ring, DAC, maze lookup edges, skill policies");
    free(m);
}
int main(int argc, char **argv) {
    if (argc==2 && !strcmp(argv[1],"units")) units();
    else { CHECK(argc==5); replay(argv[1],argv[2],argv[3],argv[4]); }
    return 0;
}
