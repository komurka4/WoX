/* Phase167, corrected in Phase168 (owner correction: Ctrl+Q, not Ctrl+C,
 * quits at every menu screen; native evidence only): the platform's
 * contract, headless. A fresh Ctrl+Q press, with any other modifiers held,
 * is queued marked; the poll that reads it asks the host's menu-context
 * query (DS 8C77 in the host): true -> the close request and no key;
 * false -> the plain 11h (4FE85: 'Q' - 40h). While the F10 calculator
 * shows, a fresh Ctrl+Q waits for the game thread's next pump: the menu's
 * context -> the close (not a dismissal), else it hides the calculator as
 * any key does and reaches no poll. Ctrl+C is no shortcut anywhere.
 *  1. the maps: X11 'q'/'Q', Win32 'Q' are XKEY_Q; Ctrl+Q is 11h, Ctrl+C 03h;
 *  2. the menu context: Ctrl+Q closes (window events and headless queue),
 *     no key, counted once;
 *  3. outside it (the first title animation, a game): the plain 11h
 *     (rejects a global quit);
 *  4. other modifiers held (Shift, Alt, Super, Space+Tab): still the quit
 *     (rejects Phase166's exact-modifier rule);
 *  5. false positives in the menu context: plain Q, Ctrl+W, Ctrl+C (with
 *     and without modifiers, headless and window), an auto-repeat, a
 *     release: never a close, never a query; Ctrl+C is the plain 03h;
 *  6. the reader decides: a key queued before is read first; a Ctrl+Q read
 *     after the state left the menu context is the plain 11h, and one read
 *     after the buttons opened quits (rejects a narrow title-only scope:
 *     the query, not a stage, decides);
 *  7. the calculator from the menu: Ctrl+Q (also with modifiers) closes;
 *     a repeat decides nothing; plain Q and Ctrl+C there dismiss it;
 *  8. the calculator from a game: Ctrl+Q hides it, no close, no key; its
 *     repeat and release reach nothing; the next key is the game's; Ctrl+C
 *     there dismisses it as any key (no Phase167 special path). */
