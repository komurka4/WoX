#ifndef XEEN_COMBAT_H
#define XEEN_COMBAT_H
#include "xeen/audio.h"
#include "xeen/presentation.h"
#include "xeen/sprites.h"
#include "xeen/gameplay_tick.h"

typedef enum {
    XC_INACTIVE,
    XC_AWAITING_COMMAND,
    XC_AWAITING_TREASURE,
    XC_UNSUPPORTED_CONTINUATION,
    XC_AWAITING_CAST
} xeen_combat_phase;
/*
 * Where a combat call hands the fight back (its *boundary result, c->last_boundary, the game's lifecycle boundary).
 * Each value is the original address the fight goes on at, so traces and the original-byte harness name the same
 * place. A call can also end at an unsupported stop's own address, or at a tick's (enum xeen_tick_boundary) when a
 * time charge inside the fight stopped.
 */
enum xeen_combat_boundary {
    XEEN_COMBAT_COMMAND_POLL = 0x3912c,       /* the next party member's command is awaited: xeen_combat_command */
    XEEN_COMBAT_OVER = 0x39430,               /* the fight's cleanup has run; the game restores exploration */
    XEEN_COMBAT_CAST_DIALOG = 0x391e5,        /* C: the game runs the Cast dialog, then xeen_combat_after_cast */
    XEEN_COMBAT_USE_DIALOG = 0x39223,         /* U: the game runs the Use dialog, then xeen_combat_after_use */
    XEEN_COMBAT_DEFEATED_AT_ENTRY = 0x39505,  /* the monsters' attack as the fight began defeated the party; the
                                               * cleanup has run and the game's defeat follows */
    XEEN_COMBAT_ROUND_RESUME = 0x393d9,       /* the round reset's sky reload is done; the round goes on at its
                                               * attacker test once the RNG (and the audio, sound on) is given */
    XEEN_COMBAT_OUTER_RESUME = 0x393f5        /* a fight nested in the outer fight's no-attackers scene has ended;
                                               * the outer fight goes on (xeen_combat_resume_outer) */
};
/* The modal commands the game runs on the fight's behalf (c->modal_site, xeen_combat_after_modal's site): the
 * original handler each one returns to. */
enum xeen_combat_modal {
    XEEN_COMBAT_MODAL_CONTROL_PANEL = 0x392c9,    /* Tab */
    XEEN_COMBAT_MODAL_INFO = 0x392bb,             /* I */
    XEEN_COMBAT_MODAL_OPTIONS = 0x3929c,          /* O */
    XEEN_COMBAT_MODAL_QUICK_REFERENCE = 0x392c2,  /* Q */
    XEEN_COMBAT_MODAL_VIEW_MEMBER = 0x39322,      /* F1..F6 for a listed member */
    XEEN_COMBAT_MODAL_QUICK_FIGHT = 0x387f4       /* the Quick Fight key's spell (3D8C1 with its AX) */
};
#define XEEN_COMBAT_RNG_CAPACITY 128u
/* Owner of the original 2AAB1 -> 4128F queued ranged delivery that ends a
 * 2A701 update called directly by combat (38F5F 3906E, round reset 393C7).
 * It runs projectile travel and every queued 38049 ID action, then 41519;
 * it returns XEEN_OK or stores the exact unsupported address in *boundary. */
typedef xeen_status (*xeen_combat_ranged_fn)(void *user, xeen_movement *m, uint32_t *boundary);
/* The game owner's window services for the combat presentation:
 * window() is 1871B/18765 (the 4F2A1/4F57B pixels under the window and the
 * DS window bookkeeping), text() the 4E38C writer. Without them combat
 * writes only the DS bookkeeping and records the text (rule fixtures). */
/* Suspended outer fights (one per monster record at most). */
#define XEEN_COMBAT_NEST_MAX 255

typedef struct {
    void *user;
    void (*window)(void *user, unsigned window, int open);
    void (*text)(void *user, uint16_t window, const char *text);
} xeen_combat_windows;
/* Combat borrows the authoritative DS owned by xeen_game. Every field below
 * is either a phase/ownership marker or diagnostic evidence; gameplay
 * decisions read the original DS arrays (98B4 attackers, 9713 order, 97FE/
 * 97FF turn cursor and actor, 9707 turn-taken flags, 9720 combat party, monster records). */
