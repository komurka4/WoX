/* damage_numbers (Phase 199 add-on, NOT original behavior): the queue the
 * original code's hooks fill (include/xeen/addon/damage_numbers.h). The
 * hooks only read DS; nothing here writes game state. */
#include <stdio.h>
#include <string.h>
#include "state.h"
#include "xeen/character_record.h"
#include "xeen/ds_globals.h"

int xeen_damage_numbers_on;
dn_entry dn_entries[DN_MAX];
uint64_t dn_seq;

/* The party records (B41Eh, 162h bytes each, six of them). */
#define DN_PARTY_BASE 0xb41eu
#define DN_PARTY_SIZE 0x162u
/**
 * Returns 1 and sets *MEMBER when RECORD is the DS offset of one of the six party records, else 0.
 */
static int member_of(uint16_t record,unsigned *member)
{
    if(record<DN_PARTY_BASE || (record-DN_PARTY_BASE)%DN_PARTY_SIZE)return 0;
    *member=(record-DN_PARTY_BASE)/DN_PARTY_SIZE;
    return *member<6u;
}
/** Reads the little-endian word at P. */
static uint16_t rd16(const uint8_t *p){return (uint16_t)(p[0]|p[1]<<8);}

/* The monster view slots 98B4h..98CDh and their draw records (12 bytes).
 * Indoor (F608 = 0): the records 1ED46 writes per slot (src/graphics/scene.c
 * monster_views). Outdoor (F608 != 0): 2126C writes the monster index to the
 * slot's word, the record's frame field (+2) (src/graphics/scene.c
 * assign_outdoor_slots, the slots' outdoor records in monster_views), so the
 * record starts 2 bytes before it. Both tables are this add-on's own copies of
 * the record starts; scene.c's table is not shared. */
static const uint16_t indoor_record[26]={
    0x10b2,0x106a,0x108e,0x0f92,0x0f7a,0x0f86,0x0e5a,0x0e42,0x0e4e,0x0caa,0x0c92,0x0c9e,0x0f62,
    0x0f6e,0x0e12,0x0e2a,0x0c4a,0x0c6e,0x0c7a,0x0c86,0x0e1e,0x0e36,0x0c32,0x0c56,0x0c3e,0x0c62};
static const uint16_t outdoor_record[26]={
    0x16ee,0x16a6,0x16ca,0x15ce,0x15b6,0x15c2,0x14ea,0x14d2,0x14de,0x13d6,0x13be,0x13ca,0x159e,
    0x15aa,0x14a2,0x14ba,0x1376,0x139a,0x13a6,0x13b2,0x14ae,0x14c6,0x135e,0x1382,0x136a,0x138e};
int dn_monster_record(const uint8_t *ds,unsigned index,uint8_t out[12])
{
    const uint16_t *table=rd16(ds+XEEN_DS_OUTDOORS)?outdoor_record:indoor_record;unsigned s;
    if(index>=0xffu)return 0;
    for(s=0;s<XEEN_VIEW_MONSTER_SLOTS;++s)if(ds[XEEN_DS_VIEW_MONSTER(s)]==index){
        memcpy(out,ds+table[s],12);
        return rd16(out)!=0 && rd16(out)!=0xffffu && rd16(out+2)!=0xffffu;
    }
    return 0;
}

/**
 * Records one damage, healing or miss note for a party member (PARTY 1) or a monster (PARTY 0). A
 * free slot is used, or the oldest note is replaced; the note gets the next sequence number.
 * Returns it.
 */
static dn_entry *add(int party,unsigned index,int32_t value,int miss)
{
    unsigned i,oldest=0;dn_entry *e=NULL;
    for(i=0;i<DN_MAX;++i){
        if(!dn_entries[i].used){e=&dn_entries[i];break;}
        if(dn_entries[i].seq<dn_entries[oldest].seq)oldest=i;
    }
    if(!e)e=&dn_entries[oldest];
    memset(e,0,sizeof *e);
    e->used=1;e->party=(uint8_t)party;e->index=(uint8_t)index;e->value=value;e->miss=(uint8_t)miss;e->seq=++dn_seq;
    return e;
}

/* A party member's HP changed by DELTA (negative: damage); RECORD is the
 * member's DS record (B41Eh + 162h * n). */
static void xeen_damage_numbers_hp(uint16_t record,int32_t delta)
{
    unsigned member;
    if(member_of(record,&member))(void)add(1,member,delta,0);
}
void xeen_damage_numbers_hp_set(const uint8_t *ds,uint16_t record,uint16_t value)
{
    int32_t delta;
    if(!ds)return;
    delta=(int32_t)(int16_t)value-(int32_t)(int16_t)rd16(ds+(uint16_t)(record+XEEN_CHARACTER_HP));
    if(delta)xeen_damage_numbers_hp(record,delta);
}
void xeen_damage_numbers_hp_restore(const uint8_t *ds,uint16_t record,uint16_t value)
{
    if(ds && (int16_t)value>(int16_t)rd16(ds+(uint16_t)(record+XEEN_CHARACTER_HP)))xeen_damage_numbers_hp_set(ds,record,value);
}
void xeen_damage_numbers_hp_ptr(const uint8_t *ds,const uint8_t *ch,int32_t delta)
{
    if(ds && ch>=ds && ch<ds+0x10000)xeen_damage_numbers_hp((uint16_t)(ch-ds),delta);
}
void xeen_damage_numbers_hp_ptr_restore(const uint8_t *ds,const uint8_t *ch,uint16_t value)
{
    if(ds && ch>=ds && ch<ds+0x10000)xeen_damage_numbers_hp_restore(ds,(uint16_t)(ch-ds),value);
}
void dn_event_hp(const uint8_t *ds,uint16_t record,uint16_t value,int restore)
{
    if(restore)xeen_damage_numbers_hp_restore(ds,record,value);
    else xeen_damage_numbers_hp_set(ds,record,value);
}
void xeen_damage_numbers_party_miss(unsigned member)
{
    if(member<6u)(void)add(1,member,0,1);
}
/**
 * Records a change of DELTA hit points (or a miss) of monster INDEX, with a copy of its view record
 * so the number can follow it. A zero-damage report right after the same turn's miss is dropped: it
 * is part of that miss.
 */
static void monster(const uint8_t *ds,unsigned index,int32_t delta,int miss)
{
    dn_entry *e;unsigned i;
    if(!ds || index>=0xffu)return;
    /* A melee turn whose swings all missed reports the miss, then 36738's
     * zero-damage path: the zero is part of that miss. */
    if(!miss && !delta)
        for(i=0;i<DN_MAX;++i){const dn_entry *o=&dn_entries[i];
            if(o->used && !o->stamped && !o->party && o->index==index && o->miss && o->seq==dn_seq)return;}
    e=add(0,index,delta,miss);
    e->have_record=(uint8_t)dn_monster_record(ds,index,e->record);
}
void xeen_damage_numbers_monster(const uint8_t *ds,unsigned index,int32_t delta){monster(ds,index,delta,0);}
void xeen_damage_numbers_monster_miss(const uint8_t *ds,unsigned index){monster(ds,index,0,1);}
