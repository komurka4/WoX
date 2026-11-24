/* character_details (Phase 208 add-on, NOT original behavior): the windows
 * in the character view's stat details popup, drawn on a copy of the
 * displayed page (include/xeen/addon/character_details.h). DS is read,
 * never written. Self-contained: no other add-on, no bug fix. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xeen/addon/character_details.h"
#include "xeen/byte_order.h"
#include "xeen/character_record.h"
#include "xeen/combat.h"
#include "xeen/ds_globals.h"
#include "xeen/party.h"
#include "xeen/game.h"
#include "xeen/resources.h"
#include "xeen/sprites.h"
#include "xeen/text_window.h"
#include "texts.h"

int xeen_character_details_on;

/* The character view is window 18h (0,0,320,146): a grown stat popup stays
 * between its name line and its frame's inner bottom (owner: inside the
 * sheet). */
#define SHEET_TOP 20
#define SHEET_BOTTOM 144
/* The detail windows (owner choices): skills over the stat grid (left of the
 * Skills popup, x 177), conditions in the right column (right of the
 * Conditions popup, x 34..176). */
static const uint16_t skill_window[4]={8,20,169,124},condition_window[4]={177,20,143,124};
/* 428F1's skill lines in the popup's (alphabetical) order (DS 7918). */
static const uint8_t skill_order[18]={0,1,2,3,4,5,17,6,7,8,9,10,11,12,13,16,14,15};
/* The character view's two list rows (xeen_game_details_view.stat, the view's cursor 0..19): Skills and Conditions. */
enum { CD_STAT_SKILLS=13, CD_STAT_CONDITIONS=19 };

static struct {
    int started;
    const xeen_game *game;
    xeen_text_window font;
    uint8_t page[64000];
    int sel;                  /* the chosen line's skill (0..17) or condition (0..15, 20..23); -1 none */
    unsigned sel_stat;uint16_t sel_record;
} cd;

int xeen_character_details_start(const void *sprite_archives)
{
    const xeen_sprite_archives *a=sprite_archives;unsigned i;
    xeen_character_details_stop();
    if(!a)return 0;
    for(i=0;i<2u && !cd.font.loaded;++i)if(a->cc[i])(void)xeen_text_window_load(&cd.font,a->cc[i]);
    if(!cd.font.loaded)return 0;
    cd.started=1;cd.sel=-1;xeen_character_details_on=1;
    return 1;
}
void xeen_character_details_stop(void)
{
    xeen_character_details_on=0;cd.started=0;cd.game=NULL;cd.sel=-1;
    memset(&cd.font,0,sizeof cd.font);
}
void xeen_character_details_bind(const xeen_game *g){cd.game=g;}
void xeen_character_details_reset(void){cd.sel=-1;}


/* The popup's lines after its title, as 428F1 writes them: per line the
 * skill or condition a click chooses, -1 for a line that is not clickable
 * ("None", "Good": owner). The count. */
static unsigned entries(const uint8_t *ds,uint16_t record,unsigned stat,int out[20])
{
    const uint8_t *ch=ds+record;unsigned n=0,i;
    if(stat==CD_STAT_SKILLS){
        for(i=0;i<sizeof skill_order;++i)if(ch[XEEN_CHARACTER_SKILLS+skill_order[i]])out[n++]=skill_order[i];
        if(!n)out[n++]=-1;                                               /* "None" */
    }else if(stat==CD_STAT_CONDITIONS){
        for(i=0;i<16u;++i)if(ch[XEEN_CHARACTER_CONDITIONS+i])out[n++]=(int)i;
        if(!n)out[n++]=-1;                                               /* "Good" (2AB96 = 16) */
        for(i=0;i<4u;++i)if(ds[(uint16_t)(XEEN_DS_EFFECT_BLESS+i)])out[n++]=20+(int)i;   /* Bless .. Heroism */
    }
    return n;
}

/* 4E38C's layout of TEXT in the window R (border 8) from the carried colour
 * and background the game drew the popup with; PAGE NULL measures. The
 * number of lines it takes (the reduced font's 9 rows each). */
static unsigned layout(uint8_t *page,const uint16_t r[4],const char *text,const xeen_game_details_view *v)
{
    xeen_text_layout s;
    xeen_text_layout_init(&s,r,8);s.colour=v->colour;s.background=v->background;
    (void)xeen_text_layout_run(&s,&cd.font,page,(const uint8_t *)text,strlen(text)+1u,0);
    return (unsigned)((s.y-s.top)/9+1);
}
/**
 * Returns the height the layout of TEXT needs in a window as wide as R, measured without drawing.
 */
static unsigned measure(const char *text,const uint16_t r[4],const xeen_game_details_view *v)
{
    uint16_t tall[4]={r[0],0,r[2],600};
    return layout(NULL,tall,text,v);
}
/** Draws a text window frame at R on PAGE and lays TEXT out inside it. */
static void window(uint8_t *page,const uint16_t r[4],const char *text,const xeen_game_details_view *v)
{
    (void)xeen_text_frame(page,64000,r);
    (void)layout(page,r,text,v);
}

/* A stat popup (0..9) with the explanation under its title: the popup's
 * text (AC0F) with the lines inserted after its first line break, the
 * window grown by their lines and moved up when it would pass the sheet.
 * The original popup's rectangle gets the pixels saved under it first. */
