/* damage_numbers (Phase 199 add-on, NOT original behavior): layout,
 * animation and drawing of the noted values on a copy of the displayed page
 * (include/xeen/addon/damage_numbers.h). Engine only: the game's FNT glyphs,
 * the game's text colours, 320x200 pixels; DS and the sprite cache are read,
 * never written. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "state.h"
#include "xeen/byte_order.h"
#include "xeen/ds_globals.h"
#include "xeen/party.h"
#include "xeen/game.h"
#include "xeen/raster.h"
#include "xeen/sprites.h"
#include "xeen/text_window.h"
/* The driver's window rectangles (generated from XEEN.DAT, as presentation.c
 * uses them): an open window (DS 986Ch + n) hides the numbers under it. */
#include "../../graphics/window_table.inc"

#define MS UINT64_C(1000000)
/* Default layout (P1/P4, M1): the pop, the life, the fade, the rise. */
#define RISE_LIFE (1250u*MS)
#define POP (100u*MS)
#define FADE (350u*MS)
#define KICK_T (100u*MS)
/* Stack layout (P2/P5, M2): held after the last hit, then faded. */
#define HOLD (1000u*MS)
#define HOLD_FADE (350u*MS)
#define GAP_MAX (HOLD+HOLD_FADE)
/* The view (window 3) for the monsters' numbers. */
#define VIEW_X0 8
#define VIEW_X1 224
#define VIEW_Y0 8
#define VIEW_Y1 140
/* A monster's number is kept at least this far inside the view's top (the stack moves it down, the rise skips it);
 * the stack's kinds of one target are STACK_ROW pixels apart. */
#define TEXT_TOP (VIEW_Y0+5)
#define STACK_ROW 10

static struct {
    int stack, started;
    uint8_t font[4352];
    const xeen_sprite_archives *archives;
    const xeen_game *game;
    uint8_t page[64000];
    uint64_t fake_ns;
    /* monster outlines: draw record -> anchor (round robin) */
    struct { uint8_t record[12];int x,y,ok; } anchors[8];unsigned next_anchor;
    /* the game's text colours: palette index of code 0..39 */
    uint8_t codes[40];
    /* the last composition: its source page and time (a window's host loop
     * asks every pass; an unchanged page is recomposed every 15 ms) */
    uint8_t source[64000];uint64_t composed_ns;int composed;
    /* one cached sprite resource */
    char sprite_name[16];uint8_t sprite_archive;uint8_t *sprite;size_t sprite_size;
    uint8_t scratch[2][64000];
} dn;

/* The game's text colours (the \fNN codes, the driver table read through
 * the engine's text cursor): the palette index of code N. */
static uint8_t code(unsigned n){return dn.codes[n<40u?n:0u];}
/** Fills the table of the text colour codes 0..39 with the colour each one selects. */
static void load_codes(void)
{
    unsigned n;
    for(n=0;n<40u;++n){xeen_text_cursor c;memset(&c,0,sizeof c);xeen_text_cursor_set_colour(&c,n);dn.codes[n]=c.colours[1];}
}
enum { K_DAMAGE, K_HEAL, K_MISS };
/* flash (the pop), normal, fade: damage 31/32/33, healing 14/15/16,
 * misses 4/1/2 (white, light grey, grey). */
static const uint8_t kind_codes[3][3]={{31,32,33},{14,15,16},{4,1,2}};
#define COUNT_CODE 1u
#define OUTLINE 0u

/**
 * Loads the game font ("fnt") from the first archive that holds one of the expected size. Returns 1
 * on success, else 0.
 */
