#ifndef XEEN_TEST_COMBAT_FIXTURE_H
#define XEEN_TEST_COMBAT_FIXTURE_H
/* Synthetic combat fixtures: an original-layout DS with the verified stat
 * thresholds, combat text formats and monster scene tables, a party at
 * (2,6) facing south on Clouds map 41 and monsters in the party cell. 96C4
 * is zero so 2A701 never needs map data. These fixtures exercise rules and
 * control flow; they are not authentic gameplay sessions. */
#include "xeen/combat.h"
#include "xeen/scene.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"check failed line %d: %s\n",__LINE__,#x);return 1;}}while(0)
#define FIXTURE_DB_ROWS 20u

typedef struct {const uint8_t *results;const uint8_t *lows;unsigned count,at;uint8_t incoming[128];} fixture_rng;
/* Returns a raw value whose original range conversion gives the scripted
 * result: low + (raw & 7FFF) % width == result. */
static inline uint16_t fixture_draw(void *user,uint16_t incoming)
{
    fixture_rng *r=user;uint16_t raw;
    if(r->at>=r->count)return 0;
    r->incoming[r->at]=(uint8_t)incoming;
    raw=(uint16_t)(r->results[r->at]-r->lows[r->at]);
    ++r->at;return raw;
}

static inline void fixture_world(xeen_movement *m,uint8_t *db)
{
    static const uint16_t thresholds[24]={3,5,7,9,11,13,15,17,19,21,25,30,
        35,40,50,75,100,125,150,175,200,225,250,65535};
    static const int8_t bonuses[24]={-5,-4,-3,-2,-1,0,1,2,3,4,5,6,7,8,9,10,
        11,12,13,14,15,16,17,20};
    static const char line1[]="\n\v020\f%2u%s\fd",line2[]="\n\v030\f%2u%s\fd",
        line3[]="\n\v040\f%2u%s\fd",title[]="\r\f00\003c\v000\t000\002Combat%s%s%s\001";
    unsigned i;
    xeen_movement_init(m,NULL,NULL);
    xeen_scene_init_monster_tables(m);
    memset(db,0,FIXTURE_DB_ROWS*60u);
    m->ds[0x94]=0;m->ds[0xed99]=41;m->ds[0xed96]=2;m->ds[0xed97]=2;m->ds[0xed98]=6;
    /* Phase65: the combat cleanup's scene builds the view (1C1C1 queries):
     * the synthetic map 41 occupies the centre cache slot (an all-zero maze). */
    xeen_write16(m,0xf46a+8,41);
    for(i=0;i<24u;++i){xeen_write16(m,(uint16_t)(0x2da8u+2u*i),thresholds[i]);m->ds[0x2dd8u+i]=(uint8_t)bonuses[i];}
    memcpy(m->ds+0x6771,line1,sizeof line1);memcpy(m->ds+0x677f,line2,sizeof line2);
    memcpy(m->ds+0x678d,line3,sizeof line3);memcpy(m->ds+0x679b,title,sizeof title);
    m->ds[0x96c4]=0;m->ds[0x96c0]=0;m->ds[0x9a1c]=1;
    xeen_write16(m,0xeff0,5);xeen_write16(m,0xeff2,610);xeen_write16(m,0xeff4,100);
    xeen_write16(m,0x8c92,1); /* minute 100 is night: no 30004 transition */
    m->ds[0x98b4]=m->ds[0x98b5]=m->ds[0x98b6]=0xff;
}

static inline uint8_t *fixture_member(xeen_movement *m,unsigned index,const char *name,uint8_t klass,
    uint8_t level,const uint8_t stats[7],uint8_t weapon,uint8_t material,int16_t hp)
{
    uint8_t *ch=m->ds+0xb41eu+index*0x162u;unsigned s;
    memset(ch,0,0x162);memcpy(ch,name,strlen(name));
    ch[0x13]=klass;for(s=0;s<7u;++s)ch[0x14u+2u*s]=stats[s];
    ch[0x23]=level;xeen_write16(m,(uint16_t)(0xb41eu+index*0x162u+0x15au),592); /* age 18 */
    xeen_write16(m,(uint16_t)(0xb41eu+index*0x162u+0x156u),(uint16_t)hp);
    ch[0xa6]=material;ch[0xa7]=weapon;ch[0xa8]=0;ch[0xa9]=1;
    if(m->ds[0xed8c]<=index)m->ds[0xed8c]=(uint8_t)(index+1u);
    return ch;
}
static inline void fixture_weapon(xeen_movement *m,uint8_t weapon,uint8_t dice,uint8_t sides)
{m->ds[0x2250u+weapon]=dice;m->ds[0x2273u+weapon]=sides;}

static inline uint8_t *fixture_monster(xeen_movement *m,uint8_t *db,uint8_t record,uint8_t id,
    uint16_t hp,uint8_t armor,uint8_t speed,uint8_t attacks,uint8_t strikes,uint8_t damage,
    uint8_t hit_chance,uint32_t experience)
{
    uint16_t rec=(uint16_t)(0xe530u+20u*record);uint8_t *row=db+(size_t)id*60u;
    memset(m->ds+rec,0,20);m->ds[rec]=m->ds[0xed97];m->ds[(uint16_t)(rec+1u)]=m->ds[0xed98];
    m->ds[(uint16_t)(rec+4u)]=1;m->ds[(uint16_t)(rec+6u)]=id;xeen_write16(m,(uint16_t)(rec+18u),hp);
    if(m->ds[0x9c59]<=record)m->ds[0x9c59]=(uint8_t)(record+1u);
    memset(row,0,60);memcpy(row,"Fixture Beast",13);
    row[0x10]=(uint8_t)experience;row[0x11]=(uint8_t)(experience>>8);
    row[0x12]=(uint8_t)(experience>>16);row[0x13]=(uint8_t)(experience>>24);
    row[0x14]=(uint8_t)hp;row[0x15]=(uint8_t)(hp>>8);row[0x16]=armor;row[0x17]=speed;
    row[0x18]=attacks;row[0x19]=16;row[0x1a]=strikes;row[0x1c]=damage;row[0x1f]=hit_chance;
    memcpy(row+0x33,"fixture",7);
    return row;
}

/* The 1C17E contact state: 1ED46 places party-cell monsters in 98B4.. */
static inline int fixture_contact(xeen_movement *m,const uint8_t *db)
{
    return xeen_scene_monsters(m,db,FIXTURE_DB_ROWS*60u)==XEEN_OK && m->ds[0x98b4]!=0xffu;
}
#endif
