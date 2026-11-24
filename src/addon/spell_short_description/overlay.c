/* spell_short_description (Phase 203 add-on, NOT original behavior): the
 * Cast Spell dialog's spell box, drawn on a copy of the displayed page
 * (include/xeen/addon/spell_short_description.h). DS is read, never
 * written. Self-contained: no other add-on, no bug fix. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xeen/addon/spell_short_description.h"
#include "xeen/combat.h"
#include "xeen/character_record.h"
#include "xeen/party.h"
#include "xeen/spells.h"
#include "xeen/ds_globals.h"
#include "xeen/game.h"
#include "xeen/resources.h"
#include "xeen/sprites.h"
#include "xeen/text_window.h"
/* The driver's window rectangles (generated from XEEN.DAT, as presentation.c
 * uses them): another open window over the box hides it. */
#include "../../graphics/window_table.inc"

int xeen_spell_short_description_on;

/* The box: under the Cast Spell window 0Ah (226,0,93,146), over the movement
 * arrows; the text inside xeen_text_frame's 8-pixel frame (77 px wide, 4
 * lines of the reduced font's 9 rows). */
#define BOX_X 226
#define BOX_Y 146
#define BOX_W 93
#define BOX_H 54
#define CAST_WINDOW 0x0au

/* By spell id (DS:201E's values, spldesc.bin's order). Reduced font, left
 * justified, the lines broken by texts.inc's generator (the FNT widths). */
static const char *const texts[76]={
#include "texts.inc"
};

static struct {
    int started;
    const xeen_game *game;
    xeen_text_window font;
    uint8_t page[64000];
} ssd;

int xeen_spell_short_description_start(const void *sprite_archives)
{
    const xeen_sprite_archives *a=sprite_archives;unsigned i;
    xeen_spell_short_description_stop();
    if(!a)return 0;
    for(i=0;i<2u && !ssd.font.loaded;++i)if(a->cc[i])(void)xeen_text_window_load(&ssd.font,a->cc[i]);
    if(!ssd.font.loaded)return 0;
    ssd.started=1;xeen_spell_short_description_on=1;
    return 1;
}
void xeen_spell_short_description_stop(void)
{
    xeen_spell_short_description_on=0;ssd.started=0;ssd.game=NULL;
    memset(&ssd.font,0,sizeof ssd.font);
}
void xeen_spell_short_description_bind(const xeen_game *g){ssd.game=g;}

/* The window table's sized entries (42..47 are zero-size). */
#define TABLE_WINDOWS 42u

/* The spell the dialog shows, -1 for no box. */
static int shown_spell(const uint8_t *ds,const xeen_movement *m)
{
    xeen_game_cast_view c;unsigned w,index;uint8_t at;uint16_t rec;
    if(!xeen_game_cast(ssd.game,&c))return -1;
    if(!ds[XEEN_DS_WINDOW_FLAG(CAST_WINDOW)])return -1;
    for(w=0;w<TABLE_WINDOWS;++w){
        const uint16_t *r=window_rects[w];
        if(w==CAST_WINDOW || !ds[XEEN_DS_WINDOW_FLAG(w)] || !r[2] || !r[3])continue;
        if(r[0]<BOX_X+BOX_W && r[0]+r[2]>BOX_X && r[1]<BOX_Y+BOX_H && r[1]+r[3]>BOX_Y)return -1;
    }
    rec=c.record;
    if((uint32_t)rec+XEEN_CHARACTER_BYTES>0x10000u)return -1;
    if(!xeen_combat_max_sp(m,ds+rec))return -1;   /* 46470 */
    at=ds[(uint16_t)(rec+XEEN_CHARACTER_READY_SPELL)];
    /* no ready spell: the list's last entry, its "no spell" (3DA0F) */
    index=c.list_index>=0?(unsigned)c.list_index:at==XEEN_NO_READY_SPELL?XEEN_CLASS_SPELL_LIST-1u:at;
    if(index>=XEEN_CLASS_SPELL_LIST)return -1;
    w=ds[(uint16_t)(XEEN_DS_CLASS_SPELL_LISTS+XEEN_CLASS_SPELL_LIST*xeen_game_class_spell_list(ds[(uint16_t)(rec+XEEN_CHARACTER_CLASS)])+index)];
    if(w>=XEEN_SPELL_COUNT)return -1;
    return (int)w;
}

const uint8_t *xeen_spell_short_description_page(const uint8_t *page)
{
    const uint8_t *ds;const xeen_movement *m;xeen_game_view v;int spell;
    uint16_t box[4]={BOX_X,BOX_Y,BOX_W,BOX_H},inner[4]={BOX_X+8,BOX_Y+7,BOX_W-16,BOX_H-12};
    char text[160];
    if(!ssd.started || !page || !ssd.game)return page;
    if(!(ds=xeen_game_ds(ssd.game)) || !(m=xeen_game_movement(ssd.game)))return page;
    v=xeen_game_inspect(ssd.game);
    spell=v.menu_stage || v.game_over_stage?-1:shown_spell(ds,m);
    if(spell<0)return page;
    memcpy(ssd.page,page,sizeof ssd.page);
    (void)xeen_text_frame(ssd.page,sizeof ssd.page,box);
    (void)snprintf(text,sizeof text,"\002\003l%s",texts[spell]);       /* reduced font, left */
    (void)xeen_text_write(&ssd.font,ssd.page,sizeof ssd.page,inner,text,NULL);
    return ssd.page;
}
