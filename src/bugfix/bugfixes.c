/* Owner-approved bug fixes (Phase202; NOT original behavior): the shared
 * registry; see include/xeen/bugfixes.h. Each bug fix lives in its own
 * directory below and keeps its own enabled flag. */
#include "xeen/bugfixes.h"
#include <string.h>

int xeen_bugfixes_enable(const char *name)
{
    if(!name)return 0;
    if(!strcmp(name,"spell_description")){xeen_bugfix_spell_description_on=1;return 1;}
    if(!strcmp(name,"left_clip_colors")){xeen_bugfix_left_clip_colors_on=1;return 1;}
    if(!strcmp(name,"sprite_width_trim")){xeen_bugfix_sprite_width_trim_on=1;return 1;}
    if(!strcmp(name,"quest_up_down")){xeen_bugfix_quest_up_down_on=1;return 1;}
    return 0;
}