typedef struct {
    xeen_combat_phase phase;
    uint8_t attackers[3], monster_id, active_actor;
    uint16_t icon_handle;
    unsigned long commands, actions;
    uint16_t rng_raw[XEEN_COMBAT_RNG_CAPACITY];
    uint8_t rng_low[XEEN_COMBAT_RNG_CAPACITY], rng_high[XEEN_COMBAT_RNG_CAPACITY];
    uint8_t rng_result[XEEN_COMBAT_RNG_CAPACITY];
    uint16_t rng_draws;
    uint8_t rng_trace_complete;
    uint16_t last_damage;
    uint8_t last_hit;
    uint8_t last_player_actor, player_attacks;
    uint8_t use_actor; /* 39223's [BP-2] (97FF when U was pressed) */
    uint8_t use_tail;  /* 3BE09..3BE13 already ran at Use's place (xeen_combat_use_tail) */
    uint16_t last_attack_score, last_attack_threshold;
    /* monster_actions counts executed 38049 turns; the arrays describe the
     * most recent monster turn's first two attacks. */
    uint8_t monster_actions, monster_targets[2], monster_hits[2];
    uint16_t monster_attack_scores[2], monster_defenses[2], monster_damage[2];
    /* Counts of 36AD1 deaths, 36EF2 reward applications and 39430 cleanup. */
    uint8_t monster_deaths, rewards_applied, cleanup_complete;
    /* 39291 Block commands accepted (96FB set for the actor). */
    uint8_t blocks;
    /* Most recent 281A7 volley: targets entered, 38818 hits and misses,
     * 38ADB saves, and where it ended (2849E wall, 284C4 no arrow left,
     * 287E3 all tiers, 28268 no shooter; 0 unsupported). */
    uint8_t volley_targets, volley_hits, volley_misses, volley_saves;
    uint32_t volley_end;
    /* Party 37A02 saving throws won and 37D6E monster specials applied. */
    uint8_t party_saves, specials_applied;
    uint8_t party_downed; /* 2A17D: members made unconscious or dead */
    /* Completed original round resets (3935A) and in-combat 1B503 calls. */
    uint16_t rounds, scenes;
    uint8_t entry_3066;  /* 38F71 [bp-4], read by cleanup 39558 */
    uint8_t entry_world; /* 38F6A [bp-6] (DS 94 at the entry), 39505's 191DE */
    uint32_t sky_site;   /* The 2FC7E call (393D3 round reset, 393F0 no attackers) last entered */
    xeen_gameplay_tick rest_tick; /* That 2FC7E stopped at 30DB3 (the "needs rest" scroll) */
    uint32_t last_boundary;       /* exact original address of the last unsupported stop */
    /* Borrowed from xeen_game for the 39083 loop-top text/icon presentation;
     * NULL in rule fixtures, which then skip only presentation. */
    xeen_presentation *presentation;
    /* Borrowed delivery owner; NULL keeps the explicit 2AAB1 stop. */
    xeen_combat_ranged_fn ranged_delivery;
    void *ranged_user;
    /* Borrowed window services (kept across resets like
     * ranged_delivery). */
    xeen_combat_windows windows;
    /* Borrowed sprite owner (user = ranged_user) for a nested
     * 38F5F's 38F8E 190A3("combat.icn") (load 1: 96C1 up, 95A8[96C1] = 98D4,
     * the new handle in 98D4) and its 39514 190CF (load 0: that handle and
     * every later load freed, 98D4 = 95A8[96C1], 96C1 down). NULL keeps the
     * explicit stop at 1C17E. Kept across resets like ranged_delivery. */
    xeen_status (*nested_icons)(void *user, xeen_movement *m, int load);
    /* 1C18A's 310B4 inside a combat scene (9756 set after the
     * nested 38F5F; user = ranged_user): the party cell's dispatcher, run to
     * its return by the game owner; XEEN_OK only when it returned without
     * input. NULL keeps the explicit stop at 1C18A. Kept across resets. */
    xeen_status (*cell_event)(void *user, xeen_movement *m);
    /* Nested 38F5F entries run by a combat scene's contact tail. */
    uint8_t nested_entries;
    /* 39083 loop tops run (each starts a new 3912C wait; its idle
     * scene count SI restarts). */
    unsigned long loop_tops;
    /* The 3914E command whose modal the game owner runs (phase
     * XC_AWAITING_CAST): 392C9 Tab, 392BB Info, 3929C Options, 392C2 Quick
     * Reference, 39322 F1..F6, 387F4 Quick Fight's 3D8C1(1); 0 none. */
    uint32_t modal_site;
    /* 38B00 Run attempts and escapes; 387AE Quick Fights; 39168/
     * 39188 turns. */
    uint8_t runs, escapes, quick_fights, turns;
    /* 392D3's DI = 1 (the panel's Mr Wizard): after the 39430
     * cleanup the game owner runs 3957C's map move (game.c wizard_move). */
    uint8_t wizard;
    /* The game owner's 1B503 argument was 0 for this scene (the
     * panel's 298D1 from combat's Tab): 1C112 skips 50BF6(1), 50BF6(3),
     * 4E33F and 50BF6(28h). Set for one xeen_combat_scene call. */
    uint8_t quiet_scene;
    /* A live party's nested 38F5F (1C17E) inside the 393F0 scene
     * of the round tail (38633 found no live attacker: 9A1C 1; a monster
     * entered contact in that scene). live_contact_site is set around that
     * scene only; outer_active while the nested fight runs, with the outer
     * frame's [bp-6]/[bp-4] (outer_world, outer_3066); live_entered for the
     * caller of the scene (the nested fight now waits at 3912C); after its
     * cleanup the game owner runs the contact scene's 1C183..1C1B3 tail and
     * xeen_combat_resume_outer continues the outer 393F5.
     * any depth (phase217 orig217 r-kf4: a third 38F5F at the
     * nested fight's own 393F8; the original keeps no depth, each level is
     * one more 38F5F frame on its stack, the DS combat globals are shared):
     * outer_active counts the suspended outer fights, outer_world[k] and
     * outer_3066[k] are frame k's [bp-6]/[bp-4] (innermost last). Every
     * open level's entry monster but the innermost's is dead (each level
     * opens only with no attacker left), so the depth is bounded by the
     * map's monster records (9C59, a byte): XEEN_COMBAT_NEST_MAX. */
    uint8_t live_contact_site, live_entered, outer_active, live_nested;
    uint8_t outer_world[XEEN_COMBAT_NEST_MAX], outer_3066[XEEN_COMBAT_NEST_MAX];
} xeen_combat;

