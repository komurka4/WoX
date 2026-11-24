/* Bug fix spell_description (Phase202; owner-approved, NOT original
 * behavior, off by default): see include/xeen/bugfix/spell_description.h.
 * Each entry names the original code that the corrected text follows (the
 * analysis placement of the Phase 15 image); the original texts are
 * spldesc.bin's (XEEN.CC and DARK.CC hold the same file). */
#include "xeen/bugfix/spell_description.h"
#include <string.h>

int xeen_bugfix_spell_description_on;

#define RED   "\f" "32"   /* a permanent downside */
#define GREEN "\f" "16"   /* a benefit the original text overstates */
#define PLAIN "\f" "d"    /* the window's own colour (00) */

static const struct {unsigned char spell;const char *text;} fixes[]={
    /* Beast Master 3B733, 37621 branch 377A0: monster types 1 and 3 only. */
    {2,"Hypnotizes a group of animals or humanoids into stillness until they overcome the spell.  "
       "Other monsters are unaffected."},
    /* Bless 3A1F5: the party byte EDA3 (every member's AC). */
    {3,"Improves the armor class of every party member by 1 per level of the caster."},
    /* Create Food 3AF82: EFF6 += the party size ED8C, living or not. */
    {6,"Creates one unit of food for each party member."},
    /* Day of Protection 3B764: also EDA2 (Walk on Water). */
    {12,"Simultaneously casts Light, Protection from all elements, Heroism, Holy Bonus, Bless and Walk on "
        "Water for the bargain basement price of 75 spell points."},
    /* Deadly Swarm 3AC65: 9730 = 28h (40). */
    {14,"Covers a group of monsters with biting, stinging, burrowing insects, inflicting 40 points of "
        "Physical damage to each monster."},
    /* Divine Intervention 3B68F: the caster's temporary age +5 (at most 250);
     * every condition but eradicated cleared; HP to the 2AAC2 maximum. */
    {16,"Heals the entire party of all damage and all conditions, even DEAD, short of ERADICATION.  "
        "The " RED "caster ages 5 years" PLAIN "."},
    /* Finger of Death 37621 branch 37811: types 1 and 3, a save, then the
     * damage is the HP up to 50 (a second save: 5). */
    {24,"Points at a group of animals or humanoids, inflicting up to 50 points of damage on each and "
        "bloodlessly slaying the weaker ones.  Other monsters are unaffected."},
    /* Heroism 3A58D: the party byte EDA6, added to the to-hit word 96E8 for
     * each equipped weapon slot (3891F at 38A08); no level changes. */
    {30,"Increases the chance to hit of every party member's weapon attacks by 1 per level of the caster."},
    /* Holy Bonus 3A434: the party byte EDA5, added to every member's weapon damage (3891F). */
    {31,"Increases the damage inflicted by every party member's weapon attacks by 1 point per level of the caster."},
    /* Hypnotize 37621 branch 37747: types 1 and 3, as Beast Master. */
    {33,"Like Beast Master, this spell hypnotizes a group of animals or humanoids into stillness until "
        "they overcome the spell.  Other monsters are unaffected."},
    /* Moon Ray 3B3BE: each member heals random(1, 30) (3977A). */
    {48,"Inflicts 30 points of Energy damage to each monster in sight and " GREEN "cures each party member"
        PLAIN " of 1 to 30 points of damage."},
    /* Poison Volley 3B903 / 281A7: one shooting row per member (972E 11h),
     * each its own 38818 hit test; a hit consumes the row (37452), 10. */
    {51,"Each party member fires one poison arrow at the monsters in front of the party.  An arrow that "
        "hits strikes one monster for 10 points of Poison damage."},
    /* Raise Dead 3B23D: HP 0 then 3977A(member, 1); 3B2B2 permanent Endurance - 1 (at least 1). */
    {56,"Removes the DEAD condition from a character, who returns with 1 hit point and " RED
        "permanently loses 1 point of Endurance" PLAIN "."},
    /* Recharge Item 3C4EE: random(1, 6) charges, 63 at most; no failure branch. */
    {57,"Restores 1 to 6 charges to an item that has at least one charge remaining, up to a maximum of 63 charges."},
    /* Resurrect 3B52E/3B596: permanent Endurance - 1, temporary age +5 (at most 250). */
    {58,"Removes the ERADICATED condition from a character, who " RED "permanently loses 1 point of Endurance"
        PLAIN " and " RED "ages 5 years" PLAIN "."},
    /* Sleep 37621 branch 377CF: types 1 and 3 only. */
    {61,"Puts a group of animals or humanoids to sleep until they overcome the spell or are damaged.  "
        "Other monsters are unaffected."},
    /* Star Burst 3B66C: 972E = 2 (Fire). */
    {63,"Includes all monsters in front of the party in a massive explosion, inflicting 500 points of "
        "Fire damage on each monster."},
};

/* The corrected text of spell id 0..75, or NULL (the original stands). */
static const char *xeen_bugfix_spell_description_text(unsigned spell)
{
    size_t i;
    for(i=0;i<sizeof fixes/sizeof *fixes;++i)if(fixes[i].spell==spell)return fixes[i].text;
    return NULL;
}
void xeen_bugfix_spell_description_apply(uint8_t *buf,size_t cap,unsigned spell)
{
    const char *t=xeen_bugfix_spell_description_text(spell);size_t n;
    if(!buf || !cap || !t)return;
    n=strlen(t);if(n>=cap)n=cap-1u;
    memcpy(buf,t,n);memset(buf+n,0,cap-n);
}
