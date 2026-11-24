#include "xeen/scene.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do {if(!(c)){fprintf(stderr,"outdoor line%d: %s\n",__LINE__,#c);return 1;}}while(0)
int main(void) {
    xeen_movement m;uint8_t before[65536];xeen_caller_result r;
    r=xeen_scene_scan_view_walls(NULL);CHECK(r.status==XEEN_BAD_INPUT);
    xeen_movement_init(&m,NULL,NULL);xeen_write16(&m,0xf608,1);
    memcpy(before,m.ds,sizeof before);r=xeen_scene_scan_view_walls(&m);
    CHECK(r.status==XEEN_BAD_INPUT && !memcmp(before,m.ds,sizeof before));
    xeen_write16(&m,0xf608,0);m.ds[0xed99]=1;
    r=xeen_scene_scan_view_walls(&m);CHECK(r.status==XEEN_CACHE_MISS && r.boundary==0x1c1c1);
    /* Isolated cache/party fixture, not original runtime initialization. */
    xeen_movement_init(&m,NULL,NULL);m.ds[0xed99]=1;m.ds[0xed97]=8;m.ds[0xed98]=8;
    xeen_write16(&m,0xf46a,1);m.ds[0x3067]=1;m.ds[0xed8c]=1;m.ds[0xb449]=1;
    m.ds[0xbfcc+8*16+8]=0x20;xeen_write16(&m,0xeff8,1);xeen_write16(&m,0xc0d8,0x4000);
    r=xeen_scene_scan_view_walls(&m);CHECK(r.status==XEEN_SERVICE_REQUIRED && r.boundary==0x1bbdf);
    CHECK(xeen_read16(&m,0xeff8)==0 && xeen_read16(&m,0xf60c)==0);
    CHECK((m.ds[0xc128+17]&0x80)!=0);
    /* Visibility refresh does not consume light or reveal cells if3067=0. */
    m.ds[0x3067]=0;m.ds[0xc128+17]=0;xeen_write16(&m,0xeff8,2);
    r=xeen_scene_scan_view_walls(&m);CHECK(r.boundary==0x1bbdf);
    CHECK(xeen_read16(&m,0xeff8)==2 && xeen_read16(&m,0xf60c)==4 && !m.ds[0xc128+17]);
    puts("Outdoor isolated light/visibility and failure contracts passed");return 0;
}
