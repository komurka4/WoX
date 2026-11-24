/* character_details (Phase 208 add-on, NOT original behavior): the texts of
 * the stat explanations and of the skill and condition windows. Every number
 * comes from the game's own formulas (xeen/combat.h, read-only) and DS
 * tables; the names are the game's own (DS 2F54 skills, 2EC0 conditions,
 * 286A classes, the party effects' 79E9..7A2D lines). The rules stated here
 * are the original code's, as reconstructed and verified in earlier phases
 * (docs/coverage/CHARACTERS.md); the Phase 208 report lists each source. */
#include <stdio.h>
#include <string.h>
#include "texts.h"
#include "xeen/character_record.h"
#include "xeen/ds_globals.h"

/** Reads the little-endian word at P. */
static uint16_t rd16(const uint8_t *p){return (uint16_t)(p[0]|p[1]<<8);}
/* A DS far-pointer table's string K (the offset word; the game's segment). */
static const char *far_text(const xeen_movement *m,uint16_t table,unsigned k)
{return (const char *)m->ds+rd16(m->ds+(uint16_t)(table+4u*k));}
/* NAME up to its first control character (the skill table's "Thievery\t100"). */
static void name_of(char *out,size_t n,const char *name)
{
    size_t i=0;
    while(i+1u<n && (unsigned char)name[i]>=0x20u){out[i]=name[i];++i;}
    out[i]=0;
}
/** Copies the character's name (at most 15 printable bytes of the record) to OUT as a C string. */
static void member_name(char *out,size_t n,const uint8_t *ch)
{
    size_t i=0;
    while(i+1u<n && i<15u && ch[i]>=0x20u){out[i]=(char)ch[i];++i;}
    out[i]=0;
}
/**
 * Copies the character's class name (the DS:286A names) to OUT, or "?" for a class byte above 9.
 */
static void class_name(char *out,size_t n,const xeen_movement *m,const uint8_t *ch)
{
    if(ch[XEEN_CHARACTER_CLASS]<10u)name_of(out,n,far_text(m,XEEN_DS_CLASS_NAMES,ch[XEEN_CHARACTER_CLASS]));else snprintf(out,n,"?");
}
/** Returns the English indefinite article for WORD: "An" before a capital vowel, else "A". */
static const char *article(const char *word)
{return strchr("AEIOU",word[0])?"An":"A";}
/* 2C92E's next step above V (DS 2DA8's thresholds, its 24th word 65535 ends
 * the table): the threshold and its bonus; 0 past the last step. */
static int next_step(const xeen_movement *m,unsigned v,unsigned *at,int *b)
{
    unsigned k;
    for(k=0;k<24u;++k){
        uint16_t t=rd16(m->ds+(uint16_t)(XEEN_DS_ATTRIBUTE_BONUS_THRESHOLD+2u*k));
        if(t>v){
            if(t==0xffffu)return 0;
            *at=t;*b=xeen_combat_stat_bonus(m,t);return 1;
        }
    }
    return 0;
}
/* 2BAC7(record, 0): the age with its temporary years (+26h). */
static unsigned age_of(const xeen_movement *m,const uint8_t *ch)
{
    uint16_t years=(uint16_t)(rd16(m->ds+XEEN_DS_YEAR)-rd16(ch+XEEN_CHARACTER_BIRTH_YEAR));
    if((int16_t)years>254)years=254u;
    return (uint16_t)(years+ch[XEEN_CHARACTER_AGE_BONUS]);
}
/* 46470's use of Intellect (STAT 1) or Personality (2) for the member's
 * class: the stat's bonus counts per level in full (Sorcerer: Intellect,
 * Cleric: Personality), halved (Archer, Paladin: 46470 halves; Druid: the
 * mean of its two unhalved passes) or a quarter (Ranger: both halved, then
 * the mean); every other class does not use it. */