static int load_font(const xeen_sprite_archives *a)
{
    unsigned i;
    for(i=0;i<2u;++i){
        xeen_resource_info info;size_t got=0;
        if(!a->cc[i] || xeen_archive_find_name(a->cc[i],"fnt",&info)!=XR_OK)continue;
        if(info.size!=sizeof dn.font)continue;
        if(xeen_archive_extract(a->cc[i],XR_BANK_ASSETS_XOR35,&info,dn.font,sizeof dn.font,&got)==XR_OK && got==sizeof dn.font)return 1;
    }
    return 0;
}
int xeen_damage_numbers_start(int stack,const void *sprite_archives)
{
    const xeen_sprite_archives *a=sprite_archives;
    if(!a || !load_font(a))return 0;
    dn.stack=stack;dn.archives=a;dn.started=1;load_codes();
    memset(dn.anchors,0,sizeof dn.anchors);dn.composed=0;
    memset(dn_entries,0,sizeof dn_entries);
    xeen_damage_numbers_on=1;xeen_damage_numbers_event_hp=dn_event_hp;
    return 1;
}
void xeen_damage_numbers_stop(void)
{
    xeen_damage_numbers_on=0;xeen_damage_numbers_event_hp=NULL;dn.started=0;dn.game=NULL;
    free(dn.sprite);dn.sprite=NULL;dn.sprite_size=0;dn.sprite_name[0]=0;
}
void xeen_damage_numbers_bind(const xeen_game *g){dn.game=g;}

/* ------------------------------------------------------------- text --- */
/* The game's .fnt layout: 256 glyphs of 8 rows, each row a 16-bit word of two bits per pixel (FONT_GLYPH_BYTES a
 * glyph), then one width byte per glyph from FONT_WIDTHS; the reduced font is glyphs 80h.. */
enum { FONT_GLYPH_BYTES=16, FONT_ROW_BYTES=2, FONT_ROWS=8, FONT_WIDTHS=0x1000 };
/* A note's text past these widths (normal font) is drawn in the reduced one: a party member's portrait is narrower
 * than a monster's place in the view. */
enum { PARTY_TEXT_MAX=34, MONSTER_TEXT_MAX=70 };
static unsigned glyph_index(char ch,int reduced){return (unsigned)(uint8_t)ch|(reduced?0x80u:0u);}
/** Returns the width in pixels of S in the normal or the reduced font. */
static int text_width(const char *s,int reduced)
{
    int w=0;
    for(;*s;++s)w+=dn.font[FONT_WIDTHS+glyph_index(*s,reduced)];
    return w;
}
/** Nonzero when S is too wide for its target in the normal font (PARTY_TEXT_MAX / MONSTER_TEXT_MAX). */
static int too_wide(const char *s,int party){return text_width(s,0)>(party?PARTY_TEXT_MAX:MONSTER_TEXT_MAX);}
typedef struct { int x0,y0,x1,y1; } clip;
/* The open windows' rectangles (42..47 are zero-size; 28 and 39 are defined
 * at run time and not in the table): nothing is drawn inside them. */
static struct { int n; uint16_t r[42][4]; } covered;
/**
 * Collects the rectangles of the open windows (DS:986C flags), so numbers are not drawn over them.
 */
static void cover(const uint8_t *ds)
{
    unsigned w;covered.n=0;
    for(w=0;w<42u;++w)if(ds[XEEN_DS_WINDOW_FLAG(w)] && window_rects[w][2] && window_rects[w][3])
        memcpy(covered.r[covered.n++],window_rects[w],sizeof covered.r[0]);
}
/** Sets one pixel of P to colour C if it lies inside the clip K and outside every open window. */
static void plot(uint8_t *p,int x,int y,uint8_t c,const clip *k)
{
    int i;
    if(x<k->x0 || x>=k->x1 || y<k->y0 || y>=k->y1)return;
    for(i=0;i<covered.n;++i){const uint16_t *r=covered.r[i];
        if(x>=r[0] && x<r[0]+r[2] && y>=r[1] && y<r[1]+r[3])return;}
    p[y*320+x]=c;
}
/* Glyph CH's set pixels at (X, Y) as 4E38C draws them (two bits per pixel, 8x8): in COLOUR, or with OUTLINE the
 * outline colour on the 3x3 around each set pixel. */
