/* town_icons (Phase 207 add-on, NOT original behavior): icons for the town
 * places on the M map and the minimap, drawn on a copy of the displayed page
 * (include/xeen/addon/town_icons.h). The game's DS and framebuffer are read,
 * never written; the lookups run on a private copy of the game state.
 * Self-contained: no other add-on, no bug fix. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xeen/addon/town_icons.h"
#include "xeen/minimap.h"
#include "xeen/movement.h"
#include "xeen/ds_globals.h"
#include "xeen/windows.h"

int xeen_town_icons_on;

enum { TI_BANK, TI_BLACKSMITH, TI_GUILD, TI_TAVERN, TI_TEMPLE, TI_TRAINING, TI_MIRROR,
       TI_TENT_CLOUDS, TI_TENT_DARKSIDE, TI_PYRAMID, TI_TOWER, TI_KINDS };
#include "icons.inc"

/* {world (96E2), map (ED99), x, y, kind} from make_table.py; an outdoor map's x, y (Phase 309, the Skyroad
 * tents) may lie in a neighbouring map, across its edges (-16..31, as 1C680 looks them up). */
static const struct { uint8_t world,map; int8_t x,y; uint8_t kind; } ti_cells[]={
#include "table.inc"
};

#define TI_W 9                 /* the icon with its outline */
#define TI_H 7
#define TI_MAX 64              /* icons of one pass (a town has at most 21) */
#define PAGE_W 320
#define PAGE_SIZE 64000u

/* An icon: its box at X, Y; only its part inside the view's visible area
 * (X0..X1, Y0..Y1, exclusive ends) is compared and drawn. */
typedef struct { int16_t x,y,x0,y0,x1,y1; uint8_t kind; uint8_t under[TI_W*TI_H]; } ti_icon;
typedef struct { unsigned count; ti_icon icons[TI_MAX]; } ti_record;

static struct {
    int started;
    uint8_t art[TI_KINDS][TI_W*TI_H];      /* 0xFF transparent */
    xeen_movement *scratch;                /* the lookups' private copy */
    ti_record minimap,map;                 /* the latest pass of each view */
    uint8_t page[PAGE_SIZE];
} ti;

/* The art with its outline: a black pixel beside (8 directions) any art pixel. */
static void build_art(void)
{
    unsigned k;int i,j,di,dj;
    for(k=0;k<TI_KINDS;++k){
        uint8_t *a=ti.art[k];
        memset(a,0xff,TI_W*TI_H);
        for(j=0;j<TI_H;++j)for(i=0;i<TI_W;++i){
            int gi=i-1,gj=j-1,near=0;
            if(gi>=0 && gi<7 && gj>=0 && gj<5 && ti_art[k][gj][gi]!='.'){a[j*TI_W+i]=ti_colour(ti_art[k][gj][gi]);continue;}
            for(dj=-1;dj<=1 && !near;++dj)for(di=-1;di<=1 && !near;++di){
                int x=gi+di,y=gj+dj;
                if(x>=0 && x<7 && y>=0 && y<5 && ti_art[k][y][x]!='.')near=1;
            }
            if(near)a[j*TI_W+i]=0;
        }
    }
}

/**
 * Starts the add-on: allocates its scratch state and builds the icon art. Returns 0 when the
 * allocation fails.
 */
int xeen_town_icons_start(void)
{
    xeen_town_icons_stop();
    if(!(ti.scratch=malloc(sizeof *ti.scratch)))return 0;
    build_art();
    ti.minimap.count=ti.map.count=0;
    ti.started=1;xeen_town_icons_on=1;
    return 1;
}
/** Stops the add-on and frees its state. */
void xeen_town_icons_stop(void)
{
    xeen_town_icons_on=0;ti.started=0;
    free(ti.scratch);ti.scratch=NULL;
    ti.minimap.count=ti.map.count=0;
}

/* The view's cell test on the private copy: 1C680(x, y, FFFFh, 0) not 8888h
 * and F492 set (EYE: the minimap's Wizard Eye instead of F492). The outdoor
 * passes (1E4E3, 1DB8E) test the same F492 (and the minimap the Wizard Eye),
 * over the same grids. */
static int seen(int x,int y,int eye)
{
    uint16_t r=0;
    if(xeen_maze_lookup(ti.scratch,(int16_t)x,(int16_t)y,0xffffu,0,&r)!=XEEN_OK || r==0x8888u)return 0;
    return eye || xeen_read16(ti.scratch,XEEN_DS_CELL_SEEN);
}

/* One pass: every table cell of this map inside the view's COLS x ROWS grid
 * (column 0 is cell x OX, row 0 cell y OY, at screen SX, SY), seen, not the
 * party's, whose icon centre is inside CLIP, the view's visible area (an
 * icon at its last row or column is cut by the view's frame, as the cell
 * itself); the pixels under it from FB. */
typedef struct { int ox,oy,cols,rows,sx,sy; int clip[4]; int eye; } ti_view;
/**
 * Collects the icons to draw over a map view (V) of the current outdoor map: the town and building
 * cells the party has seen that lie inside the view, except the party's own cell.
 */
