#ifndef XEEN_MONSTER_RECORD_H
#define XEEN_MONSTER_RECORD_H

/*
 * A monster type's record: one 60-byte row of the world's monster table (xeen.mon or dark.mon by 19F31's map
 * case, XOR 35h; game_resources.c loads it into monster_database), indexed by a map monster's
 * XEEN_MAP_MONSTER_ID. Each field is named from its original readers (docs/coverage/MONSTERS.md cites them);
 * the one byte no reader gives a role (+29h) stays unnamed. Phase 254.
 */
#define XEEN_MONSTER_RECORD_BYTES 60u

#define XEEN_MONSTER_NAME       0x00u /* NUL-terminated name (the combat list) */
#define XEEN_MONSTER_EXPERIENCE 0x10u /* dword: the experience its death awards */
#define XEEN_MONSTER_HP         0x14u /* word: the hit points a placed monster starts with */
#define XEEN_MONSTER_ARMOR      0x16u /* byte: armor class (the party's hit threshold 10 + it) */
#define XEEN_MONSTER_SPEED      0x17u /* byte: its speed in the combat turn order */
#define XEEN_MONSTER_ATTACKS    0x18u /* byte: attacks a turn */
#define XEEN_MONSTER_HATES      0x19u /* byte: the first target's scan (attack_target): a class, or the values below */
#define XEEN_MONSTER_DICE       0x1au /* word: damage dice */
#define XEEN_MONSTER_DIE        0x1cu /* byte: the die's sides */
#define XEEN_MONSTER_ATTACK_TYPE 0x1du /* byte: damage element; nonzero (not physical) always hits */
#define XEEN_MONSTER_SPECIAL    0x1eu /* byte: the special after an unsaved hit (37D6E; Identify Monster's DS:27BE) */
#define XEEN_MONSTER_TO_HIT     0x1fu /* byte: the attack score's base (roll + it / 4 + 504DA(1, it)) */
#define XEEN_MONSTER_RANGED     0x20u /* byte: nonzero shoots (the 2A701 scheduler) */
#define XEEN_MONSTER_TYPE       0x21u /* byte: the monster type (slayer weapons; 37621's spell types, CS:127B) */
#define XEEN_MONSTER_RESISTANCES 0x22u /* 7 bytes: fire, electricity, cold, poison, energy, magic, physical */
#define XEEN_MONSTER_MAGIC_RESISTANCE    0x27u /* the magic one: a spell's resistance roll (37621) */
#define XEEN_MONSTER_PHYSICAL_RESISTANCE 0x28u /* the physical one (36738's weapon damage) */
#define XEEN_MONSTER_GOLD       0x2au /* word: treasure gold */
#define XEEN_MONSTER_GEMS       0x2cu /* byte: treasure gems */
#define XEEN_MONSTER_DROP       0x2du /* byte: the item drop level (0 none) */
#define XEEN_MONSTER_FLYING     0x2eu /* byte: flies (2A5D1's water and space; the outdoor scene's bob) */
#define XEEN_MONSTER_SPRITE     0x2fu /* byte: its sprite files' number (%03u.mon, %03u.att) */
#define XEEN_MONSTER_BOUNCE     0x30u /* byte: nonzero runs its frames back and forth, else round 0..7 */
#define XEEN_MONSTER_EFFECT     0x31u /* byte: the colour effect (the raster mode) and its timer */
#define XEEN_MONSTER_SOUND      0x32u /* byte: a sound effect: the targeted monster's, when it is in view */
#define XEEN_MONSTER_ATTACK_VOICE 0x33u /* 9 bytes: its attack's voice file (38122 "%s.voc") */

/* XEEN_MONSTER_TYPE values, by the slayer weapons' kinds (371E2's melee table of the DS:2620 names) and the
 * spells that test them (37621): Dragon Sleep dragons, Holy Word and Turn Undead the undead, Golem Stopper golems,
 * Insect Spray bugs. Type 3, which no slayer names, is the other one that Sleep, Beast Master, Hypnotize and
 * Finger of Death affect with the beasts; it stays a number. */
#define XEEN_MONSTER_TYPE_MONSTER 0u
#define XEEN_MONSTER_TYPE_BEAST   1u
#define XEEN_MONSTER_TYPE_BUG     2u
#define XEEN_MONSTER_TYPE_UNDEAD  4u
#define XEEN_MONSTER_TYPE_GOLEM   5u
#define XEEN_MONSTER_TYPE_DRAGON  6u

/* XEEN_MONSTER_ATTACK_TYPE values: 0 physical, then the elements by their names in DS:27BE's entries 1..6 (the
 * party's damage, 37B6D, takes the Protection from Elements words for fire..poison by them). */