static int stat_page(const xeen_game_details_view *v,const xeen_movement *m)
{
    char explain[600],text[1400];const char *br=strchr(v->text,'\n');uint16_t r[4];unsigned extra;int y,h;size_t head;
    if(!br || !cd_stat_text(explain,sizeof explain,m,v->record,v->stat))return 0;
    head=(size_t)(br-v->text)+1u;
    if(head+strlen(br+1)+strlen(explain)+16u>=sizeof text)return 0;
    memcpy(text,v->text,head);
    snprintf(text+head,sizeof text-head,"\014%02u%s\014%02u\n%s",CD_RULE_BLUE,explain,v->colour<40u?v->colour:0u,br+1);
    extra=measure(text,v->rect,v)-measure(v->text,v->rect,v);
    h=(int)v->rect[3]+9*(int)extra;y=(int)v->rect[1];
    if(y+h>SHEET_BOTTOM)y=SHEET_BOTTOM-h;
    if(y<SHEET_TOP)y=SHEET_TOP;
    if(y+h>200)return 0;
    if(v->under){
        unsigned row;
        for(row=v->rect[1];row<(unsigned)v->rect[1]+v->rect[3] && row<200u;++row)
            memcpy(cd.page+row*320u+v->rect[0],v->under+row*320u+v->rect[0],v->rect[2]);
    }
    r[0]=v->rect[0];r[1]=(uint16_t)y;r[2]=v->rect[2];r[3]=(uint16_t)h;
    window(cd.page,r,text,v);
    return 1;
}

/* The Skills or Conditions popup again with the chosen line in the
 * highlight colour, and the chosen entry's window beside it. */
static int list_page(const xeen_game_details_view *v,const xeen_movement *m)
{
    int list[20];unsigned n=entries(m->ds,v->record,v->stat,list),k,line=0;
    char text[1400],detail[1400];const char *p=v->text,*start,*end;size_t a,b;
    for(k=0;k<n;++k)if(list[k]==cd.sel)break;
    if(k>=n)return 0;
    line=k+1u;                                                           /* the title is line 0 */
    for(start=p,k=0;k<line;++k){start=strchr(start,'\n');if(!start)return 0;++start;}
    for(end=start;*end && *end!='\n' && *end!='\001';++end){}
    a=(size_t)(start-p);b=(size_t)(end-p);
    if(strlen(p)+16u>=sizeof text)return 0;
    snprintf(text,sizeof text,"%.*s\014%02u%.*s\014%02u%s",(int)a,p,CD_HIGHLIGHT,(int)(b-a),start,v->colour<40u?v->colour:0u,end);
    window(cd.page,v->rect,text,v);
    if(v->stat==CD_STAT_SKILLS){
        if(!cd_skill_text(detail,sizeof detail,m,v->record,(unsigned)cd.sel,v->colour))return 0;
        window(cd.page,skill_window,detail,v);
    }else{
        if(!cd_condition_text(detail,sizeof detail,m,v->record,(unsigned)cd.sel,v->colour))return 0;
        window(cd.page,condition_window,detail,v);
    }
    return 1;
}

/**
 * Returns the page to display: PAGE itself, or a copy with the add-on's explanation drawn over the
 * character view's open stat detail (stats 0..9), or over its skill or condition list when a line
 * of it was selected. The original page is never modified.
 */
static const uint8_t *compose(const uint8_t *page)
{
    xeen_game_details_view v;const xeen_movement *m;int shown=0;
    if(!cd.started || !page || !cd.game || !(m=xeen_game_movement(cd.game)))return page;
    if(!xeen_game_details(cd.game,&v))return page;
    if((uint32_t)v.record+XEEN_CHARACTER_BYTES>0x10000u)return page;
    if(v.stat!=cd.sel_stat || v.record!=cd.sel_record)cd.sel=-1;
    memcpy(cd.page,page,sizeof cd.page);
    if(v.stat<=9u)shown=stat_page(&v,m);
    else if((v.stat==CD_STAT_SKILLS || v.stat==CD_STAT_CONDITIONS) && cd.sel>=0)shown=list_page(&v,m);
    if(!shown)return page;
    return cd.page;
}
const uint8_t *xeen_character_details_page(const uint8_t *page){return compose(page);}

int xeen_character_details_click(const xeen_game *g)
{
    xeen_game_details_view v;const uint8_t *ds;int list[20];unsigned n,mx,my,line;
    if(!cd.started || !g || !xeen_game_details(g,&v) || (v.stat!=CD_STAT_SKILLS && v.stat!=CD_STAT_CONDITIONS))return 0;
    if(!(ds=xeen_game_ds(g)) || (uint32_t)v.record+XEEN_CHARACTER_BYTES>0x10000u)return 0;
    mx=xeen_get_le16(ds+XEEN_DS_MOUSE_X);my=xeen_get_le16(ds+XEEN_DS_MOUSE_Y);   /* 18C0C's click point */
    if(mx<v.rect[0] || mx>=(unsigned)v.rect[0]+v.rect[2] || my<(unsigned)v.rect[1]+8u+9u)return 0;
    line=(my-((unsigned)v.rect[1]+8u))/9u;                               /* 1.. the lines under the title */
    n=entries(ds,v.record,v.stat,list);
    if(line<1u || line>n || list[line-1u]<0)return 0;
    cd.sel=list[line-1u];cd.sel_stat=v.stat;cd.sel_record=v.record;
    return 1;
}