static void spell_points(char *out,size_t n,const xeen_movement *m,const uint8_t *ch,unsigned stat,int b)
{
    char cls[20];unsigned c=ch[XEEN_CHARACTER_CLASS];int share;
    class_name(cls,sizeof cls,m,ch);
    if(stat==1u)share=c==4u?4:c==2u||c==8u?2:c==9u?1:0;
    else share=c==3u?4:c==1u||c==8u?2:c==9u?1:0;
    if(!share)snprintf(out,n,"%s: not used.",cls);
    else if(share==4)snprintf(out,n,"%s: %+d.",cls,b);
    else snprintf(out,n,"%s: %s of %+d.",cls,share==2?"half":"1/4",b);
}

/* Stats 0..6 (Might .. Luck): the stat's bonus; TAIL gets the next step's
 * bonus and threshold ("" past the last step). Luck's bonus is that of
 * twice the stat (37A02), its threshold shown halved. */
static int attribute_bonus(const xeen_movement *m,const uint8_t *ch,unsigned stat,char *tail,size_t tn)
{
    unsigned v=xeen_combat_stat(m,ch,stat,0),at;int b=xeen_combat_stat_bonus(m,(uint16_t)v),nb;
    if(stat==6u){
        b=xeen_combat_stat_bonus(m,(uint16_t)(2u*v));
        if(next_step(m,2u*v,&at,&nb))snprintf(tail,tn,"\nNext bonus %+d at %u.",nb,(at+1u)/2u);
    }else if(next_step(m,v,&at,&nb))snprintf(tail,tn,"\nNext bonus %+d at %u.",nb,at);
    return b;
}
/* The page of stat 0..6: what its bonus does, then the next step. */
static void attribute_text(char *out,size_t n,const xeen_movement *m,const uint8_t *ch,unsigned stat)
{
    char tail[48]="",sp[48];int b=attribute_bonus(m,ch,stat,tail,sizeof tail);
    switch(stat){
    case 0:snprintf(out,n,"Melee hits %+d damage.\nBash uses it too.%s",b,tail);return;               /* 371E2, 3D7F3 */
    case 1:spell_points(sp,sizeof sp,m,ch,1,b);
        snprintf(out,n,"Spell points per level\nfor Sorcerers, Archers\n(Druids, Rangers: half).\n%s%s",sp,tail);return;
    case 2:spell_points(sp,sizeof sp,m,ch,2,b);
        snprintf(out,n,"Spell points per level\nfor Clerics, Paladins\n(Druids, Rangers: half).\n%s%s",sp,tail);return;
    case 3:snprintf(out,n,"Hit points %+d per level.%s",b,tail);return;                             /* 2AAC2 */
    case 4:snprintf(out,n,"Armor Class %+d.\nThe fastest act first.%s",b,tail);return;            /* 4660F, 38D25 */
    case 5:snprintf(out,n,"To-hit %+d for melee\nand missile attacks.%s",b,tail);return;           /* 38818 */
    default:snprintf(out,n,"Saving throws %+d\n(from twice the Luck):\nhalves elemental hits,\nresists specials.%s",b,tail);return;
    }
}
/* Stat 7, the age: 2BD94's age brackets. Might, Endurance, Speed, Accuracy
 * use the first row, Intellect and Personality the second, Luck none; 2FCAB
 * kills a member with a stat below 1. */
static void age_text(char *out,size_t n,const xeen_movement *m,const uint8_t *ch)
{
    static const uint16_t ranges[9]={1,6,11,18,36,51,76,101,201};
    static const int16_t body[10]={-250,-50,-20,-10,0,-2,-5,-10,-20,-50},mind[10]={-250,-50,-20,-10,0,2,5,10,20,50};
    char now[48];unsigned age=age_of(m,ch),k=0;
    while(k<9u && ranges[k]<=age)++k;
    if(k==4u)snprintf(now,sizeof now,"Now: no change.");
    else if(k<4u)snprintf(now,sizeof now,"Now: all %d.",body[k]);
    else snprintf(now,sizeof now,"Now: %d and %+d.",body[k],mind[k]);
    snprintf(out,n,"Age changes stats\n(not Luck): from 36\nMgt End Spd Acy -2,\nInt Per +2; more at\n51, 76, 101 and 201.\n"
        "A stat below 1 kills.\n%s",now);
}
/* Stat 8, the level: 38818's to-hit quotient (level / its class divisor),
 * 37A02's Luck save (+ level), 371E2's attacks (level / DS 2E01[class] + 1). */