#include "xeen/platform.h"
#include <stdio.h>
#include <string.h>
static int failures;
#define CHECK(c) do{if(!(c)){fprintf(stderr,"%s:%d: CHECK failed: %s\n",__FILE__,__LINE__,#c);++failures;}}while(0)
static int menu;static unsigned menu_queries;
static int menu_query(void *u){(void)u;++menu_queries;return menu;}
static int read_key(xeen_input_provider in,uint16_t *k){*k=0;return in.keyboard(in.user,k) && *k;}   /* 1 and key 0: none */
static xeen_platform *open_one(void)
{
    char error[160];xeen_platform *p=xeen_platform_open("phase168",1,1,error,sizeof error);
    CHECK(p!=NULL);
    if(p)xeen_platform_set_menu_quit_query(p,menu_query,NULL);
    menu_queries=0;
    return p;
}
static int boss_active(xeen_platform *p){xeen_platform_boss_info i;xeen_platform_boss_info_get(p,&i);return i.active;}
int main(void)
{
    const xeen_native_key ctrl_c=(xeen_native_key)(XKEY_C|XKEY_CTRL),ctrl_q=(xeen_native_key)(XKEY_Q|XKEY_CTRL);
    xeen_platform *p;xeen_input_provider in;uint16_t k;xeen_native_key key;
    /* 1 */
    CHECK(xeen_platform_x11_key('q',&key) && key==XKEY_Q);
    CHECK(xeen_platform_x11_key('Q',&key) && key==XKEY_Q);
    CHECK(xeen_platform_win32_key('Q',&key) && key==XKEY_Q);
    CHECK(xeen_input_translate_key(ctrl_q,0)==0x11u && xeen_input_translate_key(ctrl_c,0)==0x03u);
    /* 2: window events */
    p=open_one();in=xeen_platform_input(p);menu=1;
    CHECK(xeen_platform_key_event(p,ctrl_q,1,0));
    CHECK(!xeen_platform_should_close(p));                  /* nothing until a poll reads it */
    CHECK(!read_key(in,&k) && k==0 && menu_queries==1u);
    CHECK(xeen_platform_should_close(p) && xeen_platform_menu_quits(p)==1u);
    CHECK(xeen_platform_key_event(p,ctrl_q,0,0));            /* its release: no key */
    CHECK(!read_key(in,&k) && xeen_platform_menu_quits(p)==1u);
    xeen_platform_close(p);
    /* 2: the headless queue */
    p=open_one();in=xeen_platform_input(p);menu=1;
    CHECK(xeen_platform_queue_key(p,ctrl_q));
    CHECK(!read_key(in,&k) && xeen_platform_should_close(p) && xeen_platform_menu_quits(p)==1u);
    xeen_platform_close(p);
    /* 3: the first title animation, a game: the plain 11h */
    p=open_one();in=xeen_platform_input(p);menu=0;
    CHECK(xeen_platform_key_event(p,ctrl_q,1,0));
    CHECK(read_key(in,&k) && k==0x11u && menu_queries==1u);
    CHECK(xeen_platform_queue_key(p,ctrl_q));
    CHECK(read_key(in,&k) && k==0x11u);
    CHECK(xeen_platform_key_event_modified(p,ctrl_q,1,0,1));
    CHECK(read_key(in,&k) && k==0x11u);
    CHECK(!xeen_platform_should_close(p) && xeen_platform_menu_quits(p)==0u);
    xeen_platform_close(p);
    /* 4: other modifiers held (Ctrl+Shift+Q, Ctrl+Alt+Q, Ctrl+Super+Q, Ctrl+Space+Tab+Q) */
    p=open_one();in=xeen_platform_input(p);menu=1;
    CHECK(xeen_platform_key_event_modified(p,ctrl_q,1,0,1));
    CHECK(!read_key(in,&k) && xeen_platform_should_close(p) && xeen_platform_menu_quits(p)==1u);
    xeen_platform_close(p);
    /* 5 */
    p=open_one();in=xeen_platform_input(p);menu=1;
    CHECK(xeen_platform_key_event(p,XKEY_Q,1,0));
    CHECK(read_key(in,&k) && k=='Q');
    CHECK(xeen_platform_key_event_modified(p,XKEY_Q,1,0,1));  /* Shift+Q (no Ctrl) */
    CHECK(read_key(in,&k) && k=='Q');
    CHECK(xeen_platform_key_event(p,(xeen_native_key)(XKEY_W|XKEY_CTRL),1,0));
    CHECK(read_key(in,&k) && k==0x17u);
    CHECK(xeen_platform_key_event(p,ctrl_c,1,0));             /* Ctrl+C: the plain 03h, no query */
    CHECK(read_key(in,&k) && k==0x03u);
    CHECK(xeen_platform_key_event_modified(p,ctrl_c,1,0,1));  /* Ctrl+Shift/Alt+C */
    CHECK(read_key(in,&k) && k==0x03u);
    CHECK(xeen_platform_queue_key(p,ctrl_c));                 /* the headless ^C */
    CHECK(read_key(in,&k) && k==0x03u);
    CHECK(xeen_platform_key_event(p,ctrl_q,1,1));             /* an auto-repeat (the queue is empty: queued) */
    CHECK(read_key(in,&k) && k==0x11u);
    CHECK(xeen_platform_key_event(p,ctrl_q,0,0));             /* a release */
    CHECK(!read_key(in,&k));
    CHECK(menu_queries==0u && !xeen_platform_should_close(p) && xeen_platform_menu_quits(p)==0u);
    xeen_platform_close(p);
    /* 6 */
    p=open_one();in=xeen_platform_input(p);menu=1;
    CHECK(xeen_platform_key_event(p,XKEY_S,1,0) && xeen_platform_key_event(p,ctrl_q,1,0));
    CHECK(read_key(in,&k) && k=='S' && !xeen_platform_should_close(p));
    menu=0;                                                   /* the key started a game: 17024 cleared 8C77 */
    CHECK(read_key(in,&k) && k==0x11u && !xeen_platform_should_close(p));
    xeen_platform_close(p);
    p=open_one();in=xeen_platform_input(p);menu=0;
    CHECK(xeen_platform_key_event(p,XKEY_A,1,0) && xeen_platform_key_event(p,ctrl_q,1,0));
    CHECK(read_key(in,&k) && k==xeen_input_translate_key(XKEY_A,0) && !xeen_platform_should_close(p));
    menu=1;                                                   /* the key opened the menu's buttons: 16A28 */
    CHECK(!read_key(in,&k) && xeen_platform_should_close(p) && xeen_platform_menu_quits(p)==1u);
    xeen_platform_close(p);
    /* 7: the calculator shown from the menu */
    p=open_one();in=xeen_platform_input(p);menu=1;
    CHECK(xeen_platform_key_event(p,XKEY_F10,1,0) && xeen_platform_key_event(p,XKEY_F10,0,0) && boss_active(p));
    CHECK(xeen_platform_key_event(p,ctrl_q,1,0));
    CHECK(boss_active(p) && !xeen_platform_should_close(p) && menu_queries==0u);   /* the game thread decides */
    xeen_platform_pump(p);
    CHECK(xeen_platform_should_close(p) && xeen_platform_menu_quits(p)==1u && menu_queries==1u);
    CHECK(!read_key(in,&k));                                  /* no key reached a poll */
    xeen_platform_close(p);
    p=open_one();in=xeen_platform_input(p);menu=1;
    CHECK(xeen_platform_key_event(p,XKEY_F10,1,0) && boss_active(p));
    CHECK(xeen_platform_key_event_modified(p,ctrl_q,1,0,1));  /* Ctrl+Alt+Q at the calculator */
    xeen_platform_pump(p);
    CHECK(xeen_platform_should_close(p) && xeen_platform_menu_quits(p)==1u);
    xeen_platform_close(p);
    p=open_one();in=xeen_platform_input(p);menu=1;
    CHECK(xeen_platform_key_event(p,XKEY_F10,1,0) && boss_active(p));
    CHECK(xeen_platform_key_event(p,ctrl_q,1,1));             /* a repeat at the calculator: consumed, no decision */
    xeen_platform_pump(p);
    CHECK(boss_active(p) && !xeen_platform_should_close(p) && menu_queries==0u);
    CHECK(xeen_platform_key_event(p,XKEY_Q,1,0));             /* plain Q: dismisses (Phase161) */
    CHECK(!boss_active(p) && !xeen_platform_should_close(p));
    CHECK(xeen_platform_key_event(p,XKEY_Q,0,0) && !read_key(in,&k));
    CHECK(xeen_platform_key_event(p,XKEY_F10,1,0) && boss_active(p));
    CHECK(xeen_platform_key_event(p,ctrl_c,1,0));             /* Ctrl+C: dismisses as any key */
    xeen_platform_pump(p);
    CHECK(!boss_active(p) && !xeen_platform_should_close(p) && menu_queries==0u && xeen_platform_menu_quits(p)==0u);
    CHECK(xeen_platform_key_event(p,ctrl_c,0,0) && !read_key(in,&k));
    xeen_platform_close(p);
    /* 8: the calculator shown from a game */
    p=open_one();in=xeen_platform_input(p);menu=0;
    CHECK(xeen_platform_key_event(p,XKEY_F10,1,0) && xeen_platform_key_event(p,XKEY_F10,0,0) && boss_active(p));
    CHECK(xeen_platform_key_event(p,ctrl_q,1,0));
    CHECK(boss_active(p));
    xeen_platform_pump(p);
    CHECK(!boss_active(p) && !xeen_platform_should_close(p) && menu_queries==1u && xeen_platform_menu_quits(p)==0u);
    CHECK(!xeen_platform_key_event(p,ctrl_q,1,1));            /* its repeat: swallowed */
    CHECK(xeen_platform_key_event(p,ctrl_q,0,0));             /* its release */
    CHECK(!read_key(in,&k));                                  /* no 11h reached the game */
    CHECK(xeen_platform_key_event(p,XKEY_UP,1,0) && read_key(in,&k) && k==0xf2u);   /* the next key is the game's */
    xeen_platform_pump(p);
    CHECK(!xeen_platform_should_close(p));
    CHECK(xeen_platform_key_event(p,XKEY_F10,1,0) && boss_active(p));
    CHECK(xeen_platform_key_event(p,ctrl_c,1,0));             /* Ctrl+C from a game's calculator: dismissed */
    CHECK(!boss_active(p) && menu_queries==1u);
    CHECK(xeen_platform_key_event(p,ctrl_c,0,0) && !read_key(in,&k));
    CHECK(!xeen_platform_should_close(p));
    xeen_platform_close(p);
    if(failures){fprintf(stderr,"%d failure(s)\n",failures);return 1;}
    puts("phase167/168 menu quit (Ctrl+Q): PASS");
    return 0;
}