#define XEEN_ATTACK_PHYSICAL    0u
#define XEEN_ATTACK_MAGIC       1u
#define XEEN_ATTACK_FIRE        2u
#define XEEN_ATTACK_ELECTRICITY 3u
#define XEEN_ATTACK_COLD        4u
#define XEEN_ATTACK_POISON      5u
#define XEEN_ATTACK_ENERGY      6u
/* XEEN_DS_DAMAGE_TYPE (972E) takes the attack types 0..6 above for damage, and these above them: the spells whose hit
 * 37621 resolves (combat.c spell_hit), the Poison Volley (the volley's 972E = 5) and Magic Arrow. A sleep spell's value
 * is also the monster's +7 condition. */
enum xeen_spell_damage {
    XEEN_DAMAGE_SLEEP = 7,
    XEEN_DAMAGE_FINGER_OF_DEATH = 8,
    XEEN_DAMAGE_HOLY_WORD = 9,
    XEEN_DAMAGE_MASS_DISTORTION = 10,
    XEEN_DAMAGE_TURN_UNDEAD = 11,
    XEEN_DAMAGE_BEAST_MASTER = 12,
    XEEN_DAMAGE_DRAGON_SLEEP = 13,
    XEEN_DAMAGE_GOLEM_STOPPER = 14,
    XEEN_DAMAGE_HYPNOTIZE = 15,
    XEEN_DAMAGE_INSECT_SPRAY = 16,
    XEEN_DAMAGE_POISON_VOLLEY = 17,
    XEEN_DAMAGE_MAGIC_ARROW = 18
};

/* XEEN_MONSTER_SPECIAL values that 37D6E acts on, named as Identify Monster shows them (DS:27BE: 23 names "None",
 * "Magic".. 22 "Stone"; 3A2EC indexes on, so 23 shows the next pointer, "Spell Failed!", p281A, which four readers
 * also use alone). The element names 1..4 and 6 and 11 "InLove" do nothing there (37FF5), and 20,
 * "Age+5", adds one to the age bonus (37ED2): the original's behaviour, kept. */
#define XEEN_SPECIAL_POISON        5u
#define XEEN_SPECIAL_DISEASE       7u
#define XEEN_SPECIAL_INSANE        8u
#define XEEN_SPECIAL_ASLEEP        9u
#define XEEN_SPECIAL_CURSE_ITEMS   10u /* "CurseItm" */
#define XEEN_SPECIAL_DRAIN_SP      12u /* "DrnSPts" */
#define XEEN_SPECIAL_CURSE         13u
#define XEEN_SPECIAL_PARALYSIS     14u /* "Paralys" */
#define XEEN_SPECIAL_UNCONSCIOUS   15u /* "Uncons" */
#define XEEN_SPECIAL_CONFUSE       16u
#define XEEN_SPECIAL_BREAK_WEAPONS 17u /* "BrkWpn" */
#define XEEN_SPECIAL_WEAK          18u
#define XEEN_SPECIAL_ERADICATE     19u /* "Erad" */
#define XEEN_SPECIAL_AGE           20u /* "Age+5" */
#define XEEN_SPECIAL_DEAD          21u
#define XEEN_SPECIAL_STONE         22u
#define XEEN_SPECIAL_NAMES         24u /* DS:27BE's 23 entries and the next pointer (Identify Monster stops past it) */

/* XEEN_MONSTER_HATES values besides a class (xeen_class 0..9): */
#define XEEN_MONSTER_HATES_DWARVES  12u /* the first able Dwarf: the only race the scan tests */
#define XEEN_MONSTER_HATES_EVERYONE 15u /* each attack on every member */
/* (1 skips the scan, so a Paladin is never hated: the original's own compare, kept as 1.) */

/* Clouds (xeen.mon) monster ids that the original tests by number, each with its rows' name. The combat and
 * movement tests also require Clouds (DS:94 = 0); the arena's are on its Clouds map. */
#define XEEN_CLOUDS_WATER_GOLEM   0x1cu /* 28: the arena makes it a Jouster */
#define XEEN_CLOUDS_JOUSTER       0x1du /* 29 */
#define XEEN_CLOUDS_ICE_TROLL     0x35u /* 53: back to full hit points at each combat round (393A0) */
#define XEEN_CLOUDS_WATER_DRAGON  0x3bu /* 59: stays on water outdoors (2A5D1); the arena makes it a Cloud Golem */
#define XEEN_CLOUDS_CLOUD_GOLEM   0x3cu /* 60 */
#define XEEN_CLOUDS_DIAMOND_GOLEM 0x43u /* 67: the arena's highest opponent */
#define XEEN_CLOUDS_LORD_XEEN     0x59u /* 89: no damage from spells or missiles (36743), +100 from the rest (36787),
                                         * leaves weapon 5Ah (36C4B) */

#endif