/* 43244 (combat, keys 1A0A) and 43287 (exploration, keys 19E2):
 * the main button tables 98AC x words 197C, 98A8 y bytes 19A6, 98A4 widths
 * 19BA, 98A0 heights 19CE, 989C keys, 9898 enabled 19F6, 9896 = 98D4. */
void xeen_combat_buttons(xeen_movement *, int combat);
/* The nested fight's cleanup finished (phase XC_INACTIVE,
 * outer_active): RETF 393F5 to the outer round tail: no attacker left ->
 * the outer 39430 cleanup; else 39413 (F60A), 2F869, the 9A1C test and the
 * 39083 loop top. Boundaries as xeen_combat_command's. */
xeen_status xeen_combat_resume_outer(xeen_combat *, xeen_movement *, const uint8_t *, size_t,
                                     xeen_entity_random_fn, void *, xeen_native_audio *,
                                     uint32_t *boundary);
/* 390D2..39126, an idle tick at the 3912C poll without a key (the
 * host owns 4EA0D/4EA1C): 1B503(1); with si (the scenes since the loop top,
 * this one included) = 5 and 98B4 a monster, 1880E of the 306A monster's
 * row +32h. XEEN_OK, or SERVICE_REQUIRED with the exact boundary. */
xeen_status xeen_combat_idle(xeen_combat *, xeen_movement *, const uint8_t *, size_t,
                             xeen_entity_random_fn, void *, xeen_native_audio *, unsigned idle_count,
                             uint32_t *boundary);
