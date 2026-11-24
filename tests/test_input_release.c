#include "xeen/monster_update.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#c);return 1;} } while(0)
/* Phase72: a scripted 504DA raw stream for 2B5E9's 38ADB draws. */
static uint16_t raws[8];static uint16_t seen_ax[8];static unsigned draws;
static uint16_t scripted(void *u,uint16_t ax){(void)u;seen_ax[draws&7u]=ax;return raws[draws++&7u];}
static void initial(xeen_movement *m) {
    memset(m,0,sizeof *m);
    m->ds[0x96c4]=1;m->ds[0x96c0]=3;
    m->ds[0xed97]=14;m->ds[0xed98]=4;m->ds[0x9a1c]=1;
    m->ds[0x9c59]=1;m->ds[0xe530]=0;m->ds[0xe531]=0;
}
int main(int argc,char **argv) {
    xeen_movement *m=malloc(sizeof *m);
    uint32_t boundary=0;
    CHECK(m);
    CHECK(xeen_input_release_96c0_extended(NULL,&boundary)==XEEN_BAD_INPUT);
    initial(m);CHECK(xeen_input_release_96c0_extended(m,NULL)==XEEN_BAD_INPUT);
    initial(m);CHECK(xeen_input_release_96c0(m,&boundary)==XEEN_SERVICE_REQUIRED && boundary==0x2a727);
    initial(m);memset(m->ds+0x97d0,0,0x24);m->ds[0x974c]=1;
    CHECK(xeen_input_release_96c0_extended(m,&boundary)==XEEN_OK && !boundary);
    CHECK(m->ds[0x96c0]==0 && m->ds[0x974c]==0);
    CHECK(m->ds[0x8da6]==1 && m->ds[0x8da7]==0);
    CHECK(m->ds[0x97d0]==255 && m->ds[0x97f3]==255);
    initial(m);m->ds[0xe530]=14;m->ds[0xe531]=4;
    CHECK(xeen_input_release_96c0_extended(m,&boundary)==XEEN_OK && m->ds[0x974c]==1);
    initial(m);m->ds[0xe530]=14;m->ds[0xe531]=4;m->ds[0xe534]=1;
    CHECK(xeen_input_release_96c0_extended(m,&boundary)==XEEN_SERVICE_REQUIRED && boundary==0x2a87b);
    CHECK(m->ds[0x96c0]==0 && m->ds[0x974c]==1);
    /* Phase 40: an active unaligned monster reaches the original 2A908
     * movement. Facing 0, scan cell 18 (dx +1, dy +1) tries the 56C4/5726
     * step west (direction code 3, 1C680 nibble shift 0) first and the 5788
     * vertical step (code 2, shift 4) second. Table entries are the original
     * DS values for cell 18; the map words are zero. */
    initial(m);m->ds[0xed99]=41;m->ds[0xf46a]=41;
    m->ds[0x56c4+36]=0xff;m->ds[0x56c5+36]=0xff;m->ds[0x5788+36]=0xff;m->ds[0x5789+36]=0xff;
    m->ds[0x57ea+18]=3;m->ds[0x581b+18]=2;m->ds[0x279e + 4]=4;m->ds[0x279e + 6]=0;
    m->ds[0xe530]=15;m->ds[0xe531]=5;m->ds[0xe534]=1;
    CHECK(xeen_input_release_96c0_extended(m,&boundary)==XEEN_OK && !boundary);
    CHECK(m->ds[0xe530]==15 && m->ds[0xe531]==5 && !m->ds[0xfa25]); /* C0FC 0: both blocked */
    initial(m);m->ds[0xed99]=41;m->ds[0xf46a]=41;m->ds[0xc0fc]=7;
    m->ds[0x56c4+36]=0xff;m->ds[0x56c5+36]=0xff;m->ds[0x5788+36]=0xff;m->ds[0x5789+36]=0xff;
    m->ds[0x57ea+18]=3;m->ds[0x581b+18]=2;m->ds[0x279e + 4]=4;m->ds[0x279e + 6]=0;
    m->ds[0xe530]=15;m->ds[0xe531]=5;m->ds[0xe534]=1;
    CHECK(xeen_input_release_96c0_extended(m,&boundary)==XEEN_OK && !boundary);
    CHECK(m->ds[0xe530]==14 && m->ds[0xe531]==5 && m->ds[0xfa25]==1 && !m->ds[0xf9ba]);
    CHECK(m->ds[0x8da6+5*64+14*2]==1 && m->ds[0x8da6+5*64+15*2]==0);
    /* Phase195: 2A532 has no range check. The party crossing a map edge
     * (x = -1); the monster at (0,5) steps west to x = -1, so 2A548..2A562
     * address 8DA6 + 5*64 - 2 (row 4's word 31) in DOS word arithmetic, and
     * 2A5B9/2A5C5 store x = FFh. Was the explicit native stop 2A564. */
    initial(m);m->ds[0xed99]=41;m->ds[0xf46a]=41;m->ds[0xc0fc]=7;m->ds[0xed97]=0xff;
    m->ds[0x56c4+36]=0xff;m->ds[0x56c5+36]=0xff;m->ds[0x5788+36]=0xff;m->ds[0x5789+36]=0xff;
    m->ds[0x57ea+18]=3;m->ds[0x581b+18]=2;m->ds[0x279e + 4]=4;m->ds[0x279e + 6]=0;
    m->ds[0xe530]=0;m->ds[0xe531]=5;m->ds[0xe534]=1;
    CHECK(xeen_input_release_96c0_extended(m,&boundary)==XEEN_OK && !boundary);
    CHECK(m->ds[0xe530]==0xff && m->ds[0xe531]==5 && m->ds[0xfa25]==1);
    CHECK(m->ds[0x8da6+5*64-2]==1 && m->ds[0x8da6+5*64]==0);
    /* Negative control: three records at (31,4) fill that same word, so
     * 2A564's occupancy test (>= 3) refuses the step. */
    initial(m);m->ds[0xed99]=41;m->ds[0xf46a]=41;m->ds[0xc0fc]=7;m->ds[0xed97]=0xff;m->ds[0x9c59]=4;
    m->ds[0x56c4+36]=0xff;m->ds[0x56c5+36]=0xff;m->ds[0x5788+36]=0xff;m->ds[0x5789+36]=0xff;
    m->ds[0x57ea+18]=3;m->ds[0x581b+18]=2;m->ds[0x279e + 4]=4;m->ds[0x279e + 6]=0;
    m->ds[0xe530]=0;m->ds[0xe531]=5;m->ds[0xe534]=1;
    for(int k=1;k<4;++k){m->ds[0xe530+20*k]=31;m->ds[0xe531+20*k]=4;}
    CHECK(xeen_input_release_96c0_extended(m,&boundary)==XEEN_OK && !boundary);
    CHECK(m->ds[0xe530]==0 && m->ds[0xe531]==5 && !m->ds[0xfa25] && m->ds[0x8da6+5*64-2]==3);
    initial(m);m->ds[0xe530]=14;m->ds[0xe531]=4;m->ds[0x9a1c]=5;
    CHECK(xeen_input_release_96c0_extended(m,&boundary)==XEEN_SERVICE_REQUIRED && boundary==0x2a87b);
    initial(m);m->ds[0xe537]=3;
    CHECK(xeen_input_release_96c0_extended(m,&boundary)==XEEN_SERVICE_REQUIRED && boundary==0x2b612);
    initial(m);m->ds[0xe537]=13;
    CHECK(xeen_input_release_96c0_extended(m,&boundary)==XEEN_OK);
    /* Phase72: 2B5E9 with the session RNG. Records in order, off-map ones
     * too; 38ADB(id) = random(1, id + 50) <= id clears +7; 13 never draws. */
    initial(m);m->ds[0x9c59]=3;
    m->ds[0xe530]=0x80;m->ds[0xe531]=0x80;m->ds[0xe536]=35;m->ds[0xe537]=7;      /* off map, Sleep */
    m->ds[0xe530+20]=0x80;m->ds[0xe531+20]=0x80;m->ds[0xe537+20]=13;             /* Dragon Sleep */
    m->ds[0xe530+40]=0x80;m->ds[0xe531+40]=0x80;m->ds[0xe536+40]=0;m->ds[0xe537+40]=12; /* id 0 never saves */
    draws=0;raws[0]=39;raws[1]=0;                      /* 39 % 85 + 1 = 40 > 35; 0 % 50 + 1 = 1 > 0 */
    CHECK(xeen_input_release_96c0_database_rng(m,NULL,0,scripted,NULL,&boundary)==XEEN_OK && !boundary);
    CHECK(draws==2 && seen_ax[0]==85 && seen_ax[1]==50);
    CHECK(m->ds[0xe537]==7 && m->ds[0xe537+20]==13 && m->ds[0xe537+40]==12);
    initial(m);m->ds[0x9c59]=1;m->ds[0xe530]=0x80;m->ds[0xe531]=0x80;m->ds[0xe536]=35;m->ds[0xe537]=7;
    draws=0;raws[0]=34;                                /* 34 % 85 + 1 = 35 <= 35 */
    CHECK(xeen_input_release_96c0_database_rng(m,NULL,0,scripted,NULL,&boundary)==XEEN_OK && draws==1 && !m->ds[0xe537]);
    initial(m);m->ds[0xe537]=7;
    CHECK(xeen_input_release_96c0_database_rng(m,NULL,0,NULL,NULL,&boundary)==XEEN_SERVICE_REQUIRED && boundary==0x2b612);
    initial(m);m->ds[0x97f6]=1;
    CHECK(xeen_input_release_96c0_extended(m,&boundary)==XEEN_SERVICE_REQUIRED && boundary==0x2aab1);
    initial(m);m->ds[0x96c4]=0;
    CHECK(xeen_input_release_96c0_extended(m,&boundary)==XEEN_OK && m->ds[0x96c0]==3);
    initial(m);m->ds[0x97f9]=1;
    CHECK(xeen_input_release_96c0_extended(m,&boundary)==XEEN_OK && m->ds[0x97f8]==1);
    if(argc==2){
        FILE *f=fopen(argv[1],"rb");CHECK(f);
        CHECK(fread(m->ds,1,sizeof m->ds,f)==sizeof m->ds);CHECK(fclose(f)==0);
        m->ds[0x96c4]=1;m->ds[0x97f9]=0;m->ds[0x97f6]=0;
        CHECK(xeen_input_release_96c0_extended(m,&boundary)==XEEN_OK && !boundary);
    }
    free(m);puts("input release guards, occupancy, filtered scan and explicit boundaries, 2B5E9 recovery passed");return 0;
}
