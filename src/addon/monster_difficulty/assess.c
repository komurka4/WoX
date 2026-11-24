/* monster_difficulty (Phase 201 add-on, NOT original behavior): the tier of a
 * monster for the current party (include/xeen/addon/monster_difficulty.h).
 *
 * Expected values of the game's own formulas (src/combat/combat.c; nothing is
 * rolled, no game state is written):
 *   party melee  371E2: attacks = level / divisor[class] + 1; 38818: score =
 *                accuracy bonus + 3891F hit + level / divisor[class] + d20
 *                (a 20 rolls again and adds), hits at >= 10 + AC; damage =
 *                Might bonus + 3891F prepared (weapon material, dice, the
 *                Adventurer preference EDA7 = 0: +5 hit, x3);
 *                36738: physical resistance (+28h) unless a type 22h weapon.
 *   monster      38419: a typed attack (+1Dh) always hits; otherwise d20: 1
 *                misses, 20 hits and still makes the scored test (a second
 *                hit), else roll + accuracy/4 + 1d(accuracy) >= AC + 10;
 *                37B..: dice (+1Ah) x die (+1Ch), a typed attack's saves
 *                taken as x0.75.
 * Weapons only (owner choice): spells, missiles and slayer weapons are not
 * counted. The thresholds (owner-accepted prototype values) are below.
 *
 * Owner choice after the first test (Phase 201, option 3): the matchup at
 * full health, so a fight never changes the icon. Every party member counts
 * whatever its condition, with its maximum HP (2AAC2); the monster has its
 * database HP (+14h); temporary effects are left out: conditions and
 * temporary stat/AC bonuses (base values), the curse, the party's buff and
 * protection words (EDA3..EDA6, EFFC..F002) and the asleep bonus. The
 * Adventurer preference (EDA7) stays: it is fixed for the game. */
#include <string.h>
#include "assess.h"
#include "xeen/combat.h"
#include "xeen/monster_record.h"
#include "xeen/byte_order.h"
#include "xeen/character_record.h"
#include "xeen/ds_globals.h"
#include "xeen/item_tables.h"

/* Tier thresholds: the share of the party's HP lost while it kills the
 * monster (attrition). */
#define MD_TRIVIAL_ATTRITION 0.02   /* and dead within one round */
#define MD_EASY_ATTRITION    0.10
#define MD_MODERATE_ATTRITION 0.30
#define MD_HARD_ATTRITION    0.70
#define MD_SAVE_FACTOR       0.75   /* a typed attack's saving throws */
#define MD_IMMUNE_ROUNDS     999.0

static const uint8_t attack_divisor[10]={5,6,6,7,8,6,5,4,7,6};   /* 371E2 */
static const uint8_t hit_divisor[10]={1,2,2,3,4,2,2,1,3,2};      /* 38818 */

/** Returns the byte V as a signed value. */
static int s8(uint8_t v){return (int)(int8_t)v;}

/* 3891F for range 0 as an expectation: the hit bonus and the mean damage. */
static void weapon(const xeen_movement *m,const uint8_t *ch,int *hit,double *damage,int *bypass)
{
    unsigned i;double prepared=0;int h=0;
    *bypass=0;
    for(i=0;i<XEEN_ITEM_ROWS;++i){
        const uint8_t *item=ch+XEEN_CHARACTER_ITEM(XEEN_ITEMS_WEAPONS,i);
        unsigned mat=item[XEEN_ITEM_ROW_MATERIAL],id=item[XEEN_ITEM_ROW_ID],dice,die;
        if(item[XEEN_ITEM_ROW_EQUIPPED]!=XEEN_SLOT_WEAPON && item[XEEN_ITEM_ROW_EQUIPPED]!=XEEN_SLOT_TWO_HANDED)continue;
        /* an intact metal weapon's bonuses (summed here: an estimate; 3891F assigns the last one's) */
        if(!(item[XEEN_ITEM_ROW_STATE]&(XEEN_ITEM_CURSED|XEEN_ITEM_BROKEN)) && mat>=XEEN_ITEM_FIRST_METAL &&
           mat<XEEN_ITEM_FIRST_ATTRIBUTE){
            h+=s8(m->ds[XEEN_DS_METAL_TO_HIT_BY_MATERIAL+mat]);prepared+=s8(m->ds[XEEN_DS_METAL_DAMAGE_BY_MATERIAL+mat]);
        }
        dice=m->ds[XEEN_DS_WEAPON_DICE+id];die=m->ds[XEEN_DS_WEAPON_SIDES+id];
        if(die)prepared+=dice*(die+1u)/2.0;
        if(id==0x22u)*bypass=1;                          /* weapon 22h: no physical resistance (36738) */
    }
    if(prepared<1)prepared=0;
    if(!m->ds[XEEN_DS_GAME_PREFERENCE]){h+=5;prepared*=3;}   /* the Adventurer preference (0) */
    *hit=h;*damage=prepared;
}

