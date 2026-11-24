/* Add-on identify (owner-approved, not original behavior): the known item
 * types and their part of the per-slot file ADDON%02d.WOX (the base block,
 * include/xeen/addon/slot_file.h); see include/xeen/addon/identify.h. */
#include "xeen/addons.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char magic[8]={'W','O','X','A','D','D','O','N'};

void xeen_identify_free(xeen_addons *a)
{
    if(!a)return;
    free(a->known);a->known=NULL;a->count=a->capacity=0;
}
void xeen_addons_clear_items(xeen_addons *a){if(a)a->count=0;}

/* The type key: category, material, id and the weapon's power (1..6; other
 * state values are not part of what Identify shows). The broken (80h) and
 * cursed (40h) flags are not part of it (owner's choice, 2026-11-10): 3C96E
 * never reads them, so a flagged item's Identify statistics are its clean
 * type's, and an identified item stays known when it breaks or is cursed.
 * Keys written with those bits (files from before) fold into the clean key
 * on load, through xeen_addons_item_learn. */
static uint32_t item_key(unsigned category,const uint8_t *row)
{
    unsigned power=category==0u?(row[2]&0x3fu):0u;
    if(power>6u)power=0u;
    return (uint32_t)(category&3u)<<24|(uint32_t)row[0]<<16|(uint32_t)row[1]<<8|power;
}
/** Returns the index of the first known-item key not below KEY in the sorted table. */
static size_t lower_bound(const xeen_addons *a,uint32_t key)
{
    size_t lo=0,hi=a->count;
    while(lo<hi){size_t mid=lo+(hi-lo)/2u;if(a->known[mid]<key)lo=mid+1u;else hi=mid;}
    return lo;
}
int xeen_addons_item_known(const xeen_addons *a,unsigned category,const uint8_t *row)
{
    uint32_t key;size_t at;
    if(!a || !a->item_stats || category>2u || !row || !row[1])return 0;
    key=item_key(category,row);at=lower_bound(a,key);
    return at<a->count && a->known[at]==key;
}
int xeen_addons_item_learn(xeen_addons *a,unsigned category,const uint8_t *row)
{
    uint32_t key;size_t at;
    if(!a || !a->item_stats || category>2u || !row || !row[1])return 0;
    key=item_key(category,row);at=lower_bound(a,key);
    if(at<a->count && a->known[at]==key)return 1;
    if(a->count==a->capacity){
        size_t cap=a->capacity?a->capacity*2u:64u;uint32_t *k=realloc(a->known,cap*sizeof *k);
        if(!k)return 0;
        a->known=k;a->capacity=cap;
    }
    memmove(a->known+at+1u,a->known+at,(a->count-at)*sizeof *a->known);
    a->known[at]=key;++a->count;
    return 1;
}

/** Writes V to P as a little-endian dword. */
static void put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
/** Reads the little-endian dword at P. */
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}

uint8_t *xeen_addons_serialize(const xeen_addons *a,size_t *size)
{
    size_t count=a?a->count:0u,n=16u+4u*count,i;uint8_t *b;
    if(!size || !(b=malloc(n)))return NULL;
    memcpy(b,magic,8);put32(b+8,1u);put32(b+12,(uint32_t)count);
    for(i=0;i<count;++i)put32(b+16u+4u*i,a->known[i]);
    *size=n;return b;
}
int xeen_addons_deserialize(xeen_addons *a,const uint8_t *b,size_t n)
{
    uint32_t count,i;
    if(!a)return 0;
    a->count=0;
    if(!b || n<16u || memcmp(b,magic,8) || get32(b+8)!=1u)return 0;
    count=get32(b+12);
    if(count>(n-16u)/4u)return 0;
    for(i=0;i<count;++i){
        uint32_t key=get32(b+16u+4u*i);uint8_t row[4]={(uint8_t)(key>>16),(uint8_t)(key>>8),(uint8_t)key,0};
        if(!xeen_addons_item_learn(a,key>>24,row)){a->count=0;return 0;}
    }
    return 1;
}
