/* Phase166, corrected in Phase168 (owner correction: Ctrl+Q at every menu
 * screen, any other modifiers allowed; the query is now the menu-context
 * query, DS 8C77 in the host; native evidence only): the platform's
 * contract, headless. A fresh Ctrl+Q press is queued marked; the game poll
 * that reads it asks the host's query: true -> the close request and no
 * key; otherwise the plain 11h (Ctrl+Q's code).
 *  1. the physical maps: X11 'q'/'Q' and Win32 'Q' are XKEY_Q;
 *  2. the menu context: Ctrl+Q closes (both entry points: the window events
 *     and the headless queue), the game reads no key, counted once;
 *  3. not the menu context: the same press is the plain 11h, nothing closes;
 *  4. false positives at the menu: plain Q, Ctrl+W, an auto-repeat, a
 *     release (Phase168: Ctrl+Shift/Alt+Q now quit: test_phase167_menu_quit.c) (Ctrl alone has no key in
 *     either map: no event reaches the platform): never a close, the keys
 *     as before;
 *  5. the decision is the reader's: a key queued before Ctrl+Q is read
 *     first in the title state, and a Ctrl+Q read after the state left the
 *     menu context is the plain key;
 *  6. the F10 boss screen takes Ctrl+Q: outside the menu it hides the
 *     screen (no close); Phase168: in the menu it closes (after a pump). */
#include "xeen/platform.h"
#include <stdio.h>
#include <string.h>
static int failures;
#define CHECK(c) do{if(!(c)){fprintf(stderr,"%s:%d: CHECK failed: %s\n",__FILE__,__LINE__,#c);++failures;}}while(0)
static int title;static unsigned queries;
static int query(void *u){(void)u;++queries;return title;}
static int read_key(xeen_input_provider in,uint16_t *k){*k=0;return in.keyboard(in.user,k) && *k;}   /* 1 and key 0: none */
static xeen_platform *open_one(void)
{
    char error[160];xeen_platform *p=xeen_platform_open("phase166",1,1,error,sizeof error);
    CHECK(p!=NULL);
    if(p)xeen_platform_set_menu_quit_query(p,query,NULL);
    return p;
}
int main(void)
{
    const xeen_native_key ctrl_q=(xeen_native_key)(XKEY_Q|XKEY_CTRL);
    xeen_platform *p;xeen_input_provider in;uint16_t k;xeen_native_key key;
    /* 1 */
    CHECK(xeen_platform_x11_key('q',&key) && key==XKEY_Q);
    CHECK(xeen_platform_x11_key('Q',&key) && key==XKEY_Q);
    CHECK(xeen_platform_win32_key('Q',&key) && key==XKEY_Q);
    CHECK(xeen_input_translate_key(ctrl_q,0)==0x11u);
    /* 2: window events */
    p=open_one();in=xeen_platform_input(p);title=1;queries=0;
    CHECK(xeen_platform_key_event(p,ctrl_q,1,0));
    CHECK(!xeen_platform_should_close(p));                 /* nothing until a poll reads it */
    CHECK(!read_key(in,&k) && k==0 && queries==1u);
    CHECK(xeen_platform_should_close(p) && xeen_platform_menu_quits(p)==1u);
    CHECK(xeen_platform_key_event(p,ctrl_q,0,0));           /* its release: no key */
    CHECK(!read_key(in,&k));
    xeen_platform_close(p);
    /* 2: the headless queue */
    p=open_one();in=xeen_platform_input(p);title=1;
    CHECK(xeen_platform_queue_key(p,ctrl_q));
    CHECK(!read_key(in,&k) && xeen_platform_should_close(p) && xeen_platform_menu_quits(p)==1u);
    xeen_platform_close(p);
    /* 3 */
    p=open_one();in=xeen_platform_input(p);title=0;queries=0;
    CHECK(xeen_platform_key_event(p,ctrl_q,1,0));
    CHECK(read_key(in,&k) && k==0x11u && queries==1u);
    CHECK(xeen_platform_queue_key(p,ctrl_q));
    CHECK(read_key(in,&k) && k==0x11u);
    CHECK(!xeen_platform_should_close(p) && xeen_platform_menu_quits(p)==0u);
    xeen_platform_close(p);
    /* 4 */
    p=open_one();in=xeen_platform_input(p);title=1;queries=0;
    CHECK(xeen_platform_key_event(p,XKEY_Q,1,0));
    CHECK(read_key(in,&k) && k=='Q');
    CHECK(xeen_platform_key_event(p,(xeen_native_key)(XKEY_W|XKEY_CTRL),1,0));
    CHECK(read_key(in,&k) && k==0x17u);
    CHECK(xeen_platform_key_event(p,ctrl_q,1,1));             /* an auto-repeat (the queue is empty: queued) */
    CHECK(read_key(in,&k) && k==0x11u);
    CHECK(!read_key(in,&k));
    CHECK(queries==0u && !xeen_platform_should_close(p) && xeen_platform_menu_quits(p)==0u);
    xeen_platform_close(p);
    /* 5 */
    p=open_one();in=xeen_platform_input(p);title=1;
    CHECK(xeen_platform_key_event(p,XKEY_L,1,0) && xeen_platform_key_event(p,ctrl_q,1,0));
    CHECK(read_key(in,&k) && k=='L' && !xeen_platform_should_close(p));
    title=0;                                                  /* the key started a game (17024 cleared 8C77) */
    CHECK(read_key(in,&k) && k==0x11u && !xeen_platform_should_close(p));
    xeen_platform_close(p);
    /* 6 */
    p=open_one();in=xeen_platform_input(p);title=0;
    CHECK(xeen_platform_key_event(p,XKEY_F10,1,0));           /* the boss screen shows (outside the menu) */
    CHECK(xeen_platform_key_event(p,ctrl_q,1,0));             /* consumed; the pump hides it */
    xeen_platform_pump(p);
    CHECK(xeen_platform_key_event(p,ctrl_q,0,0));
    CHECK(!read_key(in,&k) && !xeen_platform_should_close(p) && xeen_platform_menu_quits(p)==0u);
    title=1;
    CHECK(xeen_platform_key_event(p,ctrl_q,1,0));             /* the next press in the menu: the quit */
    CHECK(!read_key(in,&k) && xeen_platform_should_close(p));
    xeen_platform_close(p);
    p=open_one();in=xeen_platform_input(p);title=1;
    CHECK(xeen_platform_key_event(p,XKEY_F10,1,0));           /* Phase168: the calculator from the menu */
    CHECK(xeen_platform_key_event(p,ctrl_q,1,0));
    xeen_platform_pump(p);
    CHECK(xeen_platform_should_close(p) && xeen_platform_menu_quits(p)==1u);
    xeen_platform_close(p);
    if(failures){fprintf(stderr,"%d failure(s)\n",failures);return 1;}
    puts("phase166 title quit: PASS");
    return 0;
}
