#include "xeen/resources.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(e) do { if(!(e)) { fprintf(stderr,"line %d: %s\n",__LINE__,#e); exit(1); } } while(0)
static void put(uint8_t *p,unsigned v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void entry(uint8_t *p,unsigned i,uint16_t id,unsigned off,unsigned size) {
    uint8_t b[8]={0}; unsigned j; put(b,id); b[2]=(uint8_t)off; b[3]=(uint8_t)(off>>8); b[4]=(uint8_t)(off>>16); put(b+5,size);
    for(j=0;j<8;j++) p[2+8*i+j]=xeen_directory_encode(b[j],8*i+j);
}
static uint16_t hash(const char *s) { return xeen_resource_name_id((const uint8_t *)s,strlen(s)); }
static int sound(void *u,xeen_movement *m,xeen_service s,uint16_t a,uint16_t b,uint16_t *r) { (void)u;(void)m;(void)s;(void)a;(void)b;*r=0;return 1; }
static void synthetic(void) {
    uint8_t data[2+3*8+3*0x37c]={0}, output[0x37c], before[65536];
    xeen_archive a; xeen_resource_info r; xeen_navigation_cache c,old;
    xeen_patch_inputs p={0,0}; xeen_movement m; unsigned i,j; size_t n;
    CHECK(hash("maze0001.dat")==0x4eb1 && hash("maze0005.dat")==0x4ef1 && hash("maze0033.dat")==0xaed1);
    CHECK(hash("MAZE0001.DAT")==hash("maze0001.dat")); CHECK(hash("mazex104.dat")==hash("mazex200.dat"));
    { const uint8_t hi[]={0xe1,0}, lo[]={'A',0}, nul[]={'a',0,'b'}; CHECK(xeen_resource_name_id(hi,2)==xeen_resource_name_id(lo,2)); CHECK(xeen_resource_name_id(nul,3)==hash("a")); }
    for(i=0;i<600;i++) for(j=0;j<256;j++) CHECK(xeen_directory_encode(xeen_directory_decode((uint8_t)j,i),i)==j);
    CHECK(xeen_archive_open(&a,data,1)==XR_TRUNCATED); put(data,3);
    for(i=0;i<3;i++) entry(data,i,hash(i==0?"maze0001.dat":i==1?"maze0050.dat":"maze0042.dat"),26+i*0x37c,0x37c);
    CHECK(xeen_archive_open(&a,data,25)==XR_TRUNCATED);
    CHECK(xeen_archive_open(&a,data,sizeof(data)-1)==XR_TRUNCATED);
    entry(data,0,hash("maze0001.dat"),1,0x37c); CHECK(xeen_archive_open(&a,data,sizeof(data))==XR_BAD_INDEX);
    entry(data,0,hash("maze0001.dat"),26,0x37c);
    put(data+26+0x304,50); put(data+26+0x306,42); put(data+26+0x30c,0x8000);
    memset(data+26+2*0x37c+0x200,0xff,256);
    CHECK(xeen_archive_open(&a,data,sizeof(data))==XR_OK);
    CHECK(xeen_archive_find(&a,hash("maze0001.dat"),&r)==XR_OK);
    CHECK(xeen_archive_extract(&a,XR_BANK_STATE,&r,output,1,&n)==XR_CAPACITY && n==0);
    CHECK(xeen_archive_extract(&a,XR_BANK_ASSETS_XOR35,&r,output,sizeof(output),&n)==XR_OK && output[0]==0x35 && n==0x37c);
    CHECK(xeen_map_decode(output,1,&c.records[0])==XR_BAD_MAP);
    CHECK(xeen_cache_prepare(&a,XR_BANK_STATE,1,p,&c)==XR_OK);
    CHECK(c.ids[0]==1 && c.ids[1]==0 && c.ids[2]==50 && c.ids[3]==42 && c.mode==0x8000);
    p.world=1; CHECK(xeen_cache_prepare(&a,XR_BANK_STATE,1,p,&c)==XR_OK);
    for(i=0x35c;i<0x37c;i++) CHECK(c.records[0].bytes[i]==255);
    p.world=0;p.flag19=1; CHECK(xeen_cache_prepare(&a,XR_BANK_STATE,1,p,&c)==XR_OK);
    for(i=0x200;i<0x300;i++) CHECK(c.records[3].bytes[i]==0xf8);
    xeen_movement_init(&m,NULL,NULL); memset(m.ds,0xa5,sizeof(m.ds));memcpy(before,m.ds,sizeof(before));
    CHECK(xeen_cache_commit(&c,&m)==XR_OK);
    for(i=0;i<65536;i++) if(!(i>=0xbdcc && i<0xdd28) && !(i>=0xf46a && i<0xf47c) && i!=0xf608 && i!=0xf609 && i!=0xed99) CHECK(m.ds[i]==before[i]);
    old=c; CHECK(xeen_cache_prepare(&a,XR_BANK_STATE,99,p,&c)==XR_NOT_FOUND && memcmp(&c,&old,sizeof(c))==0);
    /* Duplicate IDs retain first record while surfacing a count. */
    entry(data,1,hash("maze0001.dat"),26+0x37c,0x37c);
    CHECK(xeen_archive_open(&a,data,sizeof(data))==XR_OK && a.duplicate_ids==1);
    CHECK(xeen_archive_find(&a,hash("maze0001.dat"),&r)==XR_OK && r.offset==26);
    /* Duplicate nonzero slots, fallback diagonal, and full 16-bit IDs. */
    entry(data,0,hash("mazex300.dat"),26,0x37c); entry(data,1,hash("maze0002.dat"),26+0x37c,0x37c);
    memset(data+26,0,3*0x37c);put(data+26+0x302,2);put(data+26+0x304,2);
    put(data+26+0x37c+0x302,2);
    CHECK(xeen_archive_open(&a,data,sizeof(data))==XR_OK);
    CHECK(xeen_cache_prepare(&a,XR_BANK_STATE,300,p,&c)==XR_OK && c.ids[0]==300 && c.ids[1]==2 && c.ids[2]==2 && c.ids[5]==2);
    CHECK(xeen_cache_commit(&c,&m)==XR_OK && xeen_read16(&m,0xf46a)==300 && m.ds[0xed99]==44);
    { xeen_resource_loader l={&a,XR_BANK_STATE,0,XR_OK,NULL,NULL}; memcpy(before,m.ds,sizeof(before));CHECK(!xeen_resource_service(&l,&m,XEEN_LOAD_MAP,99,0,&r.id));CHECK(l.last_status==XR_NOT_FOUND && !memcmp(before,m.ds,sizeof(before))); }
    CHECK(xeen_cache_prepare(&a,XR_BANK_STATE,0,p,&c)==XR_OK);for(i=0;i<9;i++) CHECK(!c.ids[i]);
    puts("synthetic resource contracts passed");
}
static void real(const char *path) {
    FILE *f=fopen(path,"rb"); long length; uint8_t *data; xeen_archive a; xeen_navigation_cache c;
    xeen_movement m; xeen_patch_inputs p={0,0}; uint16_t ax; unsigned i; xeen_resource_info r;
    const unsigned ids[]={1,5,33},off[]={0x3d77,0x5dd9,0x16488};
    const uint16_t cache1[]={1,0,5,2,0,0,6,0,0}, cache5[]={5,0,9,6,1,0,10,2,0};
    xeen_resource_loader l;
    CHECK(f); CHECK(!fseek(f,0,SEEK_END));length=ftell(f);CHECK(length>0);rewind(f);data=malloc((size_t)length);CHECK(data);CHECK(fread(data,1,(size_t)length,f)==(size_t)length);fclose(f);
    CHECK(xeen_archive_open(&a,data,(size_t)length)==XR_OK && !a.duplicate_ids);
    for(i=0;i<a.count*8u;i++) CHECK(xeen_directory_encode(xeen_directory_decode(data[2+i],i),i)==data[2+i]);
    for(i=0;i<3;i++) { char name[32]; uint8_t bytes[0x37c];snprintf(name,sizeof(name),"maze%04u.dat",ids[i]);CHECK(xeen_archive_find(&a,hash(name),&r)==XR_OK && r.offset==off[i] && r.size==0x37c);CHECK(xeen_archive_extract(&a,XR_BANK_STATE,&r,bytes,sizeof(bytes),NULL)==XR_OK);CHECK(!memcmp(bytes,data+off[i],sizeof(bytes))); }
    CHECK(xeen_cache_prepare(&a,XR_BANK_STATE,1,p,&c)==XR_OK && !memcmp(c.ids,cache1,sizeof(cache1)) && c.mode==0x8000);
    CHECK(xeen_map_tile(&c.records[0],8,8)==5 && xeen_map_metadata(&c.records[0],8,8)==0x40);
    l.archive=&a;l.bank=XR_BANK_STATE;l.flag19=0;l.last_status=XR_OK;l.fallback=sound;l.fallback_user=NULL;
    xeen_movement_init(&m,xeen_resource_service,&l);CHECK(xeen_cache_commit(&c,&m)==XR_OK);
    m.ds[0xed96]=1;m.ds[0xed97]=15;m.ds[0xed98]=0;
    CHECK(xeen_navigation_query(&m,7,&ax)==XEEN_OK && xeen_read16(&m,0xf490)==5 && m.ds[0xed99]==1 && m.ds[0xed97]==15);
    m.ds[0xed97]=16;CHECK(xeen_boundary_commit(&m)==XEEN_OK && m.ds[0xed99]==5 && m.ds[0xed97]==0);
    for(i=0;i<9;i++) CHECK(xeen_read16(&m,(uint16_t)(0xf46a+2*i))==cache5[i]);
    m.ds[0xed96]=3; CHECK(xeen_navigation_query(&m,7,&ax)==XEEN_OK && m.ds[0xed99]==5 && m.ds[0xed97]==0);
    m.ds[0xed97]=255;CHECK(xeen_boundary_commit(&m)==XEEN_OK && m.ds[0xed99]==1 && m.ds[0xed97]==15);
    CHECK(xeen_cache_prepare(&a,XR_BANK_STATE,33,p,&c)==XR_OK && !c.mode); CHECK(xeen_cache_commit(&c,&m)==XR_OK);
    CHECK(xeen_map_threshold(&c.records[0])==7);m.ds[0xed96]=1;m.ds[0xed97]=1;m.ds[0xed98]=3;
    CHECK(xeen_navigation_query(&m,2,&ax)==XEEN_OK && ax==0);CHECK(xeen_navigation_query(&m,7,&ax)==XEEN_OK && xeen_read16(&m,0xf490)==1);
    CHECK(xeen_translation_gate(&m,0xf2,&ax)==XEEN_OK && ax==1);
    m.ds[0xed97]=15;CHECK(xeen_navigation_query(&m,7,&ax)==XEEN_OK && ax==0x8888 && m.ds[0xed99]==33);
    free(data);puts("DARK.CUR resource/navigation contracts passed");
}
int main(int argc,char **argv) { synthetic();if(argc>1) real(argv[1]);return 0; }
