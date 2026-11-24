/* Phase137: the window frontend's view of the original's intermediate frames
 * and the M map's lifecycle pieces.
 * - The display hook (xeen_presentation display): 189E3's keyboard feedback
 *   (8C76, a 989C key of the live tables, 9898 enabled) displays the pressed
 *   frame 2i+1 and then frame 2i, one call per 50BF6 flip; 18C0C's click adds
 *   one per 4E18F drain call; a disabled button, a key outside the table and
 *   a 3D-view click display nothing; the activation is returned once.
 * - xeen_map_begin's clamps and xeen_scene_map's passes on a one-slot cache
 *   (outdoor: three 16x16 passes; indoor: [BP-2] advanced, the 34h frame
 *   after 4F6F8, the blink arrow).
 * Modes for phase137/feedback_probe.py (the original's 189E3 in Unicorn):
 *   --feedback DS KEY | --click DS X Y: one native 189E3 on that DS image
 *   (XEEN_DRAW_TRACE=1 prints its draw lines), then "command C displays N". */
#include "xeen/platform.h"
#include "xeen/minimap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);return 1;}}while(0)
static const uint16_t xs[21]={235,260,286,235,260,286,235,260,286,109,235,260,286,235,260,286,236,239,239,239,0xffff};
static const uint8_t ys[20]={75,75,75,96,96,96,117,117,117,137,148,148,148,169,169,169,11,27,37,47};
static const uint8_t ws[20]={24,24,24,24,24,24,24,24,24,13,24,24,24,24,24,24,72,73,73,73};
static const uint8_t hs[20]={20,20,20,20,20,20,20,20,20,10,20,20,20,20,20,20,58,10,10,10};
/* DS:19E2, the exploration keys (S C R B D V M I Q Tab, arrows, = 1 2 3). */
static const uint8_t keys[20]={0x53,0x43,0x52,0x42,0x44,0x56,0x4d,0x49,0x51,9,0xf0,0xf2,0xf1,0xb0,0xf3,0xb1,0x3d,0x31,0x32,0x33};
static const uint8_t on[20]={1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0};
static void far(xeen_movement *m,uint16_t word,uint16_t offset){xeen_write16(m,word,offset);xeen_write16(m,(uint16_t)(word+2u),0x4853);}
static void tables(xeen_movement *m)
{
    unsigned i;
    for(i=0;i<21u;++i)xeen_write16(m,(uint16_t)(0x197cu+2u*i),xs[i]);
    memcpy(m->ds+0x19a6,ys,20);memcpy(m->ds+0x19ba,ws,20);memcpy(m->ds+0x19ce,hs,20);
    memcpy(m->ds+0x19e2,keys,20);memcpy(m->ds+0x19f6,on,20);
    far(m,0x98ac,0x197c);far(m,0x98a8,0x19a6);far(m,0x98a4,0x19ba);far(m,0x98a0,0x19ce);far(m,0x989c,0x19e2);far(m,0x9898,0x19f6);
    xeen_write16(m,0x9896,0x18);m->ds[0x8c76]=1;
}
typedef struct { unsigned calls, reasons[64]; uint16_t frames[64]; } shown;
static xeen_presentation *current;
static void display(void *u,unsigned reason,const uint8_t *page,const uint8_t *palette)
{
    shown *s=u;(void)page;(void)palette;
    if(s->calls<64u){s->reasons[s->calls]=reason;
        s->frames[s->calls]=current->sprite_count?current->sprites[current->sprite_count-1u].frame:0xffffu;}
    ++s->calls;
}
static uint8_t pixels[64000],cursor[1]={1},opaque[1]={1};
static void present_init(xeen_presentation *pr)
{
    memset(pr,0,sizeof *pr);
    pr->enabled=1;pr->dirty=0xffff;pr->framebuffer=pixels;pr->framebuffer_size=64000;
    pr->cursor_pixels=cursor;pr->cursor_opacity=opaque;pr->cursor_width=1;pr->cursor_height=1;pr->cursor_size=1;
}
static int poll(xeen_input_poll *ip,xeen_movement *m,const xeen_input_provider *in,xeen_presentation *pr,uint16_t *command)
{
    xeen_input_poll_result r;
    if(xeen_input_poll_run(ip,m,in,NULL,NULL,pr,&r)!=XEEN_OK)return 0;
    *command=r.command;return 1;
}
static int probe(int argc,char **argv)
{
    static xeen_movement m;static xeen_presentation pr;xeen_input_poll ip;xeen_input_queue q;xeen_input_provider in;
    shown s;uint16_t c=0;FILE *f=fopen(argv[2],"rb");
    if(!f || fread(m.ds,1,65536,f)!=65536u){fprintf(stderr,"bad DS %s\n",argv[2]);if(f)fclose(f);return 2;}
    fclose(f);
    present_init(&pr);current=&pr;memset(&s,0,sizeof s);pr.display=display;pr.display_user=&s;
    xeen_presentation_trace_bind(m.ds);
    xeen_input_poll_init(&ip,0x4853);xeen_input_queue_init(&q);in=xeen_input_queue_provider(&q);
    if(!strcmp(argv[1],"--feedback"))(void)xeen_input_queue_key(&q,(uint16_t)strtoul(argv[3],NULL,0));
    else {
        xeen_mouse_sample ms={(uint16_t)strtoul(argv[3],NULL,0),(uint16_t)strtoul(argv[4],NULL,0),1,1};
        if(argc<5)return 2;
        q.current_mouse=ms;q.current_mouse.click=0;(void)xeen_input_queue_mouse(&q,ms);
    }
    if(!strcmp(argv[1],"--feedback")){q.current_mouse.enabled=1;}
    if(!poll(&ip,&m,&in,&pr,&c)){fprintf(stderr,"poll rejected\n");return 2;}
    printf("command %u displays %u f600 %u\n",(unsigned)c,s.calls,(unsigned)xeen_read16(&m,0xf600));
    return 0;
}
/* A one-slot cache: map 1 in F46A slot 0, every cell seen (C128 bits), the
 * surfaces C0EA (outdoor frames), walls by BDCC. */