/* 28D80 (presentation): 18765(26h), 18765(0Ch), 4ADB1(0, DS:2A5C)
 * (the main icons of the current 2A68.. handles), 50BF6(22h). With F384
 * and 9894 set 28DAF also redraws window 28h's speech face: not drawn (the
 * same presentation gap as the location exit). */
void xeen_combat_main_icons(const xeen_combat *, xeen_movement *, xeen_presentation *);

/* Original 38F5F for an indoor or outdoor Clouds contact with one to three
 * attacker slots. Initialization performs the original order construction,
 * first 38633 advance (including any monster turns that precede the first
 * party actor), the 96C0-gated 2A701 update with its 2AAB1 ranged delivery
 * and every 1B503 gameplay effect (outdoors 2126C selects attackers), then
 * stops at input 3912C. c->ranged_delivery/ranged_user survive the reset of
 * *c. Presentation/resource preconditions are transactional; once a
 * gameplay draw occurs an unsupported branch keeps its writes. */
xeen_status xeen_combat_begin(xeen_combat *, xeen_movement *, const uint8_t *, size_t,
                              xeen_sprite_cache *, const xeen_sprite_archives *,
                              xeen_presentation *, xeen_entity_random_fn, void *,
                              xeen_native_audio *);
/* The gameplay part of 38F5F (38F6D..3912C) for a host that owns the combat
 * sheet: c->icon_handle/presentation/ranged_delivery may
 * be preset and are kept. Same admission and boundary contract as xeen_combat_begin; a
 * rejection before any gameplay draw restores DS. */
xeen_status xeen_combat_initialize(xeen_combat *, xeen_movement *, const uint8_t *, size_t,
                                   xeen_entity_random_fn, void *, xeen_native_audio *);
/* One original 3912C command: numeric 1-3 target selection (392E6/FA/30E)
 * or A melee (39203), followed by 38633 and the common 3934F continuation:
 * round completion/reset, victory time, and the 39083 loop top. Returns
 * boundary 3912C when the next party actor owns input, 39430 when the
 * original cleanup has run and the game owner must restore exploration
 * resources, or an exact unsupported original address. C (391E5)
 * leaves DS as it is and returns boundary 391E5 with phase
 * XC_AWAITING_CAST: the game owner runs 3D8C1 and then calls
 * xeen_combat_after_cast. */
xeen_status xeen_combat_command(xeen_combat *, xeen_movement *, const uint8_t *, size_t,
                                xeen_entity_random_fn, void *, xeen_native_audio *, uint16_t,
                                uint32_t *);
/* 391ED after the combat Cast dialog: paid (3D8C1's AX) runs 38633, else
 * 2A1F6(9720[97FF]); then the 3934F continuation, with the boundaries of
 * xeen_combat_command. */
xeen_status xeen_combat_after_cast(xeen_combat *, xeen_movement *, const uint8_t *, size_t,
                                   xeen_entity_random_fn, void *, xeen_native_audio *, int paid,
                                   uint32_t *);
/* The return of a 3914E modal command (c->modal_site) that the
 * game owner ran: 392C9 Tab with 297D9's AX (2, Mr Wizard: 392D3's DI = 1, the 39430 cleanup),
 * 392BB Info (29EF0), 3929C Options (3E9B6), 392C2 Quick Reference (46BA0):
 * 392D9, 2A1F6(97FF); 39322 F1..F6 (44AE6): 3933F, 2A1F6(97FF) and 43244;
 * 387F4 Quick Fight's 3D8C1(1) with its AX (0: 371E2 Attack), then 39295's
 * 38633. Then the 3934F continuation, with the boundaries of
 * xeen_combat_command. */
xeen_status xeen_combat_after_modal(xeen_combat *, xeen_movement *, const uint8_t *, size_t,
                                    xeen_entity_random_fn, void *, xeen_native_audio *,
                                    uint32_t site, uint16_t ax, uint32_t *);