int md_party_read(const xeen_movement *m,md_party *p)
{
    unsigned i,count;
    memset(p,0,sizeof *p);p->weakest=0x7fffffff;
    count=m->ds[0xed8c];                                 /* the party's records, in and out of combat */
    if(count>6u)count=6u;
    for(i=0;i<count;++i){
        const uint8_t *ch=m->ds+0xb41eu+i*0x162u;md_member *e;int hit;double dmg;
        if(ch[0x13]>9u)continue;
        e=&p->m[p->n++];
        e->cls=ch[0x13];e->level=(unsigned)(ch[0x23]+ch[0x24]);
        e->hp=xeen_combat_max_hp(m,ch);
        if(e->hp<1)e->hp=1;
        e->ac=xeen_combat_armor_class(m,ch,1);
        weapon(m,ch,&hit,&dmg,&e->bypass_resist);
        e->to_hit=xeen_combat_stat_bonus(m,xeen_combat_stat(m,ch,5u,1))+hit+(int)(e->level/hit_divisor[e->cls]);
        e->damage=xeen_combat_stat_bonus(m,xeen_combat_stat(m,ch,0u,1))+dmg;
        if(e->damage<0)e->damage=0;
        p->hp+=e->hp;if(e->hp<p->weakest)p->weakest=e->hp;
    }
    return p->n>0;
}

/* P(d20 + bonus, a 20 rolling again, >= need). */
static double exploding_at_least(int need,int depth)
{
    int r;double p=0;
    if(need<=1)return 1.0;
    if(depth>6)return 0.0;
    for(r=1;r<20;++r)if(r>=need)p+=1.0/20.0;
    return p+exploding_at_least(need-20,depth+1)/20.0;
}

/* 38419's expected hits on one member (0..2: a natural 20 can hit twice). */
static double monster_hits(const uint8_t *row,unsigned ac)
{
    unsigned acc=row[XEEN_MONSTER_TO_HIT],defense=ac+10u,roll,k;double p=0;
    if(row[XEEN_MONSTER_ATTACK_TYPE])return 1.0;                /* not physical: always hits */
    p=1.0/20.0;                                          /* the 20's own hit */
    if(!acc)return p;
    for(roll=2;roll<=20u;++roll){
        unsigned ok=0;
        for(k=1;k<=acc;++k)if(roll+acc/4u+k>=defense)++ok;
        p+=(double)ok/acc/20.0;
    }
    return p;
}

/* The tier by attrition (the share of the party's maximum HP the fight costs) alone: trivial only when the party also
 * wins in one round. md_tier's raises follow. */
static unsigned attrition_tier(double attrition,double rounds)
{
    if(attrition<MD_TRIVIAL_ATTRITION && rounds<=1.0)return 0u;
    if(attrition<MD_EASY_ATTRITION)return 1u;
    if(attrition<MD_MODERATE_ATTRITION)return 2u;
    if(attrition<MD_HARD_ATTRITION)return 3u;
    return 4u;
}
unsigned md_tier(const xeen_movement *m,const md_party *p,const uint8_t *row,unsigned index)
{
    double party_dpr=0,monster_dpr=0,mean,rounds,attrition,hp;
    unsigned i,tier,type=row[XEEN_MONSTER_ATTACK_TYPE],special=row[XEEN_MONSTER_SPECIAL],
             dice=xeen_get_le16(row+XEEN_MONSTER_DICE),die=row[XEEN_MONSTER_DIE];
    (void)m;(void)index;
    hp=xeen_get_le16(row+XEEN_MONSTER_HP);if(hp<1)hp=1;                  /* full health (owner option 3) */
    /* the party's weapons */
    for(i=0;i<p->n;++i){
        const md_member *e=&p->m[i];
        int need=10+row[XEEN_MONSTER_ARMOR]-e->to_hit;
        double dmg=e->damage;
        unsigned resist=row[XEEN_MONSTER_PHYSICAL_RESISTANCE];
        if(!e->bypass_resist && resist)dmg=resist>=100u?0.0:dmg*(100u-resist)/100.0;
        party_dpr+=(double)(e->level/attack_divisor[e->cls]+1u)*exploding_at_least(need,0)*dmg;
    }
    rounds=party_dpr>0?hp/party_dpr:MD_IMMUNE_ROUNDS;
    /* the monster's attacks on a random able member */
    mean=dice*(die+1u)/2.0;
    if(type)mean*=MD_SAVE_FACTOR;
    for(i=0;i<p->n;++i)monster_dpr+=monster_hits(row,p->m[i].ac);
    monster_dpr=monster_dpr/p->n*row[XEEN_MONSTER_ATTACKS]*mean;
    attrition=monster_dpr*(rounds>1.0?rounds:1.0)/p->hp;
    tier=attrition_tier(attrition,rounds);
    if(party_dpr<=0 && tier<3u)tier=3u;                                  /* immune to weapons */
    if(monster_dpr>=p->weakest){if(tier<3u)tier=3u;}                     /* a round downs the weakest */
    else if((double)dice*die>=p->weakest && tier<2u)tier=2u;             /* one maximum hit could */
    if((special==XEEN_SPECIAL_PARALYSIS || special==XEEN_SPECIAL_UNCONSCIOUS || special==XEEN_SPECIAL_ERADICATE ||
        special==XEEN_SPECIAL_DEAD || special==XEEN_SPECIAL_STONE) && tier<3u)tier=3u;
    if(monster_dpr*2.0>=p->hp)tier=4u;                                   /* two rounds could wipe the party */
    return tier;
}
