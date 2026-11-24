/* Add-on identify (owner-approved, not original behavior): the Identify
 * statistics text; see include/xeen/addon/identify.h. */
#include "xeen/addon/identify.h"
#include "xeen/items.h"
#include "xeen/combat.h"
#include "xeen/character_record.h"
#include "xeen/item_tables.h"
#include <stdio.h>
#include <string.h>

/* The item's Identify
 * statistics (3C96E's rules, xeen_item_details) in four cells. Weapon: the
 * to-hit (metal), min-max, the element or attribute, "x3 Kind"; armor: AC
 * and the resistance or attribute; accessory: the resistance or attribute;
 * misc: none. Elemental values are coloured by element (then the row colour
 * again), the rest keep the row colour. */
static const uint8_t element_colour[6]={33,10,7,39,36,25};  /* Fire Elec Cold Acid/Poison Energy Magic */
static const char *const attribute_short[10]={"Mgt","Int","Per","Spd","Acy","Lck","HP","SP","AC","Thv"};
static const char *const power_short[7]={"","Drg","Und","Glm","Bug","Mon","Bst"};
int xeen_item_stats_cells(const xeen_movement *m,uint16_t record,unsigned row,unsigned colour,unsigned category,
    xeen_item_stats *c)
{
    const uint8_t *s=m->ds+(uint16_t)(record+XEEN_CHARACTER_ITEM(category&3u,row));
    unsigned id=s[1];uint8_t mat=s[0];
    if(!c)return 0;
    memset(c,0,sizeof *c);
    if(category>2u || !id)return 0;
    if(mat<XEEN_ITEM_ELEMENT_MATERIALS){                                 /* element (36 shows none: 3C96E) */
        int v=m->ds[(uint16_t)((category?XEEN_DS_ELEMENT_RESISTANCE:XEEN_DS_ELEMENT_DAMAGE)+mat)];
        /* armor and accessories: a resistance, "+n Res" all in the colour
         * (an accessory's "Res" in the label cell) */
        unsigned e=element_colour[xeen_combat_element_category(mat)];
        if(v && category==2u){
            (void)snprintf(c->cell[XEEN_STAT_MOD],sizeof c->cell[0],"\f%02u%+d\f%02u",e,v,colour);
            (void)snprintf(c->cell[XEEN_STAT_LABEL],sizeof c->cell[0],"\f%02uRes\f%02u",e,colour);
        }else if(v)(void)snprintf(c->cell[XEEN_STAT_MOD],sizeof c->cell[0],category?"\f%02u%+d Res\f%02u":"\f%02u%+d\f%02u",
            e,v,colour);
    }else if(mat>=XEEN_ITEM_FIRST_ATTRIBUTE){
        int v=(int)(int8_t)m->ds[(uint16_t)(XEEN_DS_ATTRIBUTE_BY_MATERIAL+mat)];const char *a=attribute_short[xeen_combat_attribute_category(mat)%10u];
        if(category==2u){                                                /* accessories: "+n" and "Abr" */
            (void)snprintf(c->cell[XEEN_STAT_MOD],sizeof c->cell[0],"%+d",v);
            (void)snprintf(c->cell[XEEN_STAT_LABEL],sizeof c->cell[0],"%s",a);
        }else (void)snprintf(c->cell[XEEN_STAT_MOD],sizeof c->cell[0],"%+d%s",v,a);
    }
    if(category==0u){
        int lo=m->ds[(uint16_t)(XEEN_DS_WEAPON_DICE+id)],hi=(int)m->ds[(uint16_t)(XEEN_DS_WEAPON_DICE+id)]*m->ds[(uint16_t)(XEEN_DS_WEAPON_SIDES+id)];
        unsigned power=s[2]&0x3fu;
        if(mat>=XEEN_ITEM_FIRST_METAL && mat<XEEN_ITEM_FIRST_ATTRIBUTE){
            lo+=(int8_t)m->ds[(uint16_t)(XEEN_DS_METAL_DAMAGE_BY_MATERIAL+mat)];hi+=(int8_t)m->ds[(uint16_t)(XEEN_DS_METAL_DAMAGE_BY_MATERIAL+mat)];
            (void)snprintf(c->cell[XEEN_STAT_HIT],sizeof c->cell[0],"%+d",(int)(int8_t)m->ds[(uint16_t)(XEEN_DS_METAL_TO_HIT_BY_MATERIAL+mat)]);
        }
        /* zero-padded "min..max" (two digits always suffice: 0..56 / -3..74);
         * 3C96E clamps only the minimum, so a leather club is "00..-3". */
        (void)snprintf(c->cell[XEEN_STAT_MAIN],sizeof c->cell[0],"%02d..%02d",lo<1?0:lo,hi);
        if(power>=1u && power<=6u)(void)snprintf(c->cell[XEEN_STAT_POWER],sizeof c->cell[0],"x3 %s",power_short[power]);
    }else if(category==1u){
        int v=m->ds[(uint16_t)(XEEN_DS_ARMOR_AC+id)];
        if(mat>=XEEN_ITEM_FIRST_METAL && mat<XEEN_ITEM_FIRST_ATTRIBUTE)v+=(int8_t)m->ds[(uint16_t)(XEEN_DS_METAL_AC_BY_MATERIAL+mat)];
        (void)snprintf(c->cell[XEEN_STAT_MAIN],sizeof c->cell[0],"AC%02d",v);  /* -2..30: "AC06", "AC-2" */
    }
    {unsigned k;for(k=0;k<XEEN_STAT_CELLS;++k)if(c->cell[k][0])return 1;}
    return 0;
}
int xeen_item_name_addon(const xeen_movement *m,uint16_t record,unsigned row,unsigned colour,unsigned category,
    const uint8_t *mae,size_t mae_size,int drop_material,char *out,size_t n)
{
    char text[200];int w;
    if(!out || !n || category>2u)return 0;
    if(!xeen_item_text_format(m,m->ds+(uint16_t)(record+XEEN_CHARACTER_ITEM(category&3u,row)),colour,category,mae,mae_size,
        XEEN_ITEM_TEXT_TRIM|(drop_material?XEEN_ITEM_TEXT_NO_MATERIAL:0u),text))return 0;
    w=snprintf(out,n,"%s",text);
    return w>0 && (size_t)w<n;
}
int xeen_item_text_addon(const xeen_movement *m,uint16_t record,unsigned row,unsigned colour,unsigned category,
    const uint8_t *mae,size_t mae_size,int drop_material,char *out,size_t n)
{
    xeen_item_stats c;char text[200],suffix[120];size_t at=0,len;unsigned k;int w;
    if(!out || !n || !xeen_item_stats_cells(m,record,row,colour,category,&c))return 0;
    suffix[0]=0;
    for(k=0;k<XEEN_STAT_CELLS;++k){
        const char *v=c.cell[k];
        char tight[16];
        if(!v[0])continue;
        if(k==XEEN_STAT_POWER){(void)snprintf(tight,sizeof tight,"x3%s",v+3);v=tight;}   /* inline: "x3Mon" */
        w=snprintf(suffix+at,sizeof suffix-at,"%s%s",at?" ":"",v);
        if(w<0 || (size_t)w>=sizeof suffix-at)return 0;
        at+=(size_t)w;
    }
    if(!xeen_item_name_addon(m,record,row,colour,category,mae,mae_size,drop_material,text,sizeof text))return 0;
    len=strlen(text);   /* the name's own trailing space, else one */
    w=snprintf(out,n,"%s%s%s",text,len && text[len-1u]==' '?"":" ",suffix);
    return w>0 && (size_t)w<n;
}