/* 2A26D (the element of a material below 37: segment 50D2's limits) and
 * 2A29D (the attribute of a material above 58: segment 50D0's). */
unsigned xeen_combat_element_category(uint8_t material);
unsigned xeen_combat_attribute_category(uint8_t material);
/* 3BE09..3BE13 where Use runs them, before its 44F92 ends: 97F9 =
 * 0, 2A701, 97FF = FFh (a combat Town Portal's next 2ABB8 draws can end the
 * program there); xeen_combat_after_use then skips its 97F9 and 2A701.
 * Phase XC_AWAITING_CAST. */
xeen_status xeen_combat_use_tail(xeen_combat *, xeen_movement *, const uint8_t *, size_t,
                                 xeen_entity_random_fn, void *, xeen_native_audio *,
                                 uint32_t *boundary);
/* 39249 after the combat Use dialog 44F92(record, 7). used: 3BCF7
 * ran an item (its 2A701 with 97F9 = 0, then 97FF = FFh), so 97FF differs
 * from [BP-2]: 97FF back and 38633's next actor (39255..39295); not used:
 * 3933F, 2A1F6(97FF) and 43244. Then the 3934F continuation, with the
 * boundaries of xeen_combat_command. Phase XC_AWAITING_CAST, boundary
 * 39223 from xeen_combat_command. */
xeen_status xeen_combat_after_use(xeen_combat *, xeen_movement *, const uint8_t *, size_t,
                                  xeen_entity_random_fn, void *, xeen_native_audio *, int used,
                                  uint32_t *);
/* Continue the victory path after its 2FC7E clock call suspended at the
 * original 30004 sky refresh. No command, reward, or RNG work is replayed. */
xeen_status xeen_combat_after_sky(xeen_combat *, xeen_movement *, const uint8_t *, size_t,
                                  uint32_t *);
/* The same with the session RNG: a sky reload reached from the
 * round reset's 2FC7E (393D3) continues at 393D9 (the attacker test, a
 * further 393F0 charge when none is left, the defeat tests, the loop top),
 * with the boundaries of xeen_combat_command. Without RNG that case stops
 * at 393D9. */
xeen_status xeen_combat_after_sky_rng(xeen_combat *, xeen_movement *, const uint8_t *, size_t,
                                      xeen_entity_random_fn, void *, xeen_native_audio *,
                                      uint32_t *);
/* The round charge's 2FC7E stopped at 30DB3 (an unrested party's
 * morning: 29168 and the weak increments done) and its owner has shown and
 * dismissed 18F2F's "The Party needs rest!": the charge's rest (9842 = 0,
 * 2FF59, the night test: 30004 when the sky changes, then
 * xeen_combat_after_sky_rng) and the 2FC7E caller's continuation as after
 * the sky reload (393D9, or 393F5 for the no-attackers charge). */
xeen_status xeen_combat_after_rest_rng(xeen_combat *, xeen_movement *, const uint8_t *, size_t,
                                       xeen_entity_random_fn, void *, xeen_native_audio *,
                                       uint32_t *);
/* Continue 39430 after the 2EA68 owner has successfully collected and cleared
 * pending treasure. No scene, reward, or random work is replayed. */
xeen_status xeen_combat_finish_treasure(xeen_combat *, xeen_movement *, uint32_t *);

/* Original rule helpers, exposed for differential tests. ch is a 0x162-byte
 * character record inside m->ds. */
uint16_t xeen_combat_stat(const xeen_movement *m, const uint8_t *ch, unsigned stat,
                          int base_only);                           /* 2BD94 */
int xeen_combat_stat_bonus(const xeen_movement *m, uint16_t value); /* 2C92E */
uint16_t xeen_combat_armor_class(const xeen_movement *m, const uint8_t *ch,
                                 int base_only);                         /* 4660F */
int16_t xeen_combat_thievery(const xeen_movement *m, const uint8_t *ch); /* 3CF5B */
/* The kinds of an item bonus (2BAFA's argument, xeen_combat_item_bonus): 0..6 the stats (xeen_stat), Endurance (3)
 * never receiving one (the bonus names DS:28AE skip it), then these; an attribute material's bonus is its category's
 * kind (+1 from Endurance on). */