static void map_ds(xeen_movement *m,int outdoor)
{
    unsigned i;
    memset(m->ds,0,sizeof m->ds);
    m->ds[0xed99]=1;xeen_write16(m,0xf46a,1);for(i=1;i<9u;++i)xeen_write16(m,(uint16_t)(0xf46au+2u*i),0xffff);
    xeen_write16(m,0xf608,outdoor?1u:0u);xeen_write16(m,0xf454,0x30);xeen_write16(m,0xf458,0x18);xeen_write16(m,0xf47c,3);
    for(i=0;i<256u;++i)xeen_write16(m,(uint16_t)(0xbdccu+2u*i),(uint16_t)(0x1235u+i));
    memset(m->ds+0xc128,0xff,32);for(i=0;i<16u;++i)m->ds[0xc0eau+i]=(uint8_t)(0x40u+i);
    for(i=0;i<32u;++i)m->ds[0x4f8eu+i]=(uint8_t)(31u-i);
}
static int tests(void)
{
    static xeen_movement m;static xeen_presentation pr;static xeen_map_draws d;
    xeen_input_poll ip;xeen_input_queue q;xeen_input_provider in;shown s;uint16_t c;xeen_map_view v;unsigned i;
    present_init(&pr);current=&pr;memset(&s,0,sizeof s);pr.display=display;pr.display_user=&s;
    xeen_input_poll_init(&ip,0x4853);xeen_input_queue_init(&q);in=xeen_input_queue_provider(&q);
    m.ds[0xed8c]=6;tables(&m);
    /* S by key: pressed (frame 1) then released (frame 0), each flip shown. */
    CHECK(xeen_input_queue_key(&q,0x53)&&poll(&ip,&m,&in,&pr,&c)&&c==0x53);
    CHECK(s.calls==2&&s.reasons[0]==XEEN_DISPLAY_RETRACE&&s.reasons[1]==XEEN_DISPLAY_RETRACE&&s.frames[0]==1&&s.frames[1]==0);
    /* The activation is returned once: the next poll has no key. */
    CHECK(poll(&ip,&m,&in,&pr,&c)&&c==0&&s.calls==2);
    /* M (the Map icon, index 6) by key: frames 13 then 12. */
    CHECK(xeen_input_queue_key(&q,0x4d)&&poll(&ip,&m,&in,&pr,&c)&&c==0x4d&&s.calls==4&&s.frames[2]==13&&s.frames[3]==12);
    /* '=' (disabled) and Z (no button): the key, no feedback. */
    CHECK(xeen_input_queue_key(&q,0x3d)&&poll(&ip,&m,&in,&pr,&c)&&c==0x3d&&s.calls==4);
    CHECK(xeen_input_queue_key(&q,0x5a)&&poll(&ip,&m,&in,&pr,&c)&&c==0x5a&&s.calls==4);
    /* Without 8C76 the keyboard branch draws nothing. */
    m.ds[0x8c76]=0;CHECK(xeen_input_queue_key(&q,0x53)&&poll(&ip,&m,&in,&pr,&c)&&c==0x53&&s.calls==4);m.ds[0x8c76]=1;
    /* A click on the Map icon: pressed, one display per 4E18F drain call
     * (two queued presses: two drained, the third call finds none), released. */
    q.current_mouse=(xeen_mouse_sample){240,120,0,1};
    CHECK(xeen_input_queue_mouse(&q,(xeen_mouse_sample){240,120,1,1})&&xeen_input_queue_mouse(&q,(xeen_mouse_sample){240,120,1,1})&&
          xeen_input_queue_mouse(&q,(xeen_mouse_sample){240,120,1,1}));
    CHECK(poll(&ip,&m,&in,&pr,&c)&&c==0x4d&&xeen_read16(&m,0xf600)==1);
    CHECK(s.calls==4u+1u+3u+1u&&s.frames[4]==13&&s.frames[8]==12);
    for(i=4;i<9u;++i)CHECK(s.reasons[i]==XEEN_DISPLAY_RETRACE);
    /* A click on the disabled minimap area and one in the 3D view: no feedback. */
    CHECK(xeen_input_queue_mouse(&q,(xeen_mouse_sample){240,20,1,1})&&poll(&ip,&m,&in,&pr,&c)&&c==0x3d&&s.calls==9);
    CHECK(xeen_input_queue_mouse(&q,(xeen_mouse_sample){100,60,1,1})&&poll(&ip,&m,&in,&pr,&c)&&c==1&&s.calls==9);
    /* No display hook (headless): the same draws, nothing else. */
    pr.display=NULL;CHECK(xeen_input_queue_key(&q,0x53)&&poll(&ip,&m,&in,&pr,&c)&&c==0x53&&s.calls==9);
    xeen_presentation_display(&pr,XEEN_DISPLAY_SCENE_END);CHECK(s.calls==9&&pr.displays==9);
    /* xeen_map_begin: x 3 with no west link: [BP-4] 7, arrow 3 x 10 + 4;
     * y 2 with no south link: [BP-6] 8, arrow (4F9E[2] << 3) + 0Dh. */
    map_ds(&m,1);m.ds[0xed97]=3;m.ds[0xed98]=2;
    CHECK(xeen_map_begin(&m,&v)==XEEN_OK&&v.x==7&&v.y==8&&v.arrow_x==34&&v.arrow_y==(uint8_t)((m.ds[0x4fa0]<<3)+0x0d)&&v.anim==6);
    /* x 12, y 12 with east and north links set: the centre, arrow 4Ah/45h. */
    xeen_write16(&m,0xc0d0,2);xeen_write16(&m,0xc0ce,2);m.ds[0xed97]=12;m.ds[0xed98]=12;
    CHECK(xeen_map_begin(&m,&v)==XEEN_OK&&v.x==12&&v.y==12&&v.arrow_x==0x4a&&v.arrow_y==0x45);
    /* A map outside the cache: rejected before any draw. */
    m.ds[0xed99]=7;CHECK(xeen_map_begin(&m,&v)==XEEN_CACHE_MISS);
    /* Outdoor passes on one slot, the arrow only with the blink. */
    map_ds(&m,1);m.ds[0xed97]=7;m.ds[0xed98]=8;m.ds[0xed96]=2;
    CHECK(xeen_map_begin(&m,&v)==XEEN_OK&&xeen_scene_map(&m,&v,&d)==XEEN_OK&&d.count==d.framed&&d.count>256u);
    CHECK(d.draws[0].handle==0x30&&d.draws[0].window==0&&d.draws[0].x==0x50&&d.draws[0].y==0x26);
    v.blink=1;CHECK(xeen_scene_map(&m,&v,&d)==XEEN_OK&&d.count==d.framed+1u&&d.draws[d.count-1u].handle==0x18&&d.draws[d.count-1u].frame==3);
    /* Indoor: [BP-2] = ([BP-2] + 2) mod 8 per pass, the 34h frame last. */
    map_ds(&m,0);m.ds[0xed97]=7;m.ds[0xed98]=8;
    CHECK(xeen_map_begin(&m,&v)==XEEN_OK&&xeen_scene_map(&m,&v,&d)==XEEN_OK&&v.anim==0&&d.count==d.framed+1u);
    CHECK(d.draws[d.count-1u].frame==0x34&&d.draws[d.count-1u].x==0x4c&&d.draws[d.count-1u].y==0x1e);
    CHECK(xeen_scene_map(&m,&v,&d)==XEEN_OK&&v.anim==2);
    puts("phase137 ui tests passed");
    return 0;
}
int main(int argc,char **argv)
{
    if(argc>=4 && (!strcmp(argv[1],"--feedback") || !strcmp(argv[1],"--click")))return probe(argc,argv);
    return tests();
}
