/* Owner-approved add-ons (not original behavior): the shared ADDONnn.WOX
 * layout; see include/xeen/addon/slot_file.h. */
#include "xeen/addon/slot_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Reads the little-endian dword at P. */
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
/** Writes V to P as a little-endian dword. */
static void put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}

void xeen_addons_slot_name(int slot,char out[16]){(void)snprintf(out,16,"ADDON%02d.WOX",slot);}

/* The base block's size (16 + 4 x count); 0 for a malformed file. */
static size_t xeen_addon_file_base(const uint8_t *b,size_t n)
{
    uint32_t count;
    if(!b || n<16u || memcmp(b,"WOXADDON",8) || get32(b+8)!=1u)return 0;
    count=get32(b+12);
    if(count>(n-16u)/4u)return 0;
    return 16u+4u*(size_t)count;
}
/* Walks the sections after the base; 0 when they do not end exactly at n. */
static int sections_ok(const uint8_t *b,size_t n,size_t at)
{
    while(at<n){
        uint32_t size;
        if(n-at<8u)return 0;
        size=get32(b+at+4);
        if(size>n-at-8u)return 0;
        at+=8u+size;
    }
    return 1;
}
int xeen_addon_file_section(const uint8_t *b,size_t n,const char tag[4],const uint8_t **data,size_t *size)
{
    size_t at=xeen_addon_file_base(b,n);
    if(!at || !tag || !sections_ok(b,n,at))return 0;
    while(at<n){
        uint32_t s=get32(b+at+4);
        if(!memcmp(b+at,tag,4)){if(data)*data=b+at+8u;if(size)*size=s;return 1;}
        at+=8u+s;
    }
    return 0;
}
uint8_t *xeen_addon_file_compose(const uint8_t *old,size_t old_size,const uint8_t *base,size_t base_size,
    const char *tag,const uint8_t *payload,size_t payload_size,size_t *size)
{
    static const uint8_t empty[16]={'W','O','X','A','D','D','O','N',1,0,0,0,0,0,0,0};
    size_t old_base=xeen_addon_file_base(old,old_size),n,at;uint8_t *b,*p;
    if(!size)return NULL;
    if(old_base && !sections_ok(old,old_size,old_base))old_base=0;   /* malformed: nothing kept */
    if(!base){
        if(old_base){base=old;base_size=old_base;}
        else {base=empty;base_size=sizeof empty;}
    }
    n=base_size+(old_base?old_size-old_base:0u)+(tag && payload_size?8u+payload_size:0u);
    if(!(b=malloc(n?n:1u)))return NULL;
    memcpy(b,base,base_size);p=b+base_size;
    for(at=old_base;old_base && at<old_size;){
        uint32_t s=get32(old+at+4);
        if(!tag || memcmp(old+at,tag,4)){memcpy(p,old+at,8u+s);p+=8u+s;}
        at+=8u+s;
    }
    if(tag && payload_size){memcpy(p,tag,4);put32(p+4,(uint32_t)payload_size);memcpy(p+8,payload,payload_size);p+=8u+payload_size;}
    *size=(size_t)(p-b);return b;
}
