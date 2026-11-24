#include "xeen/movement.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static xeen_movement m;
static struct { xeen_service s[128]; uint16_t a[128]; unsigned n; uint16_t ability[16],event; } logbook;
static int stub(void *user,xeen_movement *state,xeen_service s,uint16_t a,uint16_t b,uint16_t *r) {
    (void)user; (void)b; CHECK(logbook.n<128);
    logbook.s[logbook.n]=s; logbook.a[logbook.n++]=a;
    *r=0;
    if(s==XEEN_ABILITY) *r=logbook.ability[a];
    if(s==XEEN_EVENT) *r=logbook.event;
    if(s==XEEN_LOAD_MAP) state->ds[0xed99]=(uint8_t)a;
    if(s==XEEN_SCENE && state->ds[0x9c6e]) CHECK(state->ds[0xed98]==8);
    return 1;
}
#define B(a) m.ds[a]
#define W(a) xeen_read16(&m,a)
#define S(a,v) xeen_write16(&m,(uint16_t)(a),(uint16_t)(v))
static void fixture(unsigned d,unsigned mode) {
    unsigned i; memset(&logbook,0,sizeof(logbook)); xeen_movement_init(&m,stub,NULL);
    xeen_navigation_set(&m,(xeen_navigation){(uint8_t)d,8,8,1});
    S(0xf46a,1); S(0xf46c,2); S(0xf608,mode); S(0xeff4,100); B(0xc0fc)=1;
    for(i=0;i<256;i++) B(0xbfcc+i)=1;
    B(0xc0eb)=1;
}
static void gates(void) {
    unsigned d,n,c,cmd; uint16_t a;
    for(d=0;d<4;d++) for(n=0;n<3;n++) {
        fixture(d,0); S(0xbdcc+8*32+8*2,n<<(12-4*d));
        CHECK(xeen_translation_gate(&m,0xf2,&a)==XEEN_OK); CHECK(a==(n==0)); CHECK(B(0xed96)==d);
    }
    for(c=0;c<16;c++) {
        fixture(0,0x8000); S(0xbdcc+9*32+8*2,(c<<4)|1); B(0xc0eb)=1;
        CHECK(xeen_translation_gate(&m,0xf2,&a)==XEEN_OK);
        CHECK(a==(c==0||c==2||c==4||c==5||c==8||c==11||c==13||c==14));
        logbook.ability[9]=1; logbook.ability[11]=1;
        CHECK(xeen_translation_gate(&m,0xf2,&a)==XEEN_OK); CHECK(a==(c<15));
    }
    for(c=0;c<4;c++) for(n=0;n<4;n++) {
        static const uint8_t classes[]={15,8,0,1};
        fixture(0,0x8000); S(0xbdcc+9*32+8*2,1); B(0xc0eb)=classes[c];
        B(0xeda2)=(uint8_t)(n&1u); logbook.ability[14]=(uint16_t)(n>>1);
        CHECK(xeen_translation_gate(&m,0xf2,&a)==XEEN_OK);
        CHECK(a==(c==3 || (c==1 && (n&1u)) || (c==2 && n)));
    }
    /* Audit every classification byte on the ordinary type-0 branch.
     * Class 15 must reject even with both potential overrides enabled. */
    for(c=0;c<256;c++) {
        fixture(0,0x8000); S(0xbdcc+9*32+8*2,1); B(0xc0eb)=(uint8_t)c;
        B(0xeda2)=1; logbook.ability[14]=1;
        CHECK(xeen_translation_gate(&m,0xf2,&a)==XEEN_OK);
        CHECK(a==(c!=15));
        if(c==15) {
            CHECK(logbook.n==1 && logbook.s[0]==XEEN_SOUND && logbook.a[0]==0x15);
            CHECK(B(0xed96)==0 && B(0xed97)==8 && B(0xed98)==8 && W(0xeff4)==100);
        }
    }
    for(n=0;n<4;n++) {
        fixture(0,0); B(0xbfcc+9*16+8)=4; B(0xeda2)=(uint8_t)(n&1u); logbook.ability[14]=(uint16_t)(n>>1);
        CHECK(xeen_translation_gate(&m,0xf2,&a)==XEEN_OK); CHECK(a==(n!=0));
    }
    for(d=0;d<4;d++) for(cmd=0;cmd<3;cmd++) for(n=0;n<2;n++) {
        static const uint16_t commands[]={0xf3,0xb0,0xb1};
        unsigned q=cmd==0?(d^2u):cmd==1?(d+3u)&3u:(d+1u)&3u;
        fixture(d,0); S(0xbdcc+8*32+8*2,n<<(12-4*q));
        CHECK(xeen_translation_gate(&m,commands[cmd],&a)==XEEN_OK); CHECK(a==(n==0)); CHECK(B(0xed96)==d);
    }
    fixture(0,0); B(0xf516)=1; CHECK(xeen_translation_gate(&m,0xf2,&a)==XEEN_OK && a);
    CHECK(logbook.n==2 && logbook.s[0]==XEEN_SOUND && logbook.a[0]==0x2f && logbook.s[1]==XEEN_SCENE && B(0x9c6e)==0);
}
static void commands(void) {
    unsigned d; uint16_t a;
    static const uint8_t xs[]={8,9,8,7},ys[]={9,8,7,8};
    static const xeen_service order[]={XEEN_TIME_CONDITIONS,XEEN_CLOCK_TAIL,XEEN_SOUND,XEEN_WORLD_UPDATE,XEEN_SCENE};
    for(d=0;d<4;d++) {
        fixture(d,0); CHECK(xeen_process_forward(&m,&a)==XEEN_OK && a);
        CHECK(B(0xed97)==xs[d] && B(0xed98)==ys[d] && B(0xed96)==d && B(0xed99)==1);
        CHECK(W(0xeff4)==101 && W(0xefee)==1 && B(0x3067)==1 && W(0xf5e4)==0x8000 && B(0xf5df)==1);
        CHECK(logbook.n==5 && memcmp(logbook.s,order,sizeof(order))==0);
    }
    fixture(0,0); S(0xbdcc+8*32+8*2,0x1000); CHECK(xeen_process_forward(&m,&a)==XEEN_OK && !a);
    CHECK(B(0xed98)==8 && W(0xeff4)==100 && !W(0xefee));
    CHECK(logbook.n==3 && logbook.s[0]==XEEN_SOUND && logbook.a[0]==0x2e && logbook.s[1]==XEEN_WORLD_UPDATE);
    fixture(0,0); B(0xbfcc+9*16+8)=0x11; logbook.event=7;
    CHECK(xeen_process_forward(&m,&a)==XEEN_OK); CHECK(W(0xf482)==16 && W(0xf64a)==7);
    CHECK(logbook.n==4 && logbook.s[3]==XEEN_EVENT);
    fixture(0,0); S(0xefee,23); CHECK(xeen_process_forward(&m,&a)==XEEN_OK && W(0xefee)==0);
    fixture(0,0); m.service=NULL; CHECK(xeen_process_forward(&m,&a)==XEEN_SERVICE_REQUIRED);
}
static void boundaries(void) {
    unsigned d,mode; uint16_t a; static const unsigned link[]={0x302,0x304,0x306,0x308};
    for(d=0;d<4;d++) {
        fixture(d,0x8000); B(0xed97)=(uint8_t)(d==1?15:d==3?0:8); B(0xed98)=(uint8_t)(d==0?15:d==2?0:8);
        S(0xbdcc+link[d],2); B(0xc0ea)=15;
        CHECK(xeen_navigation_query(&m,7,&a)==XEEN_OK && B(0xed99)==1);
        xeen_forward_delta(&m); CHECK(xeen_boundary_commit(&m)==XEEN_OK);
        CHECK(B(0xed99)==2 && (d%2?B(0xed97):B(0xed98))==(d<2?0:15));
        CHECK(logbook.n==1 && logbook.s[0]==XEEN_LOAD_MAP && logbook.a[0]==2);
        fixture(d,0x8000); B(0xed97)=(uint8_t)(d==1?15:d==3?0:8); B(0xed98)=(uint8_t)(d==0?15:d==2?0:8);
        S(0xbdcc+link[d],2); B(0xc0ea)=0; logbook.ability[14]=1;
        CHECK(xeen_process_forward(&m,&a)==XEEN_OK && a && B(0xed99)==2);
        CHECK(logbook.s[3]==XEEN_LOAD_MAP);
    }
    for(mode=0;mode<2;mode++) {
        fixture(0,mode); B(0xed98)=15; CHECK(xeen_navigation_query(&m,7,&a)==XEEN_OK);
        CHECK(a==(mode?0:0x8888) && W(0xf490)==(mode?15:0));
    }
    fixture(0,1); B(0xed98)=16; CHECK(xeen_boundary_commit(&m)==XEEN_OK);
    CHECK(B(0xed98)==0 && B(0xed99)==1 && logbook.n==0);
    fixture(0,0); B(0xed98)=31; CHECK(xeen_navigation_query(&m,7,&a)==XEEN_OK && a==0x8888);
    B(0xed99)=0x19; CHECK(xeen_navigation_query(&m,7,&a)==XEEN_OK && W(0xf490)==7);
    fixture(0,0); B(0xed99)=3; CHECK(xeen_navigation_query(&m,2,&a)==XEEN_CACHE_MISS);
    fixture(3,0); B(0xed97)=0; xeen_forward_delta(&m); CHECK(B(0xed97)==255 && xeen_signed_byte(B(0xed97))==-1);
    fixture(1,0); B(0xed97)=255; xeen_forward_delta(&m); CHECK(B(0xed97)==0);
    /* diagonal uses y neighbor's x link */
    fixture(0,1); S(0xf46e,3); S(0xbdcc+0x302,2); S(0xbdcc+0x37c+0x304,3);
    B(0xed97)=16; B(0xed98)=16; CHECK(xeen_boundary_commit(&m)==XEEN_OK && B(0xed99)==3 && W(0xf494)==2);
}
static void time_and_post(void) {
    unsigned mode; uint16_t a;
    for(mode=0;mode<2;mode++) {
        fixture(0,mode); S(0xeff4,1439); S(0xeff0,99); S(0xeff2,65535);
        CHECK(xeen_charge_translation_time(&m)==XEEN_OK);
        CHECK(W(0xeff4)==(mode?9:0) && W(0xeff0)==0 && W(0xeff2)==0 && B(0x96c0)==3);
    }
    fixture(0,0); B(0xf60a)=1; CHECK(xeen_charge_translation_time(&m)==XEEN_OK && !logbook.n && W(0xeff4)==100);
    fixture(0,1); B(0xc0ea)=6; CHECK(xeen_post_translation(&m)==XEEN_OK && W(0xeff4)==270);
    fixture(0,0); B(0xc0eb)=15; CHECK(xeen_post_translation(&m)==XEEN_OK && B(0xf60a)==1);
    fixture(0,0); B(0xc0eb)=5; CHECK(xeen_post_translation(&m)==XEEN_OK && W(0x972e)==2);
    CHECK(logbook.n==2 && logbook.s[0]==XEEN_SCENE && logbook.s[1]==XEEN_TILE_EFFECT && logbook.a[1]==100);
    fixture(0,0); B(0xc0eb)=10; CHECK(xeen_post_translation(&m)==XEEN_OK && B(0x8c85)==1 && logbook.s[0]==XEEN_RELOCATE);
    fixture(0,0); CHECK(xeen_metadata(&m)==XEEN_OK && W(0xf47e)==1);
    fixture(0,1); B(0x94)=1; S(0xbdcc+9*32+8*2,0x51); B(0xc0eb)=15;
    CHECK(xeen_translation_gate(&m,0xf2,&a)==XEEN_OK && !a); CHECK(logbook.s[0]==XEEN_ABILITY && logbook.a[0]==9);
}
int main(void) { gates(); commands(); boundaries(); time_and_post(); puts("movement contracts passed"); return 0; }
