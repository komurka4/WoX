/* monster_difficulty (Phase 201 add-on, NOT original behavior): placement and
 * drawing of the difficulty icons on a copy of the displayed page
 * (include/xeen/addon/monster_difficulty.h). DS, the monster database and the
 * sprite cache are read, never written. Self-contained: no other add-on. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "assess.h"
#include "xeen/addon/monster_difficulty.h"
#include "xeen/ds_globals.h"
#include "xeen/game.h"
#include "xeen/map_objects.h"
#include "xeen/monster_record.h"
#include "xeen/raster.h"
#include "xeen/sprites.h"
/* The driver's window rectangles (generated from XEEN.DAT, as presentation.c
 * uses them): an open window (DS 986Ch + n) hides the icons under it. */
#include "../../graphics/window_table.inc"
#include "icons.inc"

int xeen_monster_difficulty_on;

/* The 3D view (window 3's inside). */
#define VIEW_X0 8
#define VIEW_X1 224
#define VIEW_Y0 8
#define VIEW_Y1 140
#define GAP 2                 /* px between the sprite's top and the icon */
#define SAMPLES 48u           /* sprite pixels checked on the displayed page */
#define ANCHORS 64u
#define PLACES 16u
#define SPRITES 4u

/* The monster view slots 98B4h..98CDh and their draw records (12 bytes:
 * handle, frame, x, y, scale, flags). Indoor (F608 = 0): the records 1ED46
 * writes per slot (src/graphics/scene.c monster_views). Outdoor (F608 != 0): 2126C
 * writes the slot's word at the record's frame field (+2)
 * (src/graphics/scene.c assign_outdoor_slots, the slots' outdoor records in
 * monster_views). Both tables are this add-on's own copies of the record
 * starts; scene.c's table is not shared. */
static const uint16_t indoor_record[26]={
    0x10b2,0x106a,0x108e,0x0f92,0x0f7a,0x0f86,0x0e5a,0x0e42,0x0e4e,0x0caa,0x0c92,0x0c9e,0x0f62,
    0x0f6e,0x0e12,0x0e2a,0x0c4a,0x0c6e,0x0c7a,0x0c86,0x0e1e,0x0e36,0x0c32,0x0c56,0x0c3e,0x0c62};
static const uint16_t outdoor_record[26]={
    0x16ee,0x16a6,0x16ca,0x15ce,0x15b6,0x15c2,0x14ea,0x14d2,0x14de,0x13d6,0x13be,0x13ca,0x159e,
    0x15aa,0x14a2,0x14ba,0x1376,0x139a,0x13a6,0x13b2,0x14ae,0x14c6,0x135e,0x1382,0x136a,0x138e};
/* Each slot's depth (the same in both tables): 0 the adjacent cell, 3 the
 * farthest. */
static const uint8_t slot_depth[26]={0,0,0,1,1,1,2,2,2,3,3,3,1,1,2,2,3,3,3,3,2,2,3,3,3,3};

typedef struct {
    int used;unsigned age;
    uint8_t record[12];
    char sprite[16];          /* the handle's resource name (handles are reused) */
    int ok;                   /* the frame drew pixels inside the view */
    int effect;               /* drawn with a colour mode: the page check is skipped */
    unsigned n;uint16_t sx[SAMPLES];uint8_t sy[SAMPLES],sc[SAMPLES];
} anchor_t;

/* The icon's fixed place for a sprite at one draw position (owner choice
 * after the first test): from every frame of the sprite together, so the
 * icon stays still while the monster animates. */
#define MASK_W (VIEW_X1-VIEW_X0)
#define MASK_H (VIEW_Y1-VIEW_Y0)
typedef struct {
    int used;unsigned age;
    char sprite[16];int16_t x,y;uint16_t scale,flags;   /* the record without its frame */
    int ok,top,bottom,cx;     /* all frames: the top and bottom rows, the head's mean x */
    int have[3],ix[3],iy[3];  /* the icon's top-left corner per icon size (depth 0, 1, 2-3) */
    uint8_t mask[MASK_H][MASK_W];   /* a pixel of any frame */
} place_t;