static void draw_glyph(uint8_t *p,unsigned ch,int x,int y,int outline,uint8_t colour,const clip *k)
{
    unsigned row,col;
    for(row=0;row<FONT_ROWS;++row){
        const uint8_t *r=dn.font+ch*FONT_GLYPH_BYTES+row*FONT_ROW_BYTES;
        unsigned bits=(unsigned)r[0]|((unsigned)r[1]<<8);
        for(col=0;col<8u;++col,bits>>=2)if(bits&3u){
            int px=x+(int)col,py=y+(int)row;
            if(!outline)plot(p,px,py,colour,k);
            else for(int oy=-1;oy<=1;++oy)for(int ox=-1;ox<=1;++ox)plot(p,px+ox,py+oy,code(OUTLINE),k);
        }
    }
}
/* S centred at (CX, CY) (g, p, q and y one row lower): with OUTLINE first the whole text's outline, then the glyphs
 * in COLOUR. */
static void draw_text(uint8_t *p,const char *s,int reduced,int cx,int cy,uint8_t colour,int outline,const clip *k)
{
    int pass,x0=cx-text_width(s,reduced)/2,y0=cy-4;
    for(pass=outline?0:1;pass<2;++pass){
        int x=x0;const char *q;
        for(q=s;*q;++q){
            unsigned ch=glyph_index(*q,reduced);
            int dy=(*q=='g'||*q=='p'||*q=='q'||*q=='y')?1:0;
            if(*q!=' ')draw_glyph(p,ch,x,y0+dy,!pass,colour,k);
            x+=dn.font[FONT_WIDTHS+ch];
        }
    }
}
/**
 * Formats a note for display: "Miss", or the value with its sign ("+12", "-7", "0"), limited to
 * -99999..99999.
 */
static void format_value(char *out,size_t n,const dn_entry *e,int32_t value)
{
    if(e->miss){snprintf(out,n,"Miss");return;}
    if(value>99999)value=99999;
    if(value<-99999)value=-99999;
    if(value>0)snprintf(out,n,"+%ld",(long)value);
    else if(value<0)snprintf(out,n,"-%ld",(long)-value);
    else snprintf(out,n,"0");
}
/** Returns the display kind of a note: a miss, healing (a positive value) or damage. */
static int kind_of(const dn_entry *e,int32_t value){return e->miss?K_MISS:value>0?K_HEAL:K_DAMAGE;}

/* --------------------------------------------------------- anchors --- */
static int load_sprite(const xeen_sprite *sp)
{
    const xeen_archive *a;xeen_resource_info info;size_t got=0;uint8_t *b;
    if(dn.sprite && dn.sprite_archive==sp->archive && !strncmp(dn.sprite_name,sp->name,sizeof dn.sprite_name))return 1;
    if(sp->archive>XSP_CUR)return 0;
    a=sp->archive==XSP_CUR?dn.archives->cur:dn.archives->cc[sp->archive];
    if(!a || xeen_archive_find_name(a,sp->name,&info)!=XR_OK)return 0;
    if(!(b=malloc(info.size?info.size:1u)))return 0;
    if(xeen_archive_extract(a,sp->archive==XSP_CUR?XR_BANK_STATE:XR_BANK_ASSETS_XOR35,&info,b,info.size,&got)!=XR_OK){free(b);return 0;}
    free(dn.sprite);dn.sprite=b;dn.sprite_size=got;dn.sprite_archive=sp->archive;
    memcpy(dn.sprite_name,sp->name,sizeof dn.sprite_name);
    return 1;
}
/* The monster's anchor on its body: the drawn frame's opaque pixels inside
 * the view (the engine's own renderer on two scratch pages): their mean x
 * (wide limbs do not pull it aside), halfway between the top and the mean
 * y (the upper body). */
