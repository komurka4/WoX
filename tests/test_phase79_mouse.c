/* Phase79: the native mouse. The pointer map shared by the X11 and Win32
 * backends (letterbox, resize, 4:3, edges, outside), the pointer event
 * policy (motion, left/right presses alike, release no event, the INT 33h
 * range, the 19-entry LIFO press stack of 4E172/4E18F) and the 189E3 click
 * path over the original's own tables (18C0C: portraits with 98D6, the 1BC4
 * default, buttons with press feedback and the drain of queued presses,
 * disabled buttons, combat monster columns; 18A48: a 3D-view click with the
 * main tables is command 1). The tables are the XEEN.DAT DS bytes at
 * 197C..1A1D (43244's combat keys 1A0A) and 17B2 (portrait x). */
#include "xeen/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);return 1;}}while(0)
static const uint16_t xs[21]={235,260,286,235,260,286,235,260,286,109,235,260,286,235,260,286,236,239,239,239,0xffff};
static const uint8_t ys[20]={75,75,75,96,96,96,117,117,117,137,148,148,148,169,169,169,11,27,37,47};
static const uint8_t ws[20]={24,24,24,24,24,24,24,24,24,13,24,24,24,24,24,24,72,73,73,73};
static const uint8_t hs[20]={20,20,20,20,20,20,20,20,20,10,20,20,20,20,20,20,58,10,10,10};
static const uint8_t keys[20]={'F','C','A','U','R','B','O','I','Q',9,0xf0,0xf2,0xf1,0xb0,0xf3,0xb1,0,'1','2','3'};
static const uint8_t on[20]={1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0};
static const uint8_t portraits[6]={10,45,81,117,153,189};
static void far(xeen_movement *m,uint16_t word,uint16_t offset){xeen_write16(m,word,offset);xeen_write16(m,(uint16_t)(word+2u),0x4853);}
static void tables(xeen_movement *m,uint16_t list)
{
    unsigned i;
    for(i=0;i<21u;++i)xeen_write16(m,(uint16_t)(0x197cu+2u*i),xs[i]);
    memcpy(m->ds+0x19a6,ys,20);memcpy(m->ds+0x19ba,ws,20);memcpy(m->ds+0x19ce,hs,20);
    memcpy(m->ds+0x1a0a,keys,20);memcpy(m->ds+0x19f6,on,20);memcpy(m->ds+0x17b2,portraits,6);
    far(m,0x98ac,list);far(m,0x98a8,0x19a6);far(m,0x98a4,0x19ba);far(m,0x98a0,0x19ce);far(m,0x989c,0x1a0a);far(m,0x9898,0x19f6);
    xeen_write16(m,0x9896,0x18);
}
/* A press at logical (x, y) through the platform's pointer entry (scale 3). */
static int press(xeen_platform *p,unsigned x,unsigned y,int button)
{return xeen_platform_mouse_event(p,(int)(x*3u+1u),(int)(y*3u+1u),button,1);}
static void live(void *u,uint16_t *x,uint16_t *y){(void)u;*x=50;*y=60;}
static int poll(xeen_input_poll *ip,xeen_movement *m,const xeen_input_provider *in,xeen_presentation *pr,uint16_t *command)
{
    xeen_input_poll_result r;
    if(xeen_input_poll_run(ip,m,in,NULL,NULL,pr,&r)!=XEEN_OK)return 0;
    *command=r.command;return 1;
}
int main(void)
{
    char error[160];unsigned lx,ly;uint16_t c,px,py;xeen_mouse_sample s;int i;
    xeen_platform *p=xeen_platform_open("phase79 mouse",3,1,error,sizeof error);
    xeen_movement *m=calloc(1,sizeof *m);xeen_presentation *pr=calloc(1,sizeof *pr);uint8_t *pixels=calloc(1,64000);
    xeen_input_poll ip;xeen_input_provider in;uint64_t drawn;
    CHECK(p&&m&&pr&&pixels);in=xeen_platform_input(p);
    /* The shared map: the inverse of the presentation's sampling. */
    CHECK(xeen_platform_map_pointer(960,600,0,0,0,&lx,&ly)&&lx==0&&ly==0);
    /* Phase 332, [display] locked-scale: a 1920x1080 window shows 5x unlocked (1600x1000 at (160,40)) but 3x locked
     * (960x600 at (480,240)), 960x720 at (480,180) with the aspect; a smaller window still fits (640x400: 2x). */
    CHECK(xeen_platform_map_pointer(1920,1080,0,160,40,&lx,&ly)&&lx==0&&ly==0);
    CHECK(xeen_platform_map_pointer_locked(1920,1080,0,0,160,40,&lx,&ly)&&lx==0&&ly==0);
    CHECK(!xeen_platform_map_pointer_locked(1920,1080,0,3,479,240,&lx,&ly)&&!xeen_platform_map_pointer_locked(1920,1080,0,3,480,239,&lx,&ly));
    CHECK(xeen_platform_map_pointer_locked(1920,1080,0,3,480,240,&lx,&ly)&&lx==0&&ly==0);
    CHECK(xeen_platform_map_pointer_locked(1920,1080,0,3,1439,839,&lx,&ly)&&lx==319&&ly==199);
    CHECK(!xeen_platform_map_pointer_locked(1920,1080,0,3,1440,839,&lx,&ly));
    CHECK(xeen_platform_map_pointer_locked(1920,1080,1,3,480,180,&lx,&ly)&&lx==0&&ly==0);
    CHECK(xeen_platform_map_pointer_locked(1920,1080,1,3,1439,899,&lx,&ly)&&lx==319&&ly==199);
    CHECK(xeen_platform_map_pointer_locked(640,400,0,3,639,399,&lx,&ly)&&lx==319&&ly==199);
    CHECK(xeen_platform_map_pointer(960,600,0,959,599,&lx,&ly)&&lx==319&&ly==199);
    CHECK(!xeen_platform_map_pointer(960,600,0,960,10,&lx,&ly)&&!xeen_platform_map_pointer(960,600,0,-1,10,&lx,&ly));
    CHECK(xeen_platform_map_pointer(960,600,0,298*3+2,85*3,&lx,&ly)&&lx==298&&ly==85);
    /* Resized to 1000x700: 3x, letterboxed at (20,50); the margins are outside. */
    CHECK(!xeen_platform_map_pointer(1000,700,0,19,60,&lx,&ly)&&!xeen_platform_map_pointer(1000,700,0,500,49,&lx,&ly));
    CHECK(xeen_platform_map_pointer(1000,700,0,20,50,&lx,&ly)&&lx==0&&ly==0);
    CHECK(xeen_platform_map_pointer(1000,700,0,979,649,&lx,&ly)&&lx==319&&ly==199);
    CHECK(!xeen_platform_map_pointer(1000,700,0,980,649,&lx,&ly)&&!xeen_platform_map_pointer(1000,700,0,979,650,&lx,&ly));
    /* A window smaller than 320x200 shrinks the image (200x125 at y 12). */
    CHECK(!xeen_platform_map_pointer(200,150,0,10,11,&lx,&ly));
    CHECK(xeen_platform_map_pointer(200,150,0,0,12,&lx,&ly)&&lx==0&&ly==0);
    CHECK(xeen_platform_map_pointer(200,150,0,199,136,&lx,&ly)&&lx==318&&ly==198);
    /* The 4:3 correction: rows stretched to 240 (960x720 at 3x). */
    CHECK(xeen_platform_map_pointer(960,720,1,0,719,&lx,&ly)&&lx==0&&ly==199);
    CHECK(xeen_platform_map_pointer(960,720,1,480,360,&lx,&ly)&&lx==160&&ly==100);
    CHECK(!xeen_platform_map_pointer(960,720,1,480,720,&lx,&ly));
    CHECK(xeen_platform_map_pointer(1100,800,1,70,40,&lx,&ly)&&lx==0&&ly==0);     /* 3x, offset (70,40) */
    /* No mouse before an event (headless). */
    CHECK(in.mouse(in.user,&s)&&!s.enabled&&!s.click);
    /* A motion moves the pointer, held in the INT 33h range (x 2..318 - w). */
    CHECK(xeen_platform_mouse_event(p,0,0,0,0)&&in.mouse(in.user,&s)&&s.enabled&&!s.click&&s.x==2&&s.y==2);
    /* A position taken before the range (the SC-55 start-up's window: x up to 318) is moved into the range
     * when it is set, with a press still queued: no later motion is needed (the pointer outside the window). */
    CHECK(xeen_platform_mouse_event(p,959,599,0,0)&&xeen_platform_pointer(p,&px,&py)&&px==318&&py==198);
    CHECK(press(p,319,199,1));
    xeen_platform_set_pointer_limits(p,9,11);
    CHECK(xeen_platform_pointer(p,&px,&py)&&px==309&&py==187);
    CHECK(in.mouse(in.user,&s)&&s.click==1&&s.x==309&&s.y==187);
    CHECK(xeen_platform_mouse_event(p,959,599,0,0)&&in.mouse(in.user,&s)&&s.x==309&&s.y==187&&!s.click);
    /* Presses (left or right alike) are queued; a release is no event; the
     * stack pops the newest first and holds 19. Outside points: no event. */
    CHECK(press(p,100,50,1)&&xeen_platform_mouse_event(p,301,151,1,0)&&press(p,200,60,2));
    CHECK(!xeen_platform_mouse_event(p,961,10,1,1)&&!xeen_platform_mouse_event(p,10,10,3,1));
    CHECK(in.mouse(in.user,&s)&&s.click==1&&s.x==200&&s.y==60);
    CHECK(in.mouse(in.user,&s)&&s.click==1&&s.x==100&&s.y==50);
    CHECK(in.mouse(in.user,&s)&&!s.click&&s.x==200&&s.y==60);          /* then the pointer, at the last event */
    for(i=0;i<19;++i)CHECK(press(p,50,50,1));
    CHECK(!press(p,50,50,1));
    for(i=0;i<19;++i)CHECK(in.mouse(in.user,&s)&&s.click);
    CHECK(in.mouse(in.user,&s)&&!s.click);
    /* The 189E3 click path. */
    pr->enabled=1;pr->dirty=0xffff;pr->framebuffer=pixels;pr->framebuffer_size=64000;
    pr->cursor_pixels=pixels;pr->cursor_opacity=pixels+1;pr->cursor_width=1;pr->cursor_height=1;pr->cursor_size=1;
    xeen_input_poll_init(&ip,0x4853);m->ds[0xed8c]=6;
    tables(m,0x197c);m->ds[0x9a1c]=2;
    /* The Attack button (A, index 2): its key, pressed and released (4AC92
     * frames 5 and 4, window 27h), F600 = 1, F602/F604 = the point. */
    drawn=pr->sprite_submissions;
    CHECK(press(p,298,85,1)&&poll(&ip,m,&in,pr,&c)&&c=='A'&&xeen_read16(m,0xf5fe)=='A'&&xeen_read16(m,0xf600)==1);
    CHECK(xeen_read16(m,0xf604)==298&&xeen_read16(m,0xf602)==85&&pr->sprite_submissions==drawn+2&&pr->sprites[0].frame==4);
    /* Block (B, index 5) by the right button; queued presses drained by its
     * feedback (the newest is the one tested). */
    CHECK(press(p,240,80,1)&&press(p,240,80,1)&&press(p,298,106,2));
    CHECK(poll(&ip,m,&in,pr,&c)&&c=='B'&&in.mouse(in.user,&s)&&!s.click);
    /* A disabled entry (16: the minimap's, 236,11 72x58) still gives its key
     * (0 in the combat table) without feedback; the 3D view is command 1
     * with the main tables, the monster columns notwithstanding. */
    drawn=pr->sprite_submissions;
    CHECK(press(p,300,30,1)&&poll(&ip,m,&in,pr,&c)&&c==0&&pr->sprite_submissions==drawn);
    CHECK(press(p,100,60,1)&&poll(&ip,m,&in,pr,&c)&&c==1);
    CHECK(press(p,226,60,1)&&poll(&ip,m,&in,pr,&c)&&c==0);                  /* x E2h: outside the view */
    /* Other tables in combat: the monster columns (y 2Ah..7Fh) give 0Eh +
     * column; with 98B5 set and 98B6 clear from column 3. */
    far(m,0x98ac,0x1c10);xeen_write16(m,0x1c10,0xffff);
    CHECK(press(p,100,60,1)&&poll(&ip,m,&in,pr,&c)&&c==0x0e);
    m->ds[0x98b5]=1;m->ds[0x98b6]=0;
    CHECK(press(p,100,60,1)&&poll(&ip,m,&in,pr,&c)&&c==0x12);
    CHECK(press(p,100,130,1)&&poll(&ip,m,&in,pr,&c)&&c==0);
    m->ds[0x9a1c]=1;m->ds[0x98b5]=0;
    CHECK(press(p,100,60,1)&&poll(&ip,m,&in,pr,&c)&&c==0);
    /* Portraits first when 98D6 is set (y 96h..B5h, x 17B2[i]..+1Fh): F1..F6. */
    tables(m,0x197c);m->ds[0x98d6]=1;
    CHECK(press(p,46,160,1)&&poll(&ip,m,&in,pr,&c)&&c==0xca);
    CHECK(press(p,189,181,1)&&poll(&ip,m,&in,pr,&c)&&c==0xce);
    m->ds[0x98d6]=0;
    /* The default tables (1BC4): any click is 1. */
    far(m,0x98ac,0x1bc4);
    CHECK(press(p,5,5,1)&&poll(&ip,m,&in,pr,&c)&&c==1);
    /* A key with the combat table and 8C76: the same button pressed and
     * released (18AC5), no mouse involved; keys without a button pass. */
    tables(m,0x197c);m->ds[0x8c76]=1;drawn=pr->sprite_submissions;
    CHECK(xeen_platform_queue_key(p,XKEY_A)&&poll(&ip,m,&in,pr,&c)&&c=='A'&&pr->sprite_submissions==drawn+2);
    CHECK(xeen_platform_queue_key(p,XKEY_Z)&&poll(&ip,m,&in,pr,&c)&&c=='Z'&&pr->sprite_submissions==drawn+2);
    /* A present reads the live pointer for its cursor (4E295 before each
     * cursor draw): the cursor follows the mouse between polls too. */
    pr->pointer=live;pr->pointer_user=NULL;
    CHECK(xeen_presentation_update_window(pr,0)==XEEN_OK&&pr->cursor_x==50&&pr->cursor_y==60);
    pr->pointer=NULL;
    xeen_input_poll_destroy(&ip);xeen_platform_close(p);free(m);free(pr);free(pixels);
    puts("phase79 mouse: map, policy, press stack and the 18C0C/18A48 click path");
    return 0;
}