static struct {
    int started;
    const xeen_sprite_archives *archives;
    const xeen_game *game;
    unsigned tick;
    struct { char name[16];uint8_t archive;uint8_t *bytes;size_t size;unsigned age; } sprites[SPRITES];
    anchor_t anchors[ANCHORS];
    place_t places[PLACES];
    uint16_t sum[MASK_H+1][MASK_W+1];
    uint8_t scratch[2][64000];
    uint8_t page[64000];
} md;

/** Reads the little-endian word at P. */
static uint16_t rd16(const uint8_t *p){return (uint16_t)(p[0]|p[1]<<8);}

int xeen_monster_difficulty_start(const void *sprite_archives)
{
    if(!sprite_archives)return 0;
    xeen_monster_difficulty_stop();
    md.archives=sprite_archives;md.started=1;
    xeen_monster_difficulty_on=1;
    return 1;
}
void xeen_monster_difficulty_stop(void)
{
    unsigned i;
    xeen_monster_difficulty_on=0;md.started=0;md.game=NULL;
    for(i=0;i<SPRITES;++i){free(md.sprites[i].bytes);memset(&md.sprites[i],0,sizeof md.sprites[i]);}
    memset(md.anchors,0,sizeof md.anchors);memset(md.places,0,sizeof md.places);
}
void xeen_monster_difficulty_bind(const xeen_game *g){md.game=g;}

/* ------------------------------------------------------------ windows --- */
static struct { int n; uint16_t r[42][4]; } covered;
/**
 * Collects the rectangles of the open windows (DS:986C flags), so icons are not drawn over them.
 */
static void cover(const uint8_t *ds)
{
    unsigned w;covered.n=0;
    for(w=0;w<42u;++w)if(ds[XEEN_DS_WINDOW_FLAG(w)] && window_rects[w][2] && window_rects[w][3])
        memcpy(covered.r[covered.n++],window_rects[w],sizeof covered.r[0]);
}
/** Returns 1 when (X, Y) lies outside the 3D view or inside an open window. */
static int hidden(int x,int y)
{
    int i;
    if(x<VIEW_X0 || x>=VIEW_X1 || y<VIEW_Y0 || y>=VIEW_Y1)return 1;
    for(i=0;i<covered.n;++i){const uint16_t *r=covered.r[i];
        if(x>=r[0] && x<r[0]+r[2] && y>=r[1] && y<r[1]+r[3])return 1;}
    return 0;
}

/* ------------------------------------------------------------ sprites --- */
static const uint8_t *sprite_bytes(const xeen_sprite *sp,size_t *size)
{
    unsigned i,slot=0;const xeen_archive *a;xeen_resource_info info;size_t got=0;uint8_t *b;
    ++md.tick;
    for(i=0;i<SPRITES;++i)if(md.sprites[i].bytes && md.sprites[i].archive==sp->archive &&
        !strncmp(md.sprites[i].name,sp->name,sizeof md.sprites[i].name)){
        md.sprites[i].age=md.tick;*size=md.sprites[i].size;return md.sprites[i].bytes;}
    if(sp->archive>XSP_CUR)return NULL;
    a=sp->archive==XSP_CUR?md.archives->cur:md.archives->cc[sp->archive];
    if(!a || xeen_archive_find_name(a,sp->name,&info)!=XR_OK)return NULL;
    if(!(b=malloc(info.size?info.size:1u)))return NULL;
    if(xeen_archive_extract(a,sp->archive==XSP_CUR?XR_BANK_STATE:XR_BANK_ASSETS_XOR35,&info,b,info.size,&got)!=XR_OK){free(b);return NULL;}
    for(i=1;i<SPRITES;++i)if(md.sprites[i].age<md.sprites[slot].age)slot=i;
    free(md.sprites[slot].bytes);
    md.sprites[slot].bytes=b;md.sprites[slot].size=got;md.sprites[slot].archive=sp->archive;md.sprites[slot].age=md.tick;
    memcpy(md.sprites[slot].name,sp->name,sizeof md.sprites[slot].name);
    *size=got;return b;
}