static int monster_anchor(const uint8_t record[12],int *ax,int *ay)
{
    const xeen_sprite_cache *cache;const xeen_sprite *sp;
    uint16_t handle=xeen_get_le16(record),frame=xeen_get_le16(record+2),scale=xeen_get_le16(record+8),flags=xeen_get_le16(record+10);
    int16_t x=(int16_t)xeen_get_le16(record+4),y=(int16_t)xeen_get_le16(record+6);
    int miny=VIEW_Y1,px,py;long n=0,sx=0,sy=0;unsigned i;
    for(i=0;i<8u;++i)if(dn.anchors[i].ok && !memcmp(dn.anchors[i].record,record,12)){*ax=dn.anchors[i].x;*ay=dn.anchors[i].y;return 1;}
    if(!dn.game || !(cache=xeen_game_sprites(dn.game)) || handle==0 || handle==0xffffu || frame==0xffffu || scale==0xffffu)return 0;
    if(!(sp=xeen_sprite_get(cache,handle)) || !load_sprite(sp))return 0;
    memset(dn.scratch[0],0,64000);memset(dn.scratch[1],255,64000);
    flags&=0xe000u;                       /* the colour-mode bits are effects, not the outline */
    if(xeen_raster_frame(dn.sprite,dn.sprite_size,frame,dn.scratch[0],64000,x,y,scale,flags)!=XEEN_OK ||
       xeen_raster_frame(dn.sprite,dn.sprite_size,frame,dn.scratch[1],64000,x,y,scale,flags)!=XEEN_OK)return 0;
    for(py=VIEW_Y0;py<VIEW_Y1;++py)for(px=VIEW_X0;px<VIEW_X1;++px)
        if(dn.scratch[0][py*320+px]!=0 || dn.scratch[1][py*320+px]!=255){
            if(py<miny)miny=py;
            ++n;sx+=px;sy+=py;
        }
    if(!n)return 0;
    *ax=(int)(sx/n);*ay=(miny+(int)(sy/n))/2;
    i=dn.next_anchor++%8u;memcpy(dn.anchors[i].record,record,12);dn.anchors[i].x=*ax;dn.anchors[i].y=*ay;dn.anchors[i].ok=1;
    return 1;
}
/* A party member's portrait (17B2h[slot], 96h): the slot shows the member
 * (in combat, 9A1C = 2, the slot whose 9720h entry is the member). */
static int party_anchor(const uint8_t *ds,unsigned member,int *ax,int *ay)
{
    int combat=ds[XEEN_DS_UI_MODE]==XEEN_UI_MODE_COMBAT;                          /* combat: its party map */
    unsigned slot,count=combat?ds[XEEN_DS_COMBAT_PARTY_COUNT]:ds[XEEN_DS_PARTY_COUNT];
    if(count>XEEN_PARTY_SLOTS)return 0;
    for(slot=0;slot<count;++slot)if((combat?ds[XEEN_DS_COMBAT_PARTY+slot]:slot)==member){
        *ax=ds[XEEN_DS_PORTRAIT_X+slot]+16;*ay=163;return 1;
    }
    return 0;
}
/**
 * Finds the screen point a note is drawn at: the party member's portrait, or the monster's current
 * place in the view (its record is refreshed while it stays in a view slot). Returns 0 when there
 * is none.
 */
static int anchor(const uint8_t *ds,dn_entry *e,int *ax,int *ay)
{
    uint8_t now[12];
    if(e->party)return party_anchor(ds,e->index,ax,ay);
    /* follows the monster while it stays in a view slot */
    if(dn_monster_record(ds,e->index,now)){memcpy(e->record,now,12);e->have_record=1;}
    return e->have_record && monster_anchor(e->record,ax,ay);
}

/* ---------------------------------------------------------- layouts --- */
static uint64_t ease_px(uint64_t age,uint64_t span,unsigned px)
{
    /* ease-out cubic, whole pixels */
    double t=age>=span?1.0:(double)age/(double)span,u=1.0-t;
    return (uint64_t)((1.0-u*u*u)*px+0.5);
}
/** Returns 1 when two notes belong to the same party member or the same monster. */
static int same_target(const dn_entry *a,const dn_entry *b){return a->party==b->party && a->index==b->index;}

