#include "xeen/entities.h"
#include <assert.h>
#include <string.h>
int main(void) {
    xeen_movement m;uint8_t mob[60];uint16_t next=0;
    memset(&m,0,sizeof m);memset(mob,255,sizeof mob);
    mob[2]=85;mob[48]=6;mob[49]=4;mob[50]=2;mob[51]=1;
    m.ds[0xe006]=0xab;
    assert(xeen_entities_objects(mob,55,&m,&next)==XR_TRUNCATED);
    assert(m.ds[0x9c5a]==0);
    assert(xeen_entities_objects(mob,sizeof mob,&m,&next)==XR_OK);
    assert(next==1 && m.ds[0x9c5a]==1);
    assert(m.ds[0xe000]==6 && m.ds[0xe001]==4 && m.ds[0xe002]==2);
    assert(m.ds[0xe003]==100 && m.ds[0xe004]==85 && m.ds[0xe005]==1);
    assert(m.ds[0xe006]==0xab);
    {
        uint8_t animation[1452];
        memset(animation,0x5a,sizeof animation);
        assert(xeen_entities_animation(animation,1451,&m)==XR_BAD_INDEX);
        assert(xeen_entities_animation(animation,sizeof animation,&m)==XR_OK);
        assert(m.ds[0xa2]==0x5a && m.ds[0xa2+1451]==0x5a);
    }
    return 0;
}
