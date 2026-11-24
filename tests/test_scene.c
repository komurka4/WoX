#include "xeen/scene.h"
#include <stdio.h>
#define CHECK(c) do {if(!(c)){fprintf(stderr,"scene check failed line%d\n",__LINE__);return 1;}}while(0)
int main(void) {
    xeen_movement m;
    xeen_caller_result r;
    xeen_movement_init(&m,0,0);
    m.ds[0x9877]=1;m.ds[0xf5e0]=3;
    r=xeen_scene_after_timer(&m);
    CHECK(r.status==XEEN_SERVICE_REQUIRED && r.boundary==0x17b09 && m.ds[0xf5e0]==3);
    m.ds[0x9877]=0;m.ds[0x9c5a]=2;
    m.ds[0xe003]=4;m.ds[0xe00b]=0xff;
    m.ds[0xaa]=2;m.ds[0xa2]=1;m.ds[0xa6]=9;
    r=xeen_scene_after_timer(&m);
    CHECK(r.boundary==0x1b70f && m.ds[0xf5e0]==0);
    CHECK(m.ds[0xe003]==1 && m.ds[0xe00b]==0 && m.ds[0xe006]==9 && m.ds[0xe00e]==9);
    m.ds[0x98d9]=2;m.ds[0x9755]=1;
    r=xeen_scene_after_timer(&m);
    /* Phase95: 1B5CA is native: a non-chest object takes the ordinary step;
     * a chest sprite (Clouds 10h) skips the limit and wraps above 4 to 1. */
    CHECK(r.boundary==0x1b70f && m.ds[0xe003]==2 && m.ds[0xe00b]==1);
    m.ds[0xe00c]=0x10;m.ds[0xe00b]=5;m.ds[0xe003]=1;
    r=xeen_scene_after_timer(&m);
    CHECK(r.boundary==0x1b70f && m.ds[0xe003]==2 && m.ds[0xe00b]==1);
    m.ds[0xe00b]=3;
    r=xeen_scene_after_timer(&m);
    CHECK(r.boundary==0x1b70f && m.ds[0xe00b]==4);
    m.ds[0xe00c]=0;m.ds[0xe00b]=1;
    m.ds[0x3068]=1;
    r=xeen_scene_after_timer(&m);
    CHECK(r.boundary==0x1b70f && m.ds[0xe003]==1 && m.ds[0xe00b]==1);
    m.ds[0x96c0]=1;m.ds[0x9a1c]=1;m.ds[0x96c4]=1;
    r=xeen_scene_after_timer(&m);
    CHECK(r.status==XEEN_SERVICE_REQUIRED && r.boundary==0x2a701 && m.ds[0x96c0]==0);
    r=xeen_scene_after_timer(0);CHECK(r.status==XEEN_BAD_INPUT);
    puts("bounded scene tests passed");return 0;
}
