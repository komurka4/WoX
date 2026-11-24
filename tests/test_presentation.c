#include "xeen/presentation.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#c); exit(1); } } while (0)
static xeen_presentation p, before;
static uint8_t fb[64000], original[64000];
int main(void) {
    uint8_t pixels[6] = {9,8,7,6,5,4}, mask[6] = {1,0,1,1,1,0};
    char long_text[257];
    unsigned i;
    for (i=0;i<64000;++i) fb[i]=(uint8_t)i;
    memcpy(original,fb,sizeof fb);
    p.framebuffer=fb;p.framebuffer_size=sizeof fb;
    p.cursor_pixels=pixels;p.cursor_opacity=mask;p.cursor_size=6;
    p.cursor_width=3;p.cursor_height=2;p.old_x=12;p.old_y=4;
    p.cursor_x=12;p.cursor_y=4;p.sampled_x=17;p.sampled_y=6;
    p.dirty=0xffff;p.enabled=1;
    CHECK(xeen_presentation_present(&p)==XEEN_OK);
    CHECK(!memcmp(fb,original,sizeof fb));
    CHECK(p.page_selector==0x4000 && p.displayed_page==1);
    CHECK(p.completed_frames==1 && p.synchronization_count==1);
    CHECK(p.copied_pixels==16);
    CHECK(p.pages[1][6*320+17]==9);
    CHECK(p.pages[1][6*320+18]==original[6*320+18]);
    CHECK(p.pages[1][6*320+19]==7);
    CHECK(p.pages[1][6*320+16]==original[6*320+16]);
    CHECK(p.pages[1][4*320+12]==original[4*320+12]);
    CHECK(p.pages[1][0]==0 && p.pages[0][6*320+17]==0);
    p.sampled_x=24;CHECK(xeen_presentation_present(&p)==XEEN_OK);
    CHECK(p.page_selector==0 && p.completed_frames==2);
    p.dirty=0;
    CHECK(xeen_presentation_present(&p)==XEEN_OK);
    CHECK(p.dirty==0xffff && p.completed_frames==3 && p.ops[0][0]==XEEN_PRESENT_OP_FULL);
    CHECK(!memcmp(fb,original,sizeof fb));
    CHECK(p.pages[p.displayed_page][0]==original[0] && p.pages[p.displayed_page][199*320+319]==original[199*320+319]);
    p.dirty=1;before=p;
    CHECK(xeen_presentation_present(&p)==XEEN_SERVICE_REQUIRED);
    CHECK(!memcmp(&p,&before,sizeof p));
    p.dirty=0xffff;p.decoder=1;before=p;
    CHECK(xeen_presentation_present(&p)==XEEN_SERVICE_REQUIRED);
    CHECK(!memcmp(&p,&before,sizeof p));
    p.decoder=0;p.cursor_pixels=NULL;before=p;
    CHECK(xeen_presentation_present(&p)==XEEN_BAD_INPUT);
    CHECK(!memcmp(&p,&before,sizeof p));CHECK(!memcmp(fb,original,sizeof fb));
    p.enabled=0;before=p;CHECK(xeen_presentation_present(&p)==XEEN_OK);
    CHECK(!memcmp(&p,&before,sizeof p));
    p.enabled=1;p.cursor_pixels=pixels;p.cursor_width=3;
    p.old_x=317;p.old_y=198;p.cursor_x=317;p.cursor_y=198;
    p.sampled_x=317;p.sampled_y=198;
    CHECK(xeen_presentation_present(&p)==XEEN_OK);
    CHECK(p.pages[p.displayed_page][198*320+317]==9);
    p.sampled_x=318;before=p;
    CHECK(xeen_presentation_present(&p)==XEEN_BAD_INPUT);
    CHECK(!memcmp(&p,&before,sizeof p));
    CHECK(xeen_presentation_define_window(&p,39,10,10,20,30)==XEEN_OK);
    p.sampled_x=100;p.sampled_y=100;p.old_x=p.cursor_x=100;p.old_y=p.cursor_y=100;
    p.dirty=0xffff;
    CHECK(xeen_presentation_update_window(&p,39)==XEEN_OK && p.dirty==78);
    CHECK(p.ops[0][1]==10 && p.ops[0][2]==10 && p.ops[0][3]==20 && p.ops[0][4]==30);
    before=p;CHECK(xeen_presentation_define_window(&p,39,319,199,2,2)==XEEN_BAD_INPUT);
    CHECK(!memcmp(&p,&before,sizeof p));
    memset(long_text,'X',200);long_text[200]=0;
    CHECK(xeen_presentation_text(&p,2,long_text)==XEEN_OK);
    CHECK(p.text_count==1 && !strcmp(p.texts[0],long_text));
    memset(long_text,'Y',256);long_text[256]=0;before=p;
    CHECK(xeen_presentation_text(&p,2,long_text)==XEEN_BAD_INPUT);
    CHECK(!memcmp(&p,&before,sizeof p));
    puts("Native presentation: page ownership, cursor overlay/restore, padded regions, atomic unsupported rejection");
    /* Phase154: a restore reaches both display pages (no flip). */
    {static uint8_t fb2[64000];xeen_presentation q;memset(&q,0,sizeof q);q.framebuffer=fb2;q.framebuffer_size=sizeof fb2;
     memset(q.pages,7,sizeof q.pages);memset(fb2,9,sizeof fb2);
     xeen_presentation_restore_rect(&q,10,20,30,40);
     CHECK(q.pages[0][20*320+10]==9 && q.pages[1][59*320+39]==9 && q.pages[0][19*320+10]==7 && q.pages[1][20*320+40]==7);
     CHECK(q.displayed_page==0);}
    return 0;
}