static void record(ti_record *rec,const xeen_movement *m,const ti_view *v,const uint8_t *fb,size_t size)
{
    const uint8_t *ds=m->ds;size_t k;
    int px=xeen_signed_byte(ds[XEEN_DS_PARTY_X]),py=xeen_signed_byte(ds[XEEN_DS_PARTY_Y]);
    rec->count=0;
    if(!ti.started || !fb || size<PAGE_SIZE)return;   /* the towns (indoor) and, Phase 309, the Skyroad (outdoor) */
    *ti.scratch=*m;
    for(k=0;k<sizeof ti_cells/sizeof ti_cells[0];++k){
        int col,row,x,y,i,j;ti_icon *ic;
        if(ti_cells[k].world!=ds[XEEN_DS_MAP_SIDE] || ti_cells[k].map!=ds[XEEN_DS_PARTY_MAP])continue;
        col=ti_cells[k].x-v->ox;row=v->oy-ti_cells[k].y;
        if(col<0 || col>=v->cols || row<0 || row>=v->rows)continue;
        if(ti_cells[k].x==px && ti_cells[k].y==py)continue;           /* the party's arrow */
        x=v->sx+10*col+3;y=v->sy+8*row+3;                              /* the cell's inside */
        if(x+TI_W/2<v->clip[0] || y+TI_H/2<v->clip[1] || x+TI_W/2>=v->clip[2] || y+TI_H/2>=v->clip[3])continue;
        if(!seen(ti_cells[k].x,ti_cells[k].y,v->eye))continue;
        if(rec->count>=TI_MAX)break;
        ic=&rec->icons[rec->count++];
        ic->x=(int16_t)x;ic->y=(int16_t)y;ic->kind=ti_cells[k].kind;
        ic->x0=(int16_t)(x>v->clip[0]?x:v->clip[0]);ic->y0=(int16_t)(y>v->clip[1]?y:v->clip[1]);
        ic->x1=(int16_t)(x+TI_W<v->clip[2]?x+TI_W:v->clip[2]);ic->y1=(int16_t)(y+TI_H<v->clip[3]?y+TI_H:v->clip[3]);
        for(j=ic->y0;j<ic->y1;++j)for(i=ic->x0;i<ic->x1;++i)ic->under[(j-y)*TI_W+i-x]=fb[j*PAGE_W+i];
    }
}

/* 1E467's indoor grid: 7x7 cells at (EDh + 10 col, 0Ch + 8 row), the party
 * in the middle; drawn only while ED9F or EDA0; 1E467 clears EDA0 for the
 * pass while ED9F is on (no Wizard Eye then); 986E/9876/9894 draw nothing. */
void xeen_town_icons_minimap(const xeen_movement *m,const uint8_t *fb,size_t size)
{
    const uint8_t *ds;ti_view v;
    if(!ti.started || !m)return;
    ds=m->ds;
    if(ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_COMBAT)] || ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_RIGHT_COLUMN)] ||
       ds[XEEN_DS_WINDOW_FLAG(XEEN_WINDOW_SPEECH)] || (!ds[XEEN_DS_MINIMAP_SHOWN] && !ds[XEEN_DS_EFFECT_WIZARD_EYE])){ti.minimap.count=0;return;}
    v.ox=xeen_signed_byte(ds[XEEN_DS_PARTY_X])-3;v.oy=xeen_signed_byte(ds[XEEN_DS_PARTY_Y])+3;v.cols=7;v.rows=7;
    v.sx=0xed;v.sy=0x0c;
    v.clip[0]=0xed;v.clip[1]=0x0c;v.clip[2]=0xed+70;v.clip[3]=0x0c+56;   /* inside its frame (frame 6) */
    v.eye=ds[XEEN_DS_EFFECT_WIZARD_EYE] && !ds[XEEN_DS_MINIMAP_SHOWN];
    record(&ti.minimap,m,&v,fb,size);
}
/* 1DA2D's indoor view: 16x16 cells at (50h + 10 col, 26h + 8 row), column 0
 * the view's x - 7, row 0 its y + 7. */
void xeen_town_icons_map(const xeen_movement *m,int view_x,int view_y,const uint8_t *fb,size_t size)
{
    ti_view v;
    if(!ti.started || !m)return;
    v.ox=view_x-7;v.oy=view_y+7;v.cols=16;v.rows=16;v.sx=0x50;v.sy=0x26;
    v.clip[0]=0x50;v.clip[1]=0x26;v.clip[2]=0x50+160;v.clip[3]=0x26+128;   /* window 5's map area */
    v.eye=0;
    record(&ti.map,m,&v,fb,size);
}

/* Draws the record's icons whose pixels underneath are still on the page. */
static unsigned draw(const ti_record *rec,const uint8_t *page,int *copied)
{
    unsigned n,shown=0;int i,j;
    for(n=0;n<rec->count;++n){
        const ti_icon *ic=&rec->icons[n];int same=1,w=ic->x1-ic->x0;
        for(j=ic->y0;j<ic->y1 && same;++j)
            same=!memcmp(page+j*PAGE_W+ic->x0,ic->under+(j-ic->y)*TI_W+(ic->x0-ic->x),(size_t)w);
        if(!same)continue;
        if(!*copied){memcpy(ti.page,page,PAGE_SIZE);*copied=1;}
        for(j=ic->y0;j<ic->y1;++j)for(i=ic->x0;i<ic->x1;++i){
            uint8_t c=ti.art[ic->kind][(j-ic->y)*TI_W+i-ic->x];
            if(c!=0xffu)ti.page[j*PAGE_W+i]=c;
        }
        ++shown;
    }
    return shown;
}
const uint8_t *xeen_town_icons_page(const uint8_t *page)
{
    int copied=0;
    if(!ti.started || !page || (!ti.minimap.count && !ti.map.count))return page;
    /* The pixels under each icon are compared on PAGE itself; the icons go
     * to the copy. */
    (void)draw(&ti.map,page,&copied);
    (void)draw(&ti.minimap,page,&copied);
    if(!copied)return page;
    return ti.page;
}
