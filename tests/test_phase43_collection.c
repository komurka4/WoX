#include "xeen/treasure.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"phase43 collection check line %d\n",__LINE__);return 1;}}while(0)
int main(int argc,char **argv)
{
    xeen_movement *m=calloc(1,sizeof *m);CHECK(m);
    if(argc==4){
        FILE *f=fopen(argv[1],"rb");CHECK(f && fread(m->ds,1,65536,f)==65536);fclose(f);
        xeen_status status=xeen_treasure_collect(m,(unsigned)strtoul(argv[3],NULL,10));
        f=fopen(argv[2],"wb");CHECK(f && fwrite(m->ds,1,65536,f)==65536);fclose(f);
        printf("%d\n",(int)status);free(m);return 0;
    }
    m->ds[0xed8c]=2;
    /* Slot8 occupied means capacity unavailable, even with earlier holes.
     * Phase50 correction (original 2EEA7, rules case weapon-category-full):
     * with no free last slot, member 0's slot 8 is overwritten. */
    m->ds[0xb41eu+0xc7]=7;m->ds[0xb41eu+0x162+0xc7]=8;
    m->ds[0x99e9]=2;m->ds[0x9a10]=1;m->ds[0x9a16]=9;
    uint8_t before[65536];memcpy(before,m->ds,65536);
    CHECK(xeen_treasure_collect(m,1)==XEEN_OK);
    /* 43428 then compacts member 0's only weapon from slot 8 into slot 0. */
    CHECK(m->ds[0xb41eu+0xa7]==2 && !m->ds[0xb41eu+0xc7] && m->ds[0xb41eu+0x162+0xc7]==8 && m->ds[0xf00a]==9);
    memcpy(m->ds,before,65536);
    /* Free recipient1 pack, keep recipient0 full. Party-order fallback. */
    memset(m->ds+0xb41eu+0x162+0xa6,0,36);
    CHECK(xeen_treasure_collect(m,1)==XEEN_OK);
    CHECK(m->ds[0xb41eu+0xc7]==7 && m->ds[0xb41eu+0x162+0xa7]==2);
    CHECK(m->ds[0xf00a]==9 && !m->ds[0x9a10]);
    CHECK(xeen_treasure_collect(m,1)==XEEN_OK && m->ds[0xf00a]==9);
    /* Phase50 correction (original 2EED7, rules case quest-weapon-52): a
     * quest item selects a recipient but is not inserted; it is cleared. */
    m->ds[0x99e9]=0x52;m->ds[0x9a10]=1;memcpy(before,m->ds,65536);
    CHECK(xeen_treasure_collect(m,0)==XEEN_OK);
    CHECK(!memcmp(before+0xb41e,m->ds+0xb41e,2u*0x162u) && !m->ds[0x99e9] && !m->ds[0x9a10]);
    free(m);puts("phase43 full last slot, retry-safe boundary, fallback and exactly-once collection passed");return 0;
}