/* Default: each value pops, rises and fades; newer ones push older ones up. */
static int draw_rise(uint8_t *p,const uint8_t *ds,uint64_t now)
{
    int drawn=0;unsigned i,j;
    for(i=0;i<DN_MAX;++i){
        dn_entry *e=&dn_entries[i];uint64_t age,kick=0;int ax,ay,newer=0,k,y;char s[16];clip c;
        if(!e->used || !e->stamped)continue;
        age=now-e->t0;
        if(age>RISE_LIFE){e->used=0;continue;}
        for(j=0;j<DN_MAX;++j){const dn_entry *o=&dn_entries[j];
            if(o->used && o->stamped && o!=e && same_target(o,e) && o->seq>e->seq){
                ++newer;kick+=(now-o->t0)>=KICK_T?9u:(unsigned)((now-o->t0)*9u/KICK_T);}}
        if(!anchor(ds,e,&ax,&ay))continue;
        y=ay-(int)ease_px(age,RISE_LIFE,e->party?16u:12u)-(int)kick;
        if(e->party){c=(clip){0,0,320,200};}else{c=(clip){VIEW_X0,VIEW_Y0,VIEW_X1,VIEW_Y1};if(y<TEXT_TOP)continue;}
        format_value(s,sizeof s,e,e->value);
        k=kind_of(e,e->value);
        {int reduced=newer>0 || too_wide(s,e->party);
         uint8_t col=code(kind_codes[k][age<POP?0:age>RISE_LIFE-FADE?2:1]);
         draw_text(p,s,reduced,ax,y,col,1,&c);}
        drawn=1;
    }
    return drawn;
}

/* Stack: per target and kind one running total with a count; a hit within
 * HOLD + HOLD_FADE of the previous one continues the run. The total is held
 * HOLD after the run's last hit, then fades and drifts up. */
static int group(const dn_entry *e,const dn_entry *o,int k)
{
    return o->used && o->stamped && same_target(o,e) && kind_of(o,o->value)==k;
}
/** Returns 1 when a newer note of E's group (its target and kind K) exists: the group is drawn at that one. */
static int has_newer(const dn_entry *e,int k)
{
    unsigned j;
    for(j=0;j<DN_MAX;++j)if(group(e,&dn_entries[j],k) && dn_entries[j].seq>e->seq)return 1;
    return 0;
}
/** Collects the notes of E's group (its target and kind K) into RUN, oldest first; returns their number. */
static unsigned group_notes(const dn_entry *e,int k,dn_entry **run)
{
    unsigned j,a,b,n=0;
    for(j=0;j<DN_MAX;++j)if(group(e,&dn_entries[j],k))run[n++]=&dn_entries[j];
    for(a=1;a<n;++a)for(b=a;b>0 && run[b-1]->seq>run[b]->seq;--b){dn_entry *t=run[b];run[b]=run[b-1];run[b-1]=t;}
    return n;
}
/** The first note of the newest run among RUN's N notes (N >= 1): a note within GAP_MAX of the one before it
 *  continues the run. */
static unsigned run_start(dn_entry *const *run,unsigned n)
{
    unsigned first=n-1u;
    while(first>0 && run[first]->t0-run[first-1u]->t0<=GAP_MAX)--first;
    return first;
}
/** E's place in its target's stack of the kinds shown at NOW (damage, healing, misses, in that order): *SLOT the
 *  shown kinds before K, *KINDS all shown kinds. */