static void level_text(char *out,size_t n,const xeen_movement *m,const uint8_t *ch)
{
    static const uint8_t hit[10]={1,2,2,3,4,2,2,1,3,2};   /* 38818's to-hit divisor by class (a code switch) */
    unsigned level=xeen_character_current_level(ch),
             c=ch[XEEN_CHARACTER_CLASS]<10u?ch[XEEN_CHARACTER_CLASS]:0u,
             div=m->ds[(uint16_t)(XEEN_DS_CLASS_ATTACK_DIVISOR+c)];
    if(!div)div=1u;
    snprintf(out,n,"HP and SP grow with\nevery level.\nTo-hit %+d (level / %u).\nLuck saves %+d.\n"
        "An attack more every\n%u levels: next at %u.",(int)(level/hit[c]),(unsigned)hit[c],(int)level,div,(level/div+1u)*div);
}
/* Stat 9, the armor class: 4660F's Speed bonus, 2BAFA(9) items, the
 * temporary (Bless EDA3 and +22h); 38419's hit test: AC + 10 (Block: AC +
 * level / 2 + 15), no test for an elemental attack or a sleeper. */
static void armor_text(char *out,size_t n,const xeen_movement *m,const uint8_t *ch)
{
    int speed=xeen_combat_stat_bonus(m,xeen_combat_stat(m,ch,XEEN_STAT_SPEED,0)),items=xeen_combat_item_bonus(m,ch,9u);
    int magic=(int)m->ds[XEEN_DS_EFFECT_BLESS]+(int)ch[XEEN_CHARACTER_AC_BONUS];
    snprintf(out,n,"A monster's physical\nhit needs AC + 10.\nBlock: AC + level/2 + 15.\nElements and sleepers\n"
        "ignore it.\nSpeed %+d, items %+d,\nmagic %+d.",speed,items,magic);
}
/* The lines are broken by hand: each fits the 143-pixel popup (127 pixels
 * as 4E38C's wrap measures them, a space 4) with the widest numbers.
 * Returns 0 (OUT untouched) for a stat without a page. */
int cd_stat_text(char *out,size_t n,const xeen_movement *m,uint16_t record,unsigned stat)
{
    const uint8_t *ch=m->ds+record;
    if(stat<7u){attribute_text(out,n,m,ch,stat);return 1;}
    switch(stat){
    case 7:age_text(out,n,m,ch);return 1;
    case 8:level_text(out,n,m,ch);return 1;
    case 9:armor_text(out,n,m,ch);return 1;
    default:return 0;
    }
}

/* ------------------------------------------------------------- skills */
static int window_text(char *out,size_t n,const char *title,const char *rule,const char *party,const char *personal,unsigned colour)
{
    int w=snprintf(out,n,"\002\003c%s\003l\n%s",title,rule);
    if(w<0 || (size_t)w>=n)return 0;
    if(party && *party){int k=snprintf(out+w,n-(size_t)w,"\n\014%02u%s",CD_RULE_BLUE,party);if(k<0 || (size_t)(w+k)>=n)return 0;w+=k;}
    if(personal && *personal){int k=snprintf(out+w,n-(size_t)w,"\n\014%02u%s",CD_PERSONAL,personal);if(k<0 || (size_t)(w+k)>=n)return 0;w+=k;}
    {int k=snprintf(out+w,n-(size_t)w,"\014%02u",colour<40u?colour:0u);if(k<0 || (size_t)(w+k)>=n)return 0;}
    return 1;
}
#define ONE "Party: one member is enough."
/* A spell-point skill's personal line: +2 spell points per level for DOUBLE_CLASS, +1 for SINGLE_CLASS, no effect for
 * any other class (CLS its name, CLASS_ID the member's class byte). */