/* The sprite's outline: its frame drawn by the engine's own renderer on two
 * scratch pages (0 and FFh): a pixel differing from its page is drawn. */
static const anchor_t *anchor(const xeen_sprite_cache *cache,const uint8_t record[12])
{
    unsigned i,slot=0,n=0,stride,k=0;anchor_t *a;const xeen_sprite *sp;const uint8_t *bytes;size_t size=0;
    uint16_t handle=rd16(record),frame=rd16(record+2),scale=rd16(record+8),flags=rd16(record+10);
    int16_t x=(int16_t)rd16(record+4),y=(int16_t)rd16(record+6);int px,py;
    if(handle==0 || handle==0xffffu || frame==0xffffu || scale==0xffffu || !(sp=xeen_sprite_get(cache,handle)))return NULL;
    ++md.tick;
    for(i=0;i<ANCHORS;++i)if(md.anchors[i].used && !memcmp(md.anchors[i].record,record,12) &&
        !strncmp(md.anchors[i].sprite,sp->name,sizeof md.anchors[i].sprite)){
        md.anchors[i].age=md.tick;return md.anchors[i].ok?&md.anchors[i]:NULL;}
    for(i=1;i<ANCHORS;++i)if(!md.anchors[i].used || md.anchors[i].age<md.anchors[slot].age){slot=i;if(!md.anchors[i].used)break;}
    a=&md.anchors[slot];memset(a,0,sizeof *a);a->used=1;a->age=md.tick;memcpy(a->record,record,12);
    memcpy(a->sprite,sp->name,sizeof a->sprite);
    if(!(bytes=sprite_bytes(sp,&size)))return NULL;
    memset(md.scratch[0],0,64000);memset(md.scratch[1],255,64000);
    a->effect=(flags&0x1fffu)!=0;
    flags&=0xe000u;                       /* the colour-mode bits are effects, not the outline */
    if(xeen_raster_frame(bytes,size,frame,md.scratch[0],64000,x,y,scale,flags)!=XEEN_OK ||
       xeen_raster_frame(bytes,size,frame,md.scratch[1],64000,x,y,scale,flags)!=XEEN_OK)return NULL;
#define DRAWN(px,py) (md.scratch[0][(py)*320+(px)]!=0 || md.scratch[1][(py)*320+(px)]!=255)
    for(py=VIEW_Y0;py<VIEW_Y1;++py)for(px=VIEW_X0;px<VIEW_X1;++px)if(DRAWN(px,py))++n;
    if(!n)return NULL;
    stride=n/SAMPLES+1u;
    for(py=VIEW_Y0;py<VIEW_Y1 && a->n<SAMPLES;++py)for(px=VIEW_X0;px<VIEW_X1 && a->n<SAMPLES;++px)if(DRAWN(px,py)){
        if(k++%stride)continue;
        {uint8_t c0=md.scratch[0][py*320+px],c1=md.scratch[1][py*320+px];
         a->sx[a->n]=(uint16_t)px;a->sy[a->n]=(uint8_t)py;a->sc[a->n]=c0==c1?c0:c1!=255?c1:c0;++a->n;}
    }
#undef DRAWN
    a->ok=1;
    return a;
}