#define XEEN_ITEM_BONUS_HIT_POINTS   7u
#define XEEN_ITEM_BONUS_SPELL_POINTS 8u
#define XEEN_ITEM_BONUS_ARMOR_CLASS  9u  /* with each worn armor's own AC */
#define XEEN_ITEM_BONUS_THIEVERY     10u
#define XEEN_ITEM_BONUS_RESISTANCES  11u /* 11 + an element category: its resistance from element materials */
int xeen_combat_item_bonus(
        const xeen_movement *m, const uint8_t *ch,
        unsigned stat); /* 2BAFA: equipped items (stat 9 AC, 11..16 resistances) */
uint16_t xeen_combat_max_hp(const xeen_movement *m, const uint8_t *ch); /* 2AAC2 */
uint16_t xeen_combat_max_sp(const xeen_movement *m, const uint8_t *ch); /* 46470 */
/* 2E68D(damage, type, target): party damage from terrain and events (17264
 * lava: 100, type 2, with 98D7 cleared). 186C0 closes every window. With
 * 98D7 = 0 every member, otherwise only `target`: asleep cleared, the party
 * resistance word subtracted for types 2..5 (EFFC..F002), then while the
 * 37A02 save of the type succeeds the damage halves (types 1..7), FX by
 * type, power shield EDA4, type 7 takes the current HP and sets dead, 2A152.
 * Then 30E2D(2) as one semantic scene (the 37B6D policy), 2ABB8 (portraits,
 * presentation) and 2F869. 98D7 = 2 (2E6B6) is the first two
 * able members (2AB96 not 12..15); with fewer than two, 2E883's loop from
 * the first able + 1 to the target (a member beyond the party stops at
 * 2E72B). Explicit stops: types above 7 (2E74F leaves DI unset) and a
 * contact that the timed wait's scene would start (1C17E). */
xeen_status xeen_combat_party_damage(xeen_combat *, xeen_movement *, const uint8_t *db, size_t,
                                     uint32_t damage, unsigned type, unsigned target,
                                     xeen_entity_random_fn, void *, xeen_native_audio *,
                                     uint32_t *boundary);
/* 2E68D up to its 2E8B3 30E2D(2) (the same members, damage, RNG,
 * FX and stops as xeen_combat_party_damage), with each member's pow frame
 * and 50BF6(21h) drawn when `presentation` is given. The caller runs
 * 30E2D(2) itself (4EA0D, one full exploration 1B503(1), 4EA1C), then
 * 2ABB8(1) and 2F869 (xeen_combat_party_check). */
xeen_status xeen_combat_party_damage_hits(xeen_combat *, xeen_movement *, const uint8_t *db, size_t,
                                          uint32_t damage, unsigned type, unsigned target,
                                          xeen_entity_random_fn, void *, xeen_native_audio *,
                                          xeen_presentation *presentation, uint32_t *boundary);
/* 2F869: F60A = 1 when no member (the combat-mapped ones in mode 2) is able. */
void xeen_combat_party_check(xeen_movement *m);
/* 2A152(ch, damage), the shared HP subtraction: HP -= damage
 * (16-bit); below 1 HP + max HP below 1 is dead (+150h, HP clamped to 0 when
 * still positive), otherwise unconscious (+14Fh, sound 26h); dead or HP <= -10
 * breaks every equipped armor piece with a nonzero id (+CCh |= 80h). *downed
 * is set when HP fell below 1. XEEN_SERVICE_REQUIRED: the sound failed. */
xeen_status xeen_character_subtract_hp(xeen_movement *m, uint8_t *ch, uint16_t damage,
                                       xeen_native_audio *audio, int *downed);
/* 37A02 threshold for save type 0..255 and the random(1, high) bound; the
 * member saves when the roll, compared signed, is <= the threshold. */
uint16_t xeen_combat_save_threshold(const xeen_movement *m, const uint8_t *ch, unsigned type,
                                    uint16_t *high);
