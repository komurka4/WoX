/* mouse_scroll (Phase 204 add-on, NOT original behavior): the mouse wheel
 * as the scrolling lists' own Up and Down keys
 * (include/xeen/addon/mouse_scroll.h). The game is only read (its list
 * view); the key it receives is the original Up or Down command.
 * Self-contained: no other add-on, no bug fix. */
#include <stdio.h>
#include <stdlib.h>
#include "xeen/addon/mouse_scroll.h"
#include "xeen/game.h"

int xeen_mouse_scroll_on;

static struct {
    xeen_input_provider inner;
    xeen_mouse_scroll_take take; void *take_user;
    struct xeen_game *const *game;
} ms;

/* The game's key request (the provider contract: 1 with key 0 is "no key",
 * 0 no keyboard): a real key first; else one wheel notch becomes the list's
 * Up or Down while the game waits at that list's poll and the notch was over
 * its window; any other notch is dropped. */
static int keyboard(void *user,uint16_t *key)
{
    uint16_t x=0,y=0,rect[4];int d,kind;
    (void)user;
    if(!ms.inner.keyboard(ms.inner.user,key))return 0;
    if(*key)return 1;
    while((d=ms.take(ms.take_user,&x,&y))!=0){
        kind=ms.game && *ms.game?xeen_game_scroll_list(*ms.game,rect):0;
        if(kind && x>=rect[0] && x<rect[0]+rect[2] && y>=rect[1] && y<rect[1]+rect[3]){
            *key=xeen_input_translate_key(d>0?XKEY_UP:XKEY_DOWN,0);
            return 1;
        }
    }
    return 1;                                                          /* no key (*key 0) */
}
/** Passes a mouse sample request through to the wrapped input provider. */
static int mouse(void *user,xeen_mouse_sample *sample){(void)user;return ms.inner.mouse(ms.inner.user,sample);}

int xeen_mouse_scroll_start(xeen_input_provider *provider,xeen_mouse_scroll_take take,void *take_user,
                            struct xeen_game *const *game)
{
    xeen_mouse_scroll_stop();
    if(!provider || !provider->keyboard || !provider->mouse || !take || !game)return 0;
    ms.inner=*provider;ms.take=take;ms.take_user=take_user;ms.game=game;
    provider->keyboard=keyboard;provider->mouse=mouse;provider->user=NULL;
    xeen_mouse_scroll_on=1;
    return 1;
}
void xeen_mouse_scroll_stop(void)
{
    xeen_mouse_scroll_on=0;ms.take=NULL;ms.take_user=NULL;ms.game=NULL;
    ms.inner.keyboard=NULL;ms.inner.mouse=NULL;ms.inner.user=NULL;
}