/** Replaces an attack sprite's name (NNN.att) in SP by its standing sprite's (NNN.mon). */
static void standing_sprite(xeen_sprite *sp)
{
    size_t len;
    for(len=0;len<sizeof sp->name && sp->name[len];++len){}
    if(len>4u && len<sizeof sp->name && !strcmp(sp->name+len-4u,".att"))memcpy(sp->name+len-4u,".mon",4);
}
/** The place of sprite NAME drawn at X, Y, SCALE, FLAGS when it is cached (its age renewed), else NULL. */
static place_t *place_cached(const char *name,int16_t x,int16_t y,uint16_t scale,uint16_t flags)
{
    unsigned i;place_t *q;
    for(i=0;i<PLACES;++i){q=&md.places[i];
        if(q->used && q->x==x && q->y==y && q->scale==scale && q->flags==flags && !strncmp(q->sprite,name,sizeof q->sprite)){
            q->age=md.tick;return q;}}
    return NULL;
}
/** A new, empty place for sprite NAME drawn at X, Y, SCALE, FLAGS: the first unused entry, else the oldest one
 *  (entry 0 when none is older). */
static place_t *place_new(const char *name,int16_t x,int16_t y,uint16_t scale,uint16_t flags)
{
    unsigned i,slot=0;place_t *q;
    for(i=1;i<PLACES;++i)if(!md.places[i].used || md.places[i].age<md.places[slot].age){slot=i;if(!md.places[i].used)break;}
    q=&md.places[slot];memset(q,0,sizeof *q);q->used=1;q->age=md.tick;
    memcpy(q->sprite,name,sizeof q->sprite);q->x=x;q->y=y;q->scale=scale;q->flags=flags;
    return q;
}
/** Marks in Q's mask every view pixel that one of the FRAMES frames (at most 256) of the sprite BYTES draws at Q's
 *  position (a frame the renderer refuses is skipped). Returns 1 when any pixel was marked. */
static int place_mask(place_t *q,const uint8_t *bytes,size_t size,unsigned frames)
{
    unsigned f;int px,py,any=0;
    for(f=0;f<frames && f<256u;++f){
        memset(md.scratch[0],0,64000);memset(md.scratch[1],255,64000);
        if(xeen_raster_frame(bytes,size,(uint16_t)f,md.scratch[0],64000,q->x,q->y,q->scale,q->flags)!=XEEN_OK ||
           xeen_raster_frame(bytes,size,(uint16_t)f,md.scratch[1],64000,q->x,q->y,q->scale,q->flags)!=XEEN_OK)continue;
        for(py=VIEW_Y0;py<VIEW_Y1;++py)for(px=VIEW_X0;px<VIEW_X1;++px)
            if(md.scratch[0][py*320+px]!=0 || md.scratch[1][py*320+px]!=255){q->mask[py-VIEW_Y0][px-VIEW_X0]=1;any=1;}
    }
    return any;
}
/** Q's top and bottom rows (page rows) and the head's centre: the mean x of the mask's pixels in the top sixth of
 *  its rows (at least 4 rows). Q's mask has a pixel. */
static void place_head(place_t *q)
{
    int px,py,head;long hx=0,hn=0;
    q->top=VIEW_Y1;q->bottom=-1;
    for(py=0;py<MASK_H;++py)for(px=0;px<MASK_W;++px)if(q->mask[py][px]){
        if(py+VIEW_Y0<q->top)q->top=py+VIEW_Y0;
        q->bottom=py+VIEW_Y0;}
    head=q->top+((q->bottom-q->top)/6>3?(q->bottom-q->top)/6:3);
    for(py=q->top;py<=head;++py)for(px=0;px<MASK_W;++px)if(q->mask[py-VIEW_Y0][px]){hx+=px+VIEW_X0;++hn;}
    q->cx=(int)(hx/hn);
}
/* The fixed place: every frame of the monster's standing sprite drawn at the
 * record's position (the frames the record does not show yet included). In
 * combat the game swaps in the attack sprite (NNN.att) for the attack frames;
 * the place still comes from the standing sprite (NNN.mon), so the icon does
 * not move, and an attack frame may briefly reach it. */
