#include "xeen/scene.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do {if(!(c)){fprintf(stderr,"restore line%d: %s\n",__LINE__,#c);exit(1);}}while(0)
int main(void) {
    static const uint16_t destinations[4]={0,0x10b2,0x106a,0x108e};
    xeen_movement *m=malloc(sizeof *m);uint8_t *expected=malloc(65536);unsigned g,i;
    CHECK(m && expected);
    for(g=0;g<4;g++) {
        xeen_caller_result r;
        xeen_movement_init(m,NULL,NULL);
        for(i=0;i<65536;i++)m->ds[i]=(uint8_t)(i*13u+i/256u);
        memcpy(expected,m->ds,65536);
        if(g)memcpy(expected+destinations[g],expected+0x10d6,36);
        r=xeen_scene_restore_group(m,(uint16_t)g);
        CHECK(r.status==XEEN_SERVICE_REQUIRED && r.boundary==0x1b0f6);
        CHECK(!memcmp(expected,m->ds,65536));
    }
    CHECK(xeen_scene_restore_group(m,4).status==XEEN_BAD_INPUT);
    CHECK(!memcmp(expected,m->ds,65536));
    CHECK(xeen_scene_restore_group(NULL,0).status==XEEN_BAD_INPUT);
    free(m);free(expected);puts("Isolated saved-group restoration0..3, exact untouched bytes and invalid rejection");return 0;
}
