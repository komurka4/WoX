/* Phase125: the cell metadata 1C844 and the view query 1C1C1 over the DS
 * image, as the mirror "&" reaches them (Clouds map 4Dh, (56,56), facing
 * 50h; xeen_dat_evidence/phase125/w3/metadata-1c844.asm via phase124,
 * navq-1c1c1.asm, navq-tail2.asm). SYNTHETIC DS: map 77 alone in the F46A
 * cache (its link words 0), the cell byte the original read (C5F0, slot 1)
 * set to a pattern; the original byte there was 0 (run1's metadata
 * observer). Also: a miss in the nine slots stays XEEN_CACHE_MISS (the
 * DS-wide search is not verified), and 1C4A3's SAR by the 7CE byte. */
#include "xeen/movement.h"
#include <stdio.h>
#include <string.h>
static int failures;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);++failures;}}while(0)
static xeen_movement m;
static uint16_t w(uint16_t a){return xeen_read16(&m,a);}

int main(void)
{
    uint16_t ax=0,t;
    xeen_movement_init(&m,NULL,NULL);
    /* map 77 in slot 0, every other slot id 0 (no neighbours) */
    memset(m.ds+0xf46a,0,18);xeen_write16(&m,0xf46a,77);
    m.ds[0xed99]=77;m.ds[0xed97]=56;m.ds[0xed98]=56;m.ds[0xed96]=0x50;
    /* y: bit 4 set, >= 0: 40, link C0CE + 0 = 0 -> slot 1 (id 0); x: 40, link C0D0 + 37Ch = 0 -> slot 1 */
    m.ds[0xc5f0]=0xb5;
    CHECK(xeen_metadata(&m)==XEEN_OK);
    CHECK(w(0xf494)==1);
    CHECK(w(0xf48a)==0x80 && w(0xf488)==0 && w(0xf486)==0x20 && w(0xf482)==0x10 && w(0xf480)==0 && w(0xf47e)==5);
    /* the original's byte (0): no flag, no event */
    m.ds[0xc5f0]=0;
    CHECK(xeen_metadata(&m)==XEEN_OK && w(0xf482)==0 && w(0xf47e)==0);
    /* an ordinary in-range cell of slot 0 is unchanged */
    m.ds[0xbfcc+3*16+2]=0x0f;
    CHECK(xeen_metadata_at(&m,2,3)==XEEN_OK && w(0xf494)==0 && w(0xf47e)==7 && w(0xf480)==8);
    /* a link to an id no slot holds: the nine-slot miss is kept */
    xeen_write16(&m,0xbdcc+0x302,0x1234);
    CHECK(xeen_metadata_at(&m,2,20)==XEEN_CACHE_MISS);
    xeen_write16(&m,0xbdcc+0x302,0);
    /* 1C1C1 with ED96 = 50h: t = 50h x 30h + 7, the offset bytes 64E/70E + t
     * of the DS image; 56 + 2 > 31: the off-map answer 8888 (F490 0 on map 77) */
    t=(uint16_t)(0x50u*48u+7u);m.status=XEEN_OK;
    m.ds[(uint16_t)(0x64e + t)]=2;m.ds[(uint16_t)(0x70e + t)]=0xfe;
    CHECK(xeen_navigation_query(&m,7,&ax)==XEEN_OK && ax==0x8888 && w(0xf48e)==0x8888 && w(0xf490)==0);
    /* 1C1C1 has its own zero-link test (1C2B9 JE 1C2D9): through map 77's zero links the off-map answer */
    m.ds[(uint16_t)(0x64e + t)]=(uint8_t)(-40);m.ds[(uint16_t)(0x70e + t)]=(uint8_t)(-40);
    CHECK(xeen_navigation_query(&m,7,&ax)==XEEN_OK && ax==0x8888 && w(0xf490)==0);
    /* within slot 0: an offset of -50 puts x and y at 6; the wall word 9001h, the 7CE + 2t byte 0Eh:
     * SAR AX, 14 = FFFEh, & 15 = 14 (a logical shift would give 2) */
    m.ds[(uint16_t)(0x64e + t)]=(uint8_t)(-50);m.ds[(uint16_t)(0x70e + t)]=(uint8_t)(-50);
    xeen_write16(&m,0xbdcc+6*32+6*2,0x9001);
    m.ds[0xbfcc+6*16+6]=3;m.ds[(uint16_t)(0x7ce + 2u*t)]=14;
    CHECK(xeen_navigation_query(&m,7,&ax)==XEEN_OK && ax==14 && w(0xf48e)==0x9001 && w(0xf490)==3);
    /* a count byte of 31 (masked to 5 bits) fills with the sign: 15 */
    m.ds[(uint16_t)(0x7ce + 2u*t)]=0x3f;
    CHECK(xeen_navigation_query(&m,7,&ax)==XEEN_OK && ax==15);
    if(failures)fprintf(stderr,"%d failure(s)\n",failures);
    else puts("phase125 mirror metadata and view query: PASS");
    return failures!=0;
}
