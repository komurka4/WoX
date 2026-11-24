/* Owner-approved add-ons (not original behavior): the shared registry; see
 * include/xeen/addons.h. Each add-on lives in its own directory below. */
#include "xeen/addons.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int xeen_addons_enable(xeen_addons *a,const char *name)
{
    if(!a || !name)return 0;
    if(!strcmp(name,"identify")){a->item_stats=1;return 1;}
    if(!strcmp(name,"map_movement")){a->map_movement=1;return 1;}
    if(!strcmp(name,"training_preview")){a->training_preview=1;return 1;}
    if(!strcmp(name,"damage_numbers")){a->damage_numbers=1;return 1;}
    if(!strcmp(name,"damage_numbers.stack")){a->damage_numbers_stack=1;return 1;}
    if(!strcmp(name,"monster_difficulty")){a->monster_difficulty=1;return 1;}
    if(!strcmp(name,"spell_short_description")){a->spell_short_description=1;return 1;}
    if(!strcmp(name,"mouse_scroll")){a->mouse_scroll=1;return 1;}
    if(!strcmp(name,"town_icons")){a->town_icons=1;return 1;}
    if(!strcmp(name,"character_details")){a->character_details=1;return 1;}
    if(!strcmp(name,"spell_list_sort")){a->spell_list_sort=1;return 1;}
    if(!strcmp(name,"world_map")){a->world_map=1;return 1;}
    if(!strcmp(name,"quest_journal")){a->quest_journal=1;return 1;}
    if(!strcmp(name,"please_wait_skip")){a->please_wait_skip=1;return 1;}
    if(!strcmp(name,"wizard_eye_plus")){a->wizard_eye_plus=1;return 1;}
    return 0;
}
void xeen_addons_free(xeen_addons *a)
{
    if(!a)return;
    xeen_identify_free(a);
}