static place_t *place(const xeen_sprite_cache *cache,const uint8_t record[12])
{
    place_t *q;const xeen_sprite *sp;xeen_sprite base;const uint8_t *bytes;size_t size=0;
    uint16_t handle=rd16(record),scale=rd16(record+8),flags=rd16(record+10)&0xe000u;
    int16_t x=(int16_t)rd16(record+4),y=(int16_t)rd16(record+6);
    if(handle==0 || handle==0xffffu || scale==0xffffu || !(sp=xeen_sprite_get(cache,handle)))return NULL;
    base=*sp;standing_sprite(&base);
    ++md.tick;
    if((q=place_cached(base.name,x,y,scale,flags)))return q->ok?q:NULL;
    q=place_new(base.name,x,y,scale,flags);
    if(!(bytes=sprite_bytes(&base,&size)) || size<2u)return NULL;
    if(!place_mask(q,bytes,size,rd16(bytes)))return NULL;   /* the header's frame count */
    place_head(q);q->ok=1;
    return q;
}

/* The icon's corner for a W x H icon (outline included): centred over the
 * head GAP px above the sprite; when that leaves the view, the spot beside
 * the head with the least cost: twice the squared distance from the centred
 * spot, plus COVER_COST for every pixel of any frame under the icon or
 * within GAP px of it (a few stray sparks may be covered, an arm or the head
 * not), left first on a tie. */
#define COVER_COST 200
/**
 * Chooses where a monster's icon of SIZE goes: above the monster when there is room, else the place
 * inside its outline with the fewest monster pixels. The choice is made once per size.
 */
static void icon_spot(place_t *q,unsigned size,int w,int h)
{
    int x0=q->cx-w/2,y0=q->top-GAP-h,x,y,best=-1,bx=0,by=0,ymax;
    if(q->have[size])return;
    q->have[size]=1;
    if(y0>=VIEW_Y0+1){q->ix[size]=x0;q->iy[size]=y0;return;}
    for(y=0;y<MASK_H;++y)for(x=0;x<MASK_W;++x)
        md.sum[y+1][x+1]=(uint16_t)(md.sum[y][x+1]+md.sum[y+1][x]-md.sum[y][x]+q->mask[y][x]);
    ymax=q->top+((q->bottom-q->top)/3>h?(q->bottom-q->top)/3:h);   /* stay beside the head */
    if(ymax>VIEW_Y1-h)ymax=VIEW_Y1-h;
    for(y=VIEW_Y0+1;y<=ymax;++y)for(x=VIEW_X0;x<=VIEW_X1-w;++x){
        int ax=x-GAP-VIEW_X0,ay=y-GAP-VIEW_Y0,bx2=x+w+GAP-VIEW_X0,by2=y+h+GAP-VIEW_Y0,d;
        if(ax<0)ax=0;
        if(ay<0)ay=0;
        if(bx2>MASK_W)bx2=MASK_W;
        if(by2>MASK_H)by2=MASK_H;
        d=((x-x0)*(x-x0)+(y-y0)*(y-y0))*2+(x>x0)+
          COVER_COST*(md.sum[by2][bx2]-md.sum[ay][bx2]-md.sum[by2][ax]+md.sum[ay][ax]);
        if(best<0 || d<best){best=d;bx=x;by=y;}
    }
    if(best>=0){q->ix[size]=bx;q->iy[size]=by;return;}
    q->ix[size]=x0;q->iy[size]=q->top+GAP;                        /* no spot in the view */
}
/* The sprite is on the displayed page: a quarter of its visible samples
 * match (nearer monsters and objects may cover the rest). */
static int on_page(const anchor_t *a,const uint8_t *page)
{
    unsigned i,checked=0,match=0;
    for(i=0;i<a->n;++i){
        if(hidden(a->sx[i],a->sy[i]))continue;
        ++checked;if(page[a->sy[i]*320+a->sx[i]]==a->sc[i])++match;
    }
    if(!checked)return 0;
    return a->effect || match*4u>=checked;
}

/* -------------------------------------------------------------- icons --- */
static void plot(uint8_t *p,int x,int y,uint8_t c){if(!hidden(x,y))p[y*320+x]=c;}
/**
 * Draws the difficulty icon of TIER for a monster at view DEPTH (large, small, or a dot) at its
 * chosen place, outline first, then the icon.
 */