static void spell_point_text(char *personal,size_t n,const char *cls,unsigned class_id,unsigned double_class,unsigned single_class)
{
    if(class_id==double_class)snprintf(personal,n,"%s: +2 spell points per level.",cls);
    else if(class_id==single_class)snprintf(personal,n,"%s: +1 spell point per level.",cls);
    else snprintf(personal,n,"No effect for %s %s.",article(cls)[1]?"an":"a",cls);
}
int cd_skill_text(char *out,size_t n,const xeen_movement *m,uint16_t record,unsigned skill,unsigned colour)
{
    const uint8_t *ch=m->ds+record;char title[24],who[16],cls[20],personal[120]="";const char *rule="",*party="";
    if(skill>17u)return 0;
    name_of(title,sizeof title,far_text(m,XEEN_DS_SKILL_NAMES,skill));
    member_name(who,sizeof who,ch);class_name(cls,sizeof cls,m,ch);
    switch(skill){
    case 0:{
        /* 3CF5B: level x 2, Ninja +15, Robber +30, Elf/Gnome +10, Dwarf +5,
         * Half-Orc -10, 2BAFA(10) items; 170D4 doors, 2DECE chests. */
        unsigned level=xeen_character_current_level(ch);int items=xeen_combat_item_bonus(m,ch,10u);char extra[48]="";size_t e=0;
        unsigned c=ch[XEEN_CHARACTER_CLASS],race=ch[XEEN_CHARACTER_RACE];
        if(c==XEEN_CLASS_NINJA)e+=(size_t)snprintf(extra+e,sizeof extra-e," +15");
        if(c==XEEN_CLASS_ROBBER)e+=(size_t)snprintf(extra+e,sizeof extra-e," +30");
        if(race==XEEN_RACE_ELF || race==XEEN_RACE_GNOME)e+=(size_t)snprintf(extra+e,sizeof extra-e," +10");
        if(race==XEEN_RACE_DWARF)e+=(size_t)snprintf(extra+e,sizeof extra-e," +5");
        if(race==XEEN_RACE_HALF_ORC)e+=(size_t)snprintf(extra+e,sizeof extra-e," -10");
        if(items)(void)snprintf(extra+e,sizeof extra-e," %+d",items);
        rule="Opens locked doors (Space) and chests: Thievery + 1..20 must reach the lock's level. "
             "A trap may go off first (1 in 4). Success gives experience.";
        party="Level x2, Robber +30, Ninja +15, Elf/Gnome +10, Dwarf +5, Half-Orc -10, items.";
        snprintf(personal,sizeof personal,"%s: %u x2%s = %d.",who,level,extra,(int)xeen_combat_thievery(m,ch));
        break;}
    case 1:
        rule="Trainers sell it and heroes brag about it, but nothing in the world ever asks for it.";
        snprintf(personal,sizeof personal,"Effect: a confident swagger.");break;
    case 2:
        rule="Spell points of Druids (+2 per level) and Rangers (+1 per level).";
        spell_point_text(personal,sizeof personal,cls,ch[XEEN_CHARACTER_CLASS],XEEN_CLASS_DRUID,XEEN_CLASS_RANGER);
        break;
    case 3:rule="Hit points +1 per level.";break;
    case 4:rule="Each square the party walks on is marked on the map (M) and on the mini-map.";party=ONE;break;
    case 5:rule="Only Crusaders may enter Castle Burlock. Some offerings in the sky above the Clouds require it too.";party="Clouds of Xeen only.";break;
    case 6:rule="Shows the facing (N, E, S or W) in the emblem under the view.";party=ONE;break;
    case 7:rule="Reads strange runes, statue messages, hieroglyphics and foreign writing. Without it: "
               "\"You are unable to understand the strange runes.\"";party="Each reader needs it.";break;
    case 8:rule="Shops pay the full value for items you sell; without it they pay half. Buying costs the same.";
        party="The seller's own skill counts.";break;
    case 9:rule="Lets the party climb into mountains outdoors.";party="Party: two members need it.";break;
    /* The original charges 170 minutes (desert_surface, 172CD 30CF3(170)); owner-approved wording (Phase 327). */
    case 10:rule="Outdoors, each step on desert costs 170 minutes more without a Navigator.";party=ONE;break;
    case 11:rule="Lets the party walk into forests outdoors.";party="Party: two members need it.";break;
    case 12:
        rule="Spell points of Clerics (+2 per level) and Paladins (+1 per level).";
        spell_point_text(personal,sizeof personal,cls,ch[XEEN_CHARACTER_CLASS],XEEN_CLASS_CLERIC,XEEN_CLASS_PALADIN);
        break;
    case 13:
        rule="Spell points of Sorcerers (+2 per level) and Archers (+1 per level).";
        spell_point_text(personal,sizeof personal,cls,ch[XEEN_CHARACTER_CLASS],XEEN_CLASS_SORCERER,XEEN_CLASS_ARCHER);
        break;
    case 14:rule="Lets the party cross water (or Walk on Water).";party="Party: every member needs it.";break;
    case 15:
        rule="Finds the tracks of every beast in the land and keeps them all to itself. Nothing ever asks for them.";
        snprintf(personal,sizeof personal,"Effect: none. Lovely tracks, though.");break;
    case 16:rule="The gargoyle at the right of the view moves when the wall ahead hides a secret.";party=ONE;break;
    default:rule="The bat above the view moves when monsters are within 3 squares.";party=ONE;break;
    }
    return window_text(out,n,title,rule,party,personal,colour);
}