static void stack_slot(const dn_entry *e,int k,uint64_t now,unsigned *slot,unsigned *kinds)
{
    unsigned j;int kk;
    *slot=*kinds=0;
    for(kk=0;kk<3;++kk){
        for(j=0;j<DN_MAX;++j)if(group(e,&dn_entries[j],kk) && now-dn_entries[j].t0<=GAP_MAX)break;
        if(j<DN_MAX){if(kk<k)++*slot;++*kinds;}
    }
}
/**
 * Draws every live note group on page P at time NOW: the notes of one target and kind that arrived
 * close together are summed into one number at their newest note, and groups of one target are
 * stacked. Returns 1 when anything was drawn. The pass also retires notes as it meets their groups (the runs before
 * the newest, and a whole group older than GAP_MAX): a later group's stack place in the same pass sees them gone, so
 * the retiring stays inside the drawing loop.
 */
static int draw_stack(uint8_t *p,const uint8_t *ds,uint64_t now)
{
    int drawn=0;unsigned i,a;
    for(i=0;i<DN_MAX;++i){
        dn_entry *e=&dn_entries[i];dn_entry *run[DN_MAX];unsigned n,first,slot,kinds,count;
        int k,ax,ay,y,reduced;int32_t total=0;uint64_t age;char s[16],num[16];clip c;uint8_t col;
        if(!e->used || !e->stamped)continue;
        k=kind_of(e,e->value);
        if(has_newer(e,k))continue;
        n=group_notes(e,k,run);
        first=run_start(run,n);
        for(a=0;a<first;++a)run[a]->used=0;         /* earlier runs are over */
        age=now-e->t0;
        if(age>GAP_MAX){for(a=first;a<n;++a)run[a]->used=0;continue;}
        for(a=first;a<n;++a)total+=run[a]->value;
        count=n-first;
        if(!anchor(ds,e,&ax,&ay))continue;
        stack_slot(e,k,now,&slot,&kinds);
        y=ay+(int)slot*STACK_ROW-(int)(kinds-1u)*(STACK_ROW/2);   /* the shown kinds centred on the anchor */
        if(age>HOLD)y-=(int)((age-HOLD)*6u/HOLD_FADE);
        if(age<POP)y-=1;
        if(e->party)c=(clip){0,0,320,200};
        else{c=(clip){VIEW_X0,VIEW_Y0,VIEW_X1,VIEW_Y1};if(y<TEXT_TOP)y=TEXT_TOP;}
        format_value(s,sizeof s,e,total);
        reduced=too_wide(s,e->party);
        col=code(kind_codes[k][age<POP?0:age>HOLD?2:1]);
        draw_text(p,s,reduced,ax,y,col,1,&c);
        if(count>1u){
            snprintf(num,sizeof num,"x%u",count);
            draw_text(p,num,1,ax+text_width(s,reduced)/2+text_width(num,1)/2+2,y+3,code(age>HOLD?2u:COUNT_CODE),1,&c);
        }
        drawn=1;
    }
    return drawn;
}

const uint8_t *xeen_damage_numbers_page(const uint8_t *page,const uint8_t *palette,uint64_t now_ns)
{
    const uint8_t *ds;unsigned i;int any=0,drawn;
    (void)palette; /* Phase 270: read only by the removed developer capture */
    if(!dn.started || !page || !dn.game || !(ds=xeen_game_ds(dn.game)))return page;
    if(!now_ns)now_ns=(dn.fake_ns+=40u*MS);
    for(i=0;i<DN_MAX;++i)if(dn_entries[i].used){
        if(!dn_entries[i].stamped){dn_entries[i].stamped=1;dn_entries[i].t0=now_ns;}
        any=1;
    }
    if(!any){dn.composed=0;return page;}
    if(dn.composed && now_ns-dn.composed_ns<15u*MS && !memcmp(dn.source,page,sizeof dn.source))
        return dn.composed>0?dn.page:page;
    memcpy(dn.source,page,sizeof dn.source);dn.composed_ns=now_ns;
    memcpy(dn.page,page,sizeof dn.page);
    cover(ds);
    drawn=dn.stack?draw_stack(dn.page,ds,now_ns):draw_rise(dn.page,ds,now_ns);
    if(!drawn){dn.composed=-1;return page;}
    dn.composed=1;
    return dn.page;
}