static void draw_icon(uint8_t *p,unsigned tier,unsigned depth,place_t *q)
{
    const char *const *rows=depth==0u?md_icon9[tier-1u]:depth==1u?md_icon7[tier-1u]:md_dot;
    unsigned size=depth<2u?depth:2u;
    int w=(int)strlen(rows[0]),h=0,x0,y0,x,y,pass;
    while(h<10 && rows[h])++h;
    icon_spot(q,size,w+2,h+2);x0=q->ix[size];y0=q->iy[size];
    for(pass=0;pass<2;++pass)for(y=0;y<h;++y)for(x=0;x<w;++x){
        char ch=rows[y][x];uint8_t c;
        if(ch=='.')continue;
        if(!pass){int dx,dy;for(dy=-1;dy<=1;++dy)for(dx=-1;dx<=1;++dx)plot(p,x0+1+x+dx,y0+1+y+dy,MD_BLACK);continue;}
        c=ch=='L'?md_ramp[tier-1u][0]:ch=='M'?md_ramp[tier-1u][1]:ch=='D'?md_ramp[tier-1u][2]:ch=='W'?MD_WHITE:MD_BLACK;
        plot(p,x0+1+x,y0+1+y,c);
    }
}

const uint8_t *xeen_monster_difficulty_page(const uint8_t *page)
{
    struct { unsigned tier,depth;place_t *q; } shown[XEEN_VIEW_MONSTER_SLOTS];
    const uint8_t *ds,*db;const xeen_movement *m;const xeen_sprite_cache *cache;const uint16_t *table;
    size_t db_size=0;unsigned slot,n=0,i,pass;md_party party;xeen_game_view v;
    if(!md.started || !page || !md.game)return page;
    if(!(ds=xeen_game_ds(md.game)) || !(m=xeen_game_movement(md.game)) || !(cache=xeen_game_sprites(md.game)) ||
       !(db=xeen_game_monster_database(md.game,&db_size)))return page;
    v=xeen_game_inspect(md.game);
    if(v.menu_stage || v.game_over_stage || v.panel_stage)return page;
    if(!md_party_read(m,&party))return page;
    cover(ds);
    table=rd16(ds+XEEN_DS_OUTDOORS)?outdoor_record:indoor_record;
    for(slot=0;slot<XEEN_VIEW_MONSTER_SLOTS;++slot){
        unsigned index=ds[XEEN_DS_VIEW_MONSTER(slot)],id,tier;const anchor_t *a;place_t *q;
        if(index>=XEEN_MONSTER_UPDATE_SLOTS)continue;                /* FFh: empty */
        id=ds[XEEN_MAP_MONSTER(index)+XEEN_MAP_MONSTER_ID];
        if((size_t)id*XEEN_MONSTER_RECORD_BYTES+XEEN_MONSTER_RECORD_BYTES>db_size ||
           !rd16(ds+XEEN_MAP_MONSTER(index)+XEEN_MAP_MONSTER_HP))continue;   /* past the database, or dead */
        if(!(tier=md_tier(m,&party,db+(size_t)id*XEEN_MONSTER_RECORD_BYTES,index)))continue;   /* Trivial: nothing */
        if(!(a=anchor(cache,ds+table[slot])) || !on_page(a,page) || !(q=place(cache,ds+table[slot])))continue;
        shown[n].tier=tier;shown[n].depth=slot_depth[slot];shown[n].q=q;++n;
    }
    if(!n)return page;
    memcpy(md.page,page,sizeof md.page);
    for(pass=0;pass<3u;++pass)                  /* the far rows (depth 2 and up) first, then depth 1, then 0 */
        for(i=n;i-->0;)if((shown[i].depth>=2u?0u:2u-shown[i].depth)==pass)
            draw_icon(md.page,shown[i].tier,shown[i].depth,shown[i].q);
    return md.page;
}
