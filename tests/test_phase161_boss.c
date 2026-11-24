/* Phase161 (frontend, an owner-authorized modernization; native evidence
 * only, never compared with the original's 2675C DOS prompt): the window
 * icons and the F10 boss screen's contracts, headless.
 *  1. the icon tables: the owner's BMP area-averaged (4x4 at 64), and a
 *     distinct calculator icon; the X11 _NET_WM_ICON payload;
 *  2. the calculator image: its own palette, no game pixel;
 *  3. F10 / any key: title, icon, shown image and palette swap and restore
 *     exactly; F10, its repeats and releases, the dismissing key and its
 *     repeats are consumed; keys queued before stay queued in order; mouse
 *     events while it shows are none; the hook sees every change;
 *  4. repeated round trips, a custom title, the game clock's pause. */
#include "xeen/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int failures;
#define CHECK(c) do{if(!(c)){fprintf(stderr,"%s:%d: CHECK failed: %s\n",__FILE__,__LINE__,#c);++failures;}}while(0)
static int hook_log[64];static unsigned hook_n;
static void hook(void *u,int active){(void)u;if(hook_n<64u)hook_log[hook_n++]=active;}
static uint64_t fake_now;
static uint64_t fake_clock(void *u){(void)u;return fake_now;}
static uint8_t *read_file(const char *path,size_t *n)
{
    FILE *f=fopen(path,"rb");long k;uint8_t *b;
    if(!f)return NULL;
    fseek(f,0,SEEK_END);k=ftell(f);fseek(f,0,SEEK_SET);
    b=malloc((size_t)k);if(b&&fread(b,1,(size_t)k,f)!=(size_t)k){free(b);b=NULL;}
    fclose(f);*n=(size_t)k;return b;
}
static unsigned long key_count(xeen_input_provider in,uint16_t *keys,unsigned max)
{
    unsigned long n=0;uint16_t k;
    while(in.keyboard(in.user,&k)&&k){if(n<max)keys[n]=k;++n;}
    return n;
}
int main(int argc,char **argv)
{
    static uint8_t game[64000],game_pal[768],shown[64000],pal[768],calc[64000],calc_pal[768];
    char error[160];size_t n,i;xeen_platform_boss_info info;uint16_t keys[16];
    const xeen_icon_image *wox=xeen_platform_icon_images(0,&n),*cal;size_t cn;
    xeen_platform *p;xeen_input_provider in;
    /* 1. Icons (X11 tables; a Windows build has none: its resource). */
#if defined(_WIN32)
    CHECK(!xeen_platform_icon_images(1,&cn)&&n==0u&&cn==0u&&!xeen_platform_x11_icon_payload(0,NULL,0));
    (void)wox;(void)cal;(void)argc;(void)argv;(void)read_file;
#else
    CHECK(wox&&n>=4u);cal=xeen_platform_icon_images(1,&cn);CHECK(cal&&cn==n);
    {int have16=0,have32=0,have48=0,have64=0,diff=0;
     for(i=0;i<n;++i){have16|=wox[i].size==16u;have32|=wox[i].size==32u;have48|=wox[i].size==48u;have64|=wox[i].size==64u;
        CHECK(cal[i].size==wox[i].size&&wox[i].size<=64u);
        for(size_t j=0;j<(size_t)wox[i].size*wox[i].size;++j)diff+=wox[i].argb[j]!=cal[i].argb[j];}
     CHECK(have16&&have32&&have48&&have64&&diff>1000);}
    if(argc>1){size_t bn;uint8_t *bmp=read_file(argv[1],&bn);int seen=0;
        CHECK(bmp&&bn==54u+256u*256u*4u&&bmp[0]=='B'&&bmp[1]=='M');
        for(i=0;bmp&&i<n;++i)if(wox[i].size==64u){
            unsigned bad=0;seen=1;
            for(unsigned y=0;y<64u;++y)for(unsigned x=0;x<64u;++x){
                uint32_t px=wox[i].argb[y*64u+x];
                for(unsigned c=0;c<3u;++c){unsigned sum=0;
                    for(unsigned dy=0;dy<4u;++dy)for(unsigned dx=0;dx<4u;++dx)
                        sum+=bmp[54u+((255u-(4u*y+dy))*256u+4u*x+dx)*4u+(2u-c)];   /* BGRA, bottom-up */
                    {unsigned want=(sum+8u)/16u,got=(px>>(16u-8u*c))&255u;if(got+1u<want||got>want+1u)++bad;}}
                if((px>>24)!=255u)++bad;                  /* the BMP is opaque */
            }
            CHECK(bad==0);
        }
        CHECK(seen);free(bmp);}
    {size_t total=xeen_platform_x11_icon_payload(0,NULL,0),k=0;unsigned long *pay=malloc(total*sizeof *pay);
     CHECK(total>0&&pay&&xeen_platform_x11_icon_payload(0,pay,total)==total&&!xeen_platform_x11_icon_payload(0,pay,total-1u));
     for(i=0;pay&&i<n;++i){CHECK(pay[k]==wox[i].size&&pay[k+1u]==wox[i].size&&pay[k+2u]==wox[i].argb[0]);k+=2u+(size_t)wox[i].size*wox[i].size;}
     CHECK(k==total);free(pay);}
#endif
    /* 2. The calculator image. */
    xeen_boss_render(calc,calc_pal);
    {unsigned used[256]={0},kinds=0;for(i=0;i<64000u;++i)if(!used[calc[i]]++)++kinds;
     CHECK(kinds>=6u&&kinds<=9u);for(i=9;i<256u;++i)CHECK(!used[i]);
     CHECK(calc[0]==1u&&calc_pal[3]==60u&&calc_pal[4]==60u&&calc_pal[5]==60u);   /* the light background */
     CHECK(used[4]>0u);}                                                         /* the '=' accent */
    /* 3. The round trip. */
    p=xeen_platform_open("World of Xeen",2,1,error,sizeof error);CHECK(p!=NULL);if(!p)return 1;
    in=xeen_platform_input(p);xeen_platform_set_boss_hook(p,hook,NULL);
    for(i=0;i<64000u;++i)game[i]=(uint8_t)(i*7u+i/320u);
    for(i=0;i<768u;++i)game_pal[i]=(uint8_t)((i*5u)&63u);
    CHECK(xeen_platform_present(p,game,game_pal));
    xeen_platform_shown(p,shown,pal);CHECK(!memcmp(shown,game,64000)&&!memcmp(pal,game_pal,768));
    xeen_platform_boss_info_get(p,&info);CHECK(!info.active&&info.icon==0&&!strcmp(info.title,"World of Xeen"));
    CHECK(xeen_platform_key_event(p,XKEY_UP,1,0));                 /* typed ahead before F10 */
    CHECK(xeen_platform_key_event(p,XKEY_F10,1,0));                /* consumed: not queued */
    xeen_platform_boss_info_get(p,&info);CHECK(info.active&&info.icon==1&&!strcmp(info.title,"Calculator")&&info.entries==1u);
    xeen_platform_shown(p,shown,pal);CHECK(!memcmp(shown,calc,64000)&&!memcmp(pal,calc_pal,768));
    CHECK(xeen_platform_key_event(p,XKEY_F10,1,1)&&xeen_platform_key_event(p,XKEY_F10,0,0));   /* repeat, release */
    CHECK(xeen_platform_mouse_event(p,320,200,1,1));               /* a click: no event */
    {xeen_mouse_sample m;CHECK(in.mouse(in.user,&m)&&!m.click);}
    xeen_platform_boss_info_get(p,&info);CHECK(info.active);
    /* the game frame presented while it shows is kept for the return, not shown */
    game[100]^=0x55u;CHECK(xeen_platform_present(p,game,game_pal));
    xeen_platform_shown(p,shown,pal);CHECK(!memcmp(shown,calc,64000));
    CHECK(xeen_platform_key_event(p,XKEY_A,1,0));                  /* any key hides it */
    xeen_platform_boss_info_get(p,&info);CHECK(!info.active&&info.icon==0&&!strcmp(info.title,"World of Xeen")&&info.exits==1u);
    xeen_platform_shown(p,shown,pal);CHECK(!memcmp(shown,game,64000)&&!memcmp(pal,game_pal,768));
    CHECK(!xeen_platform_key_event(p,XKEY_A,1,1));                 /* its repeat: dropped */
    CHECK(xeen_platform_key_event(p,XKEY_A,0,0));                  /* its release */
    CHECK(key_count(in,keys,16)==1u&&keys[0]==0xf2);               /* only the key typed before */
    CHECK(xeen_platform_key_event(p,XKEY_A,1,0)&&key_count(in,keys,16)==1u&&keys[0]=='A');   /* A works again */
    CHECK(hook_n==2u&&hook_log[0]==1&&hook_log[1]==0);
    /* A fresh press after the dismissal (no release seen, e.g. focus lost) is a key. */
    CHECK(xeen_platform_key_event(p,XKEY_F10,1,0)&&xeen_platform_key_event(p,XKEY_ENTER,1,0));
    CHECK(xeen_platform_key_event(p,XKEY_B,1,0)&&key_count(in,keys,16)==1u&&keys[0]=='B');
    /* 4. Repeated round trips; a dismissing F10 (any key) and Escape. */
    for(i=0;i<5u;++i){
        static const xeen_native_key dismiss[5]={XKEY_F10,XKEY_GAME_ESCAPE,XKEY_SPACE,XKEY_PGDN,XKEY_F1};
        CHECK(xeen_platform_key_event(p,XKEY_F10,1,0)&&xeen_platform_key_event(p,XKEY_F10,0,0));
        xeen_platform_shown(p,shown,pal);CHECK(!memcmp(shown,calc,64000));
        CHECK(xeen_platform_key_event(p,dismiss[i],1,0)&&xeen_platform_key_event(p,dismiss[i],0,0));
        xeen_platform_shown(p,shown,pal);CHECK(!memcmp(shown,game,64000)&&!memcmp(pal,game_pal,768));
    }
    CHECK(key_count(in,keys,16)==0u&&!xeen_platform_should_close(p));
    xeen_platform_boss_info_get(p,&info);CHECK(info.entries==7u&&info.exits==7u&&!strcmp(info.title,"World of Xeen"));
    CHECK(hook_n==14u);
    xeen_platform_close(p);
    /* A custom title is restored exactly; the game clock stops while it shows. */
    p=xeen_platform_open("Xeen [dev] \xc3\xa9",1,1,error,sizeof error);CHECK(p!=NULL);if(!p)return 1;
    fake_now=1000000u;xeen_platform_set_test_clock(p,fake_clock,NULL);
    fake_now=3000000u;CHECK(xeen_platform_clock_ns(p)==3000000u);
    CHECK(xeen_platform_key_event(p,XKEY_F10,1,0));
    fake_now=9000000u;CHECK(xeen_platform_clock_ns(p)==3000000u);       /* stopped */
    CHECK(xeen_platform_key_event(p,XKEY_X,1,0));
    CHECK(xeen_platform_clock_ns(p)==3000000u);                         /* no catch-up */
    fake_now=9500000u;CHECK(xeen_platform_clock_ns(p)==3500000u);
    xeen_platform_boss_info_get(p,&info);CHECK(!strcmp(info.title,"Xeen [dev] \xc3\xa9"));
    {uint16_t k;in=xeen_platform_input(p);CHECK(in.keyboard(in.user,&k)&&k==0);}
    xeen_platform_close(p);
    if(failures)fprintf(stderr,"%d failure(s)\n",failures);
    else puts("phase161 boss screen and icons: PASS");
    return failures?1:0;
}