/* --------------------------------------------------------- conditions */
/* 2A2E8 (condition_mod): per condition 0..7 the stats it lowers and raises
 * by its value (Mgt Int Per End Spd Acy Lck); nothing while dead, stoned or
 * eradicated. */
static const char *const lowers[8]={"Lck","Mgt Int Per End Spd Acy Lck","Mgt Int Per End Spd Acy Lck","Mgt Spd Acy",
    "Int Per End","Int Per Acy","","Mgt Int End Spd Acy"};
static const char *const raises[8]={"","","","","","Mgt Spd","Mgt Int Per End Spd Acy Lck","Per Lck"};
#define LEFT " If the party runs from combat, this member is left behind and dies."
#define DAWN "Ends with Rest or at dawn."
int cd_condition_text(char *out,size_t n,const xeen_movement *m,uint16_t record,unsigned id,unsigned colour)
{
    const uint8_t *ch=m->ds+record;char title[24],rule[240],personal[120]="";const char *when="";unsigned v;
    if(id>=20u && id<=23u){
        /* The party effects' popup lines (DS 79E9, 79FD, 7A16, 7A2D:
         * "\n\t020NAME\t095%+d", the name at +5) give the names. */
        static const uint16_t lines[4]={XEEN_TEXT_DETAILS_BLESSED,XEEN_TEXT_DETAILS_POWER_SHIELD,XEEN_TEXT_DETAILS_HOLY_BONUS,XEEN_TEXT_DETAILS_HEROISM};
        name_of(title,sizeof title,(const char *)m->ds+lines[id-20u]+5u);
        v=m->ds[(uint16_t)(0xeda3u+(id-20u))];
        switch(id){
        case 20:snprintf(rule,sizeof rule,"Whole party: Armor Class %+d.",(int)v);break;            /* 4660F */
        case 21:snprintf(rule,sizeof rule,"Whole party: each monster hit does %u less damage.",v);break;   /* 37B6D */
        case 22:snprintf(rule,sizeof rule,"Whole party: weapon damage %+d per hit.",(int)v);break; /* 3891F */
        default:snprintf(rule,sizeof rule,"Whole party: to-hit %+d.",(int)v);break;               /* 3891F */
        }
        return window_text(out,n,title,rule,DAWN,"",colour);                /* 29168, Rest */
    }
    if(id>15u)return 0;
    name_of(title,sizeof title,far_text(m,XEEN_DS_CONDITION_NAMES,id));
    v=ch[0x143u+id];
    switch(id){
    case 0:snprintf(rule,sizeof rule,"Lowers Luck and the chance to hit by its value.");when="Lasts until a temple removes the curse.";break;
    case 1:snprintf(rule,sizeof rule,"Lowers all seven stats by its value.");when="Grows by 1 every 8 hours; past 10 it turns into Depressed.";break;
    case 2:snprintf(rule,sizeof rule,"Lowers all seven stats by its value.");when="Ends at the next 8-hour mark, by Revitalize or at a temple.";break;
    case 3:snprintf(rule,sizeof rule,"Lowers Might, Speed and Accuracy by its value.");
        when="Every 8 hours 1 in 10 (and a save) cures it, else it doubles. Cure Poison or a temple.";break;
    case 4:snprintf(rule,sizeof rule,"Lowers Endurance, Personality and Intellect by its value.");
        when="Every 8 hours 1 in 10 (and a save) cures it, else it doubles. Cure Disease or a temple.";break;
    case 5:snprintf(rule,sizeof rule,"Lowers Intellect, Personality and Accuracy; raises Might and Speed by its value.");
        when="Grows by 1 every 8 hours. A temple heals it.";break;
    case 6:snprintf(rule,sizeof rule,"Raises all seven stats by its value.");when="Grows by 1 every 8 hours; past 10 it turns into Heart Broken.";break;
    case 7:snprintf(rule,sizeof rule,"Raises Personality and Luck; lowers Might, Intellect, Endurance, Speed and Accuracy.");
        when="At the next 8-hour mark it becomes Weak (the hangover).";break;
    case 8:snprintf(rule,sizeof rule,"Cannot act. A monster's hit wakes the sleeper." LEFT);when="Ends at the next 8-hour mark, by Awaken or by Rest.";break;
    case 9:snprintf(rule,sizeof rule,"No effect on the stats.");when="Counts 1, 2, 3 and ends: one step every 8 hours.";break;
    case 10:snprintf(rule,sizeof rule,"No effect on the stats.");when="Wears off with time (a Luck saving throw can end it); gone at the next 8-hour mark.";break;
    case 11:snprintf(rule,sizeof rule,"Cannot act." LEFT);when="Wears off with time; gone at the next 8-hour mark. Cure Paralysis.";break;
    case 12:snprintf(rule,sizeof rule,"Cannot act. HP are 0 or less; at minus the maximum HP the character dies." LEFT);when="Any healing or Rest.";break;
    case 13:snprintf(rule,sizeof rule,"Cannot act and cannot be healed. Rest does not help.");when="Raise Dead, Divine Intervention or a temple.";break;
    case 14:snprintf(rule,sizeof rule,"Cannot act and cannot be healed." LEFT);when="Stone to Flesh, Divine Intervention or a temple.";break;
    default:snprintf(rule,sizeof rule,"Cannot act and cannot be healed." LEFT);when="Resurrect or a temple.";break;
    }
    if(id<8u && v){
        char low[48]="",high[48]="";
        if(*lowers[id])snprintf(low,sizeof low,"%s -%u",lowers[id],v);
        if(*raises[id])snprintf(high,sizeof high,"%s +%u",raises[id],v);
        if(id==0u)snprintf(personal,sizeof personal,"Now: Lck -%u, to-hit -%u.",v,v);   /* 38818: score - cursed */
        else snprintf(personal,sizeof personal,"Now: %s%s%s.",low,*low && *high?", ":"",high);
    }
    return window_text(out,n,title,rule,when,personal,colour);
}
