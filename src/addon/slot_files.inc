/* Included by src/game/game.c (the game's translation unit). */
/* Owner-approved add-ons (NOT original behavior): the save slot's
 * ADDON%02d.WOX (include/xeen/addon/slot_file.h), through the same host file
 * service as the slot files, for the add-ons that keep something per slot:
 * identify (the known item types: the file's base block) and, since Phase
 * 211, spell_list_sort (the characters' list setting: section "SLST").
 * Each add-on reads and writes only its own part; Save copies the part of an
 * add-on that is off from the slot's previous file unchanged. Nothing here
 * runs with both add-ons off. */
int addons_items(const xeen_game *g){return g->addons && g->addons->item_stats;}
/** Returns 1 when the spell_list_sort add-on is enabled. */
int addons_sort(const xeen_game *g){return g->addons && g->addons->spell_list_sort;}
/** Returns 1 when any add-on keeps per-save-slot data (identify or spell_list_sort). */
static int addons_slots(const xeen_game *g){return addons_items(g) || addons_sort(g);}
/**
 * Writes the add-ons' data for save SLOT into its ADDONnn.WOX file. Sections of add-ons that are
 * off are kept from the old file. No file is written when there is nothing to keep.
 */
void addons_slot_save(xeen_game *g,int slot)
{
    char name[16];size_t n=0,on=0,bn=0,sn=0;uint8_t *old=NULL,*base=NULL,*sort=NULL,*b;
    if(!addons_slots(g) || !g->save_files || !g->save_files->write)return;
    xeen_addons_slot_name(slot,name);
    if(g->save_files->read)old=g->save_files->read(g->save_files->user,name,&on);
    if(addons_items(g) && !(base=xeen_addons_serialize(g->addons,&bn))){free(old);return;}
    if(addons_sort(g))sort=xeen_sls_serialize(&g->addons->sort,&sn);   /* NULL: no character in Type */
    if(!base && !sort && !old)return;                                   /* nothing to keep and no file: none written */
    b=xeen_addon_file_compose(old,on,base,bn,addons_sort(g)?"SLST":NULL,sort,sn,&n);
    if(b && !g->save_files->write(g->save_files->user,name,b,n))fprintf(stderr,"add-ons: cannot write %s\n",name);
    free(b);free(sort);free(base);free(old);
}
/**
 * Clears the add-ons' per-game data, then loads it from save SLOT's ADDONnn.WOX file, if there is
 * one.
 */
void addons_slot_load(xeen_game *g,int slot)
{
    char name[16];size_t n=0;uint8_t *b;
    if(!addons_slots(g))return;
    if(addons_items(g))xeen_addons_clear_items(g->addons);
    if(addons_sort(g))xeen_sls_clear(&g->addons->sort);
    if(!g->save_files || !g->save_files->read)return;
    xeen_addons_slot_name(slot,name);
    if(!(b=g->save_files->read(g->save_files->user,name,&n)))return;   /* none: nothing known, every list A-Z */
    if(addons_items(g) && !xeen_addons_deserialize(g->addons,b,n))fprintf(stderr,"add-on identify: %s is malformed; ignored\n",name);
    if(addons_sort(g)){
        const uint8_t *s;size_t sn;
        if(xeen_addon_file_section(b,n,"SLST",&s,&sn) && !xeen_sls_deserialize(&g->addons->sort,s,sn))
            fprintf(stderr,"add-on spell_list_sort: %s's setting is malformed; ignored\n",name);
    }
    free(b);
}
/** Deletes save SLOT's ADDONnn.WOX file, if it exists. */
void addons_slot_remove(xeen_game *g,int slot)
{
    char name[16];size_t n=0;uint8_t *b;
    if(!addons_slots(g) || !g->save_files || !g->save_files->read || !g->save_files->remove)return;
    xeen_addons_slot_name(slot,name);
    if(!(b=g->save_files->read(g->save_files->user,name,&n)))return;
    free(b);(void)g->save_files->remove(g->save_files->user,name);
}
