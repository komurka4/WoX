#include "xeen/platform.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);return 1;}}while(0)
int main(void)
{
    char error[160];xeen_platform *p=xeen_platform_open("headless test",2,1,error,sizeof error);
    xeen_input_provider input;xeen_mouse_sample mouse;uint16_t key;
    xeen_timer_registers timer={0x1200,0,0,0x246};
    uint8_t *pixels=calloc(64000u,1),palette[768]={0};
    CHECK(p&&pixels&&!error[0]);input=xeen_platform_input(p);
    CHECK(xeen_platform_queue_key(p,XKEY_UP));
    CHECK(xeen_platform_queue_key(p,XKEY_RIGHT));
    CHECK(xeen_platform_queue_key(p,XKEY_LEFT));
    CHECK(input.mouse(input.user,&mouse)&&!mouse.enabled&&!mouse.click);
    CHECK(input.keyboard(input.user,&key)&&key==0xf2);
    CHECK(input.keyboard(input.user,&key)&&key==0xf1);
    CHECK(input.keyboard(input.user,&key)&&key==0xf0);
    CHECK(input.keyboard(input.user,&key)&&key==0);
    CHECK(xeen_platform_bios(p,&timer)==XEEN_OK&&timer.dx==0&&timer.ax==0x1200&&timer.flags==0x246);
    xeen_platform_wait(p,100);
    CHECK(xeen_platform_bios(p,&timer)==XEEN_OK&&timer.dx==0);
    CHECK(xeen_platform_advance(p,XEEN_BIOS_TICKS_PER_DAY+17u)==XEEN_OK);
    CHECK(xeen_platform_bios(p,&timer)==XEEN_OK&&timer.dx==17&&timer.ax==0x1201&&timer.flags==0x246);
    CHECK(xeen_platform_bios(p,&timer)==XEEN_OK&&timer.dx==17&&timer.ax==0x1200);
    CHECK(xeen_platform_present(p,pixels,palette));
    CHECK(!xeen_platform_should_close(p));
    {   /* Phase78: the window backends' key maps and event policy. */
        xeen_native_key k;
        CHECK(xeen_platform_x11_key(0xff1b,&k)&&k==XKEY_GAME_ESCAPE);      /* Escape: the game's Esc */
        CHECK(xeen_platform_x11_key('x',&k)&&k==XKEY_X&&xeen_platform_x11_key('X',&k)&&k==XKEY_X);
        CHECK(xeen_platform_x11_key(0xff52,&k)&&k==XKEY_UP&&xeen_platform_x11_key(0xff97,&k)&&k==XKEY_UP);
        CHECK(xeen_platform_x11_key(0xff0d,&k)&&k==XKEY_ENTER&&xeen_platform_x11_key(0xffc3,&k)&&k==XKEY_F6);
        CHECK(xeen_platform_x11_key('7',&k)&&k==XKEY_7&&xeen_platform_x11_key(' ',&k)&&k==XKEY_SPACE);
        CHECK(xeen_platform_x11_key(0xff09,&k)&&k==XKEY_TAB&&xeen_platform_x11_key(0xff08,&k)&&k==XKEY_BACKSPACE);
        /* Phase139: F7/F8 are the original's CFh/D0h (4FEFF CS:7AF8); F11 (scan
         * 57h: 00h there) stays unmapped; Win32 VK_OEM_PLUS is '=' (17E20). */
        CHECK(!xeen_platform_x11_key(0xffc8,&k)&&!xeen_platform_x11_key(0x7f,&k)&&!xeen_platform_x11_key(0xffe1,&k));
        CHECK(xeen_platform_x11_key(0xffc4,&k)&&xeen_input_translate_key(k,0)==0xcf&&
              xeen_platform_x11_key(0xffc5,&k)&&xeen_input_translate_key(k,0)==0xd0);
        CHECK(xeen_platform_win32_key(0x77,&k)&&xeen_input_translate_key(k,0)==0xd0&&
              xeen_platform_win32_key(0xbb,&k)&&xeen_input_translate_key(k,0)=='=');
        /* Phase107: X11 punctuation keysyms are their characters (2894F's words). */
        CHECK(xeen_platform_x11_key('!',&k)&&xeen_input_translate_key(k,0)=='!'&&xeen_platform_x11_key('&',&k)&&
              xeen_input_translate_key(k,0)=='&'&&xeen_platform_x11_key('~',&k)&&xeen_input_translate_key(k,0)=='~');
        CHECK(xeen_platform_win32_key(0x1b,&k)&&k==XKEY_GAME_ESCAPE&&xeen_platform_win32_key('X',&k)&&k==XKEY_X);
        CHECK(xeen_platform_win32_key(0x26,&k)&&k==XKEY_UP&&xeen_platform_win32_key(0x75,&k)&&k==XKEY_F6);
        CHECK(xeen_platform_win32_key(0x09,&k)&&k==XKEY_TAB&&!xeen_platform_win32_key(0x7a,&k)&&!xeen_platform_win32_key(0x10,&k));
        CHECK(xeen_input_translate_key(XKEY_GAME_ESCAPE,0)==27&&xeen_input_translate_key(XKEY_X,0)=='X');
        /* Phase160: the keyboard driver's other extended commands (4FEFF's
         * CS:7AF8): Home DCh, End DDh, PgUp DEh, PgDn DFh, Ins E0h, F9 D1h;
         * the keypad's (NumLock off) are the same keys; F10 (D2h, 189E3's
         * own 2675C screen) stays unmapped in both backends (Phase161: the boss key). */
        {   static const unsigned long x11[][2]={{0xff50,0xdc},{0xff57,0xdd},{0xff55,0xde},{0xff56,0xdf},{0xff63,0xe0},
                {0xff95,0xdc},{0xff9c,0xdd},{0xff9a,0xde},{0xff9b,0xdf},{0xff9e,0xe0},{0xffc6,0xd1}};
            static const unsigned w32[][2]={{0x24,0xdc},{0x23,0xdd},{0x21,0xde},{0x22,0xdf},{0x2d,0xe0},{0x78,0xd1}};
            size_t i;
            for(i=0;i<sizeof x11/sizeof x11[0];++i)CHECK(xeen_platform_x11_key(x11[i][0],&k)&&xeen_input_translate_key(k,0)==x11[i][1]);
            for(i=0;i<sizeof w32/sizeof w32[0];++i)CHECK(xeen_platform_win32_key(w32[i][0],&k)&&xeen_input_translate_key(k,0)==w32[i][1]);
            /* Phase161: F10 is the boss key in both backends: no game command. */
            CHECK(xeen_platform_x11_key(0xffc7,&k)&&k==XKEY_F10&&xeen_platform_win32_key(0x79,&k)&&k==XKEY_F10&&
                  xeen_input_translate_key(XKEY_F10,0)==0);
            CHECK(xeen_input_translate_key(XKEY_PGDN,1)==0x1df&&xeen_input_translate_key((xeen_native_key)(XKEY_PGUP|XKEY_CTRL),0)==0x9e);
        }
        CHECK(xeen_platform_key_event(p,XKEY_UP,1,0));        /* a press is queued */
        CHECK(!xeen_platform_key_event(p,XKEY_UP,1,1));       /* its repeat behind it is not */
        CHECK(xeen_platform_key_event(p,XKEY_UP,0,0));        /* a release is no key */
        CHECK(input.keyboard(input.user,&key)&&key==0xf2);
        CHECK(input.keyboard(input.user,&key)&&key==0);
        CHECK(xeen_platform_key_event(p,XKEY_UP,1,1));        /* a repeat into an empty queue is */
        CHECK(xeen_platform_key_event(p,XKEY_GAME_ESCAPE,1,0)&&!xeen_platform_should_close(p));
        CHECK(input.keyboard(input.user,&key)&&key==0xf2);
        CHECK(input.keyboard(input.user,&key)&&key==27);
        CHECK(!xeen_platform_key_event(p,(xeen_native_key)999,1,0));
        /* Phase80: Ctrl+Left/Right through the frontend entry; plain arrows
         * still turn; a Ctrl release is no key. */
        CHECK(xeen_platform_key_event(p,(xeen_native_key)(XKEY_LEFT|XKEY_CTRL),1,0));
        CHECK(xeen_platform_key_event(p,(xeen_native_key)(XKEY_LEFT|XKEY_CTRL),0,0));
        CHECK(xeen_platform_key_event(p,XKEY_LEFT,1,0));
        CHECK(input.keyboard(input.user,&key)&&key==0xb0);
        CHECK(input.keyboard(input.user,&key)&&key==0xf0);
        CHECK(xeen_platform_key_event(p,(xeen_native_key)(XKEY_RIGHT|XKEY_CTRL),1,0));
        CHECK(!xeen_platform_key_event(p,(xeen_native_key)(XKEY_RIGHT|XKEY_CTRL),1,1));   /* its repeat behind it */
        CHECK(input.keyboard(input.user,&key)&&key==0xb1);
        CHECK(input.keyboard(input.user,&key)&&key==0);
        /* Phase160: the window backends' event entry takes the extended keys
         * (before, it refused every key past the punctuation: PgDn was lost);
         * the button codes 61h..7Ah stay headless-only. */
        CHECK(xeen_platform_key_event(p,XKEY_PGDN,1,0)&&xeen_platform_key_event(p,XKEY_HOME,1,0));
        CHECK(input.keyboard(input.user,&key)&&key==0xdf);
        CHECK(input.keyboard(input.user,&key)&&key==0xdc);
        CHECK(!xeen_platform_key_event(p,XKEY_LOWER_FIRST,1,0)&&!xeen_platform_key_event(p,(xeen_native_key)(XKEY_F10+1),1,0));
        CHECK(input.keyboard(input.user,&key)&&key==0);
        CHECK(xeen_platform_refresh(p,pixels,palette));       /* headless: nothing to show */
        /* Phase194: headless never repaints a host cursor on its own (the
         * window's pointer-only repaints are presentation, never replayed). */
        {static const uint8_t c[4]={1,2,3,4},o[4]={1,1,1,1};
         xeen_platform_set_cursor(p,c,o,2,2);CHECK(xeen_platform_mouse_event(p,40,40,0,0));
         CHECK(!xeen_platform_cursor_stale(p)&&xeen_platform_cursor_repaints(p)==0);}
    }
    CHECK(xeen_platform_queue_key(p,XKEY_ESCAPE)&&xeen_platform_should_close(p));
    CHECK(input.keyboard(input.user,&key)&&key==0);
    xeen_platform_close(p);free(pixels);
    CHECK(!xeen_platform_open("invalid",0,1,error,sizeof error)&&error[0]);
    /* Phase 335: the window scale shrinks to the largest that fits (the configured one is the most) */
    CHECK(xeen_platform_fit_scale(6,0,1920,1000)==5&&xeen_platform_fit_scale(6,1,1920,1000)==4);
    CHECK(xeen_platform_fit_scale(6,0,1920,1200)==6&&xeen_platform_fit_scale(6,0,1919,1200)==5); /* 320*6 = 1920 */
    CHECK(xeen_platform_fit_scale(10,0,3840,2160)==10&&xeen_platform_fit_scale(3,0,3840,2160)==3);
    CHECK(xeen_platform_fit_scale(6,0,0,1000)==6&&xeen_platform_fit_scale(6,0,1920,0)==6);        /* unknown */
    CHECK(xeen_platform_fit_scale(4,0,100,100)==1);                                               /* never 0 */
    p=xeen_platform_open("headless fit",6,1,error,sizeof error);CHECK(p&&xeen_platform_scale(p)==6); /* no screen */
    xeen_platform_set_aspect(p,1);CHECK(xeen_platform_scale(p)==6);xeen_platform_close(p);
    puts("native platform input/timer/shutdown and frontend key contracts passed");return 0;
}