/* Original 38D25 order construction over the current DS state. */
xeen_status xeen_combat_build_order(xeen_movement *m, const uint8_t *db, size_t n);
/* Queued monster 38049 ID argument; requires 97F6 and prepared combat-party
 * mapping. Shared original damage pipeline, no melee animation/initiative. */
xeen_status xeen_combat_ranged_action(xeen_combat *, xeen_movement *, const uint8_t *, size_t,
                                      unsigned, xeen_entity_random_fn, void *, xeen_native_audio *,
                                      uint32_t *);
/* 41519..4156F: 97F6/97F8 cleared, 2F869, and in mode 5 (a sleeping party)
 * the 4152D wake test (mode 1). */
xeen_status xeen_combat_ranged_complete(xeen_movement *, uint32_t *);

/* Original 39721: nonzero when the character's first missile slot (location
 * byte +A9 == 4) exists and its worst condition is not 8 or 11..15. */
int xeen_combat_has_missile(const uint8_t *ch);
/* Original 2A2D4 -> 281A7 party missile volley (pow 11, 972C=2 all tiers,
 * 972E=0 physical) from exploration: 97C7, 39721 rows, FX 49, 97F9, the
 * pow11.icn load (18931, reload 8C86) into F452, then tier 0 (98B4..),
 * and while arrows remain and the 1C1C1 cell allows, tiers 1..3 (98B7..).
 * Each target runs the 37452 volley (38818/3891F missile slot, 36738 type 3
 * with the 38ADB saving throw, 36AD1 death and 36EF2 experience). Ends with
 * 39654 or the plain 9744 clear, 306A/9804 restoration and 2EA68: no or
 * deferred treasure (xeen_treasure_check) returns XEEN_OK; otherwise the
 * 2EB9C prefix runs (97F9=0, 1B503(1), FX 54, F5FE=0) and *boundary is
 * 2EA68: the caller owns the dialog, the 2F1AA settlement and the return.
 * Every 1B503 is the semantic scene (96C0 countdown, effects, selector,
 * party scan). XEEN_OK when 281A7 returns; otherwise the exact original
 * boundary (spells, unsupported damage) with the writes made so far. c is
 * the borrowed exploration combat owner. */
xeen_status xeen_combat_shoot(xeen_combat *, xeen_movement *, const uint8_t *, size_t,
                              xeen_sprite_cache *, const xeen_sprite_archives *,
                              xeen_entity_random_fn, void *, xeen_native_audio *,
                              uint32_t *boundary);
/* 281A7(icon) for a spell handler (972C, 972E, 9730 set by the
 * handler; the caster at 9800). The rows: 972E 11h party rows 0..5 (972E
 * then 5), icon 0Bh row 0 with FX 31h, others row 0 with 972A 0; each
 * target's 371E2(9800, 1) takes 37452 for 972A 1 and 972E other than 12h,
 * else the 37621 single hit (Magic Arrow's 12h: the row +27h magic
 * resistance roll, 8 damage, 36738 type 1, 38D25). The rest, the boundaries
 * and the treasure contract as xeen_combat_shoot. */
xeen_status xeen_combat_spell_volley(xeen_combat *, xeen_movement *, const uint8_t *, size_t,
                                     xeen_sprite_cache *, const xeen_sprite_archives *,
                                     xeen_entity_random_fn, void *, xeen_native_audio *,
                                     unsigned icon, uint32_t *boundary);
/* 2EB9C, the start of a 2EA68 treasure dialog (xeen_treasure_check must be
 * DIALOG): 9744 clear, 97F9=0, one semantic 1B503(1), FX 54 when gold or
 * gems are pending, F5FE=0. Used by owners other than the volley, which
 * runs it itself. An unsupported scene boundary is returned in *boundary. */
xeen_status xeen_combat_treasure_prefix(xeen_combat *, xeen_movement *, const uint8_t *, size_t,
                                        xeen_native_audio *, uint32_t *boundary);
/* One semantic 1B503(1) (see xeen_combat_shoot) outside the combat loop,
 * e.g. 17EAF after Shoot's own 2A701. */
xeen_status xeen_combat_scene(xeen_combat *, xeen_movement *, const uint8_t *, size_t,
                              uint32_t *boundary);
#endif
