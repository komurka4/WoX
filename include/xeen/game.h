#ifndef XEEN_GAME_H
#define XEEN_GAME_H
#include "xeen/addons.h"
#include "xeen/navigation.h"
#include "xeen/event_resources.h"
#include "xeen/event_ui.h"
#include "xeen/event_dispatch.h"
#include "xeen/gameplay_tick.h"
#include "xeen/timer.h"
#include "xeen/audio.h"
#include "xeen/entities.h"
#include "xeen/presentation.h"
#include "xeen/sprites.h"
#include "xeen/input_poll.h"
#include "xeen/combat.h"
#include "xeen/main_menu.h"
/* Native bounded exploration/event session. Owns DS and decoded resources;
 * borrows immutable archive bytes until destroy. No restored party or original
 * dispatcher prologue/cleanup is implied by the default bounded mode. The
 * opt-in lifecycle mode has separately reported verified limits. Frontend
 * callbacks must not reenter. */
typedef struct xeen_game xeen_game;
typedef struct {
    uint8_t world, x, y, direction;
    uint16_t map;
} xeen_game_start;
typedef enum {
    XG_NAVIGATION,
    XG_WAITING,
    XG_STOPPED
} xeen_game_phase;
typedef enum {
    XGC_BOUNDED_SLICE,
    XGC_COMPLETED,
    XGC_PENDING_INPUT,
    XGC_EXTERNAL,
    XGC_UNSUPPORTED,
    XGC_INPUT_ERROR,
    XGC_INTERNAL,
    XGC_ORIGINAL_EXIT,
    XGC_PENDING_TIMER
} xeen_game_command_status;
/* The title menu's stages (xeen_game_view.menu_stage; 0 when the menu is not running), set by the main menu's
 * state machine (game_menu.c, game_menu_load.c, game_newgame.c, game_pairs.c). A stage named _POLL waits for a key
 * there; the others draw a frame (or a page) first. */
enum xeen_menu_stage {
    XEEN_MENU_TITLE = 1,     /* the title screen's frame */
    XEEN_MENU_TITLE_POLL,    /* its key poll */
    XEEN_MENU_FRAME,         /* the main menu's frame */
    XEEN_MENU_POLL,          /* its key poll */
    XEEN_MENU_DIALOG,        /* the load (or new game) slot dialog's frame */
    XEEN_MENU_DIALOG_POLL,   /* its key poll */
    XEEN_MENU_SCROLL_POLL,   /* the scroll's key poll: "There are no saved games to load!" */
    XEEN_MENU_SESSION,       /* a game has been loaded: the host restarts the session (session_ready) */
    XEEN_MENU_CREDITS_POLL,  /* 9..12: the credits' four pages, one poll each */
    XEEN_MENU_OPTIONS = 13,  /* Other Options' frame */
    XEEN_MENU_OPTIONS_POLL,  /* its key poll */
    XEEN_MENU_PAIR_POLL      /* the yes/no about an invalid save pair at program start (184DE's 2937A) */
};
typedef struct {
    xeen_game_command_status command_status;
    uint32_t lifecycle_boundary;
    uint16_t dispatcher_ax, clock;
    int safe_to_continue; /* host command availability, not full original equivalence */
    xeen_game_start position;
    xeen_game_phase phase;
    xeen_slice_navigation_result navigation;
    xeen_exec_status event_status;
    uint32_t boundary;
    uint16_t sequence, offset, input;
    uint8_t operation, metadata;
    uint16_t tile, mode, threshold;
    size_t presentations, completions, steps;
    int event_eligible;
    unsigned long accepted_commands, scene_returns, idle_scenes;
    int world_entry_complete;
    uint16_t scene_group; /* persistent shuffle selection (original SI) */
    xeen_combat_phase combat_phase;
    uint8_t combat_attackers[3], combat_monster_id, combat_active_actor;
    unsigned long combat_commands, combat_actions;
    uint16_t combat_last_damage;
    uint8_t combat_last_hit, combat_rng_trace_complete;
    uint16_t combat_rng_draws;
    uint8_t combat_last_player_actor, combat_player_attacks;
    uint16_t combat_last_attack_score, combat_last_attack_threshold;
    uint8_t combat_monster_actions, combat_monster_targets[2], combat_monster_hits[2];
    uint16_t combat_monster_attack_scores[2], combat_monster_defenses[2], combat_monster_damage[2];
    uint8_t combat_monster_deaths, combat_rewards_applied, combat_cleanup_complete;
    uint16_t combat_rounds, combat_scenes;
    uint8_t combat_blocks;
    /* Exploration Shoot (17E98) volleys completed and the last 281A7 volley. */
    unsigned long shots;
    uint8_t volley_targets, volley_hits, volley_misses, volley_saves;
    uint32_t volley_end;
    uint16_t combat_rng_raw[XEEN_COMBAT_RNG_CAPACITY];
    uint8_t combat_rng_low[XEEN_COMBAT_RNG_CAPACITY];
    uint8_t combat_rng_high[XEEN_COMBAT_RNG_CAPACITY];
    uint8_t combat_rng_result[XEEN_COMBAT_RNG_CAPACITY];
    unsigned long confirm_ignored_keys; /* 2937A keys other than Esc/'N'/'Y' (294D4 polls again) */
    uint8_t choice; /* The open 2F5B1 numeric choice's count (its keys are digits) */
    /* Key polls (189E3) of a waiting modal (yes/no, confirm, NPC,
     * "Who will", paged display, scrolls, treasure) so far; the DS at the
     * latest one's entry is xeen_game_modal_poll_ds (evidence). */
    unsigned long modal_polls;
    uint16_t modal_poll_key; /* F5FE after the latest one */
    unsigned long item_passes;
    uint16_t item_ticks; /* 30E05 passes, the latest 4EA1C value */
    unsigned long loc_tick_reads;
    uint16_t loc_ticks; /* The 26E60 loop's 4EA1C reads, the latest value */
    /* The Reaper (34FAA) and gatekeeper (35992) cutscenes: every
     * clock read (4EA1C/4EA44/4EA6C) and 189E3 poll so far, the latest one's
     * return site, value (ticks, or F5FE after the poll) and kind; whether
     * the next resume is a poll, and the cutscene running (8, 10 or 13). */
    unsigned long cut_events, cut_polls;
    uint32_t cut_ret;
    uint16_t cut_value;
    uint8_t cut_is_poll, cut_poll_next, cut_kind;
    /* Game over (17F28 -> 1596F -> 15647 -> 1624F -> 1660E): stage
     * 0 none, 1 the 1596F flames, 2 its laugh loop, 3 the 15647 book, 4 the
     * 1660E main menu reached, 5 the menu begun or the XEEN.EXE hand-off
     * reached, 6 a teardown's voice wait; frames presented, draws the
     * native raster could not render (colour mode 8), and the original
     * audio requests in order (the silent backend plays none). */
    uint8_t game_over_stage;
    unsigned game_over_frames, game_over_skipped_draws, game_over_event_count;
    char game_over_events[12][16];
    /* Main menu: owner stage (enum xeen_menu_stage; 0 none), key polls answered so far,
     * the explicit stop of an unsupported option, a load at 19E82. */
    int menu_stage;
    unsigned long menu_polls;
    uint32_t menu_stop;
    int session_ready;
    uint8_t menu_slot;
    /* Control panel: owner stage (0 none), poll sessions started
     * (the host feeds one key per session), key polls, the explicit stop of
     * an unsupported option, completed saves. */
    int panel_stage;
    unsigned long panel_sessions, panel_polls;
    uint32_t panel_stop;
    unsigned long saves;
    /* Completed Start a New Game creations (2B4AE..2B583). */
    unsigned long new_games;
} xeen_game_view;
xeen_game *xeen_game_create(const uint8_t *, size_t, const uint8_t *, size_t, xeen_game_start,
                            xeen_ui_present_fn, void *, char *, size_t);
/* Original paired save startup: party/header from Darkside state when present,
 * map state from header-selected world. No placement overrides. Borrowed archive
 * storage must survive the game. Unsupported saved placement fails explicitly.
 * Map/cache and party are restored here; world/map sprite services follow using
 * the ordinary preparation APIs. This is a bounded startup, not full DOS init. */
xeen_game *xeen_game_create_saved(const uint8_t *, size_t, const uint8_t *, size_t, const uint8_t *,
                                  size_t, const uint8_t *, size_t, char *, size_t);
/* Opt in before any command to original lifecycle checking. The default
 * constructor preserves the explicitly bounded exploration contract
 * (fixture placement, no lifecycle).
 * Once enabled, diagnostic abandonment cannot unlock incomplete commands. */
int xeen_game_enable_lifecycle(xeen_game *);
/* Restore verified original CUR roster/membership/session bytes before any
 * command, retaining the explicitly selected map fixture placement. Enables
 * bounded lifecycle checking; does not imply complete DOS startup. Requires
 * the program data (xeen_game_load_program_data first): the member names are
 * the DS's (19953). */
int xeen_game_restore_party(xeen_game *, char *, size_t);
/* Explicit nonblocking silent development backend. May be enabled before
 * commands or at pending 1881B; resumes that call once, preserving gameplay
 * boundaries. Returns zero if configuration is invalid. No audible output. */
int xeen_game_enable_silent_audio(xeen_game *);
const xeen_native_audio *xeen_game_audio(const xeen_game *);
/* Attach a borrowed playback sink (after xeen_game_enable_silent_audio). */
void xeen_game_set_audio_sink(xeen_game *, const xeen_audio_sink *);
/* The CD drive's status model (xeen_native_audio cd_model): 0 no
 * device (never busy; the default), 1 the modeled drive. OBSERVER (may be
 * NULL) sees every 47F3D query. Call after the audio is enabled. */
void xeen_game_set_cd_status(xeen_game *, int model,
                             void (*observer)(void *, const xeen_cd_status_query *), void *context);
/* The digital driver's voice status model (xeen_native_audio
 * voice_model): 0 no driver or nullsnd (187E9 answers 0; the default), 1 an SB
 * driver with TRIM (prosnd 0Ah, blastsnd 0Eh), 2 the PAS's specsnd (TRIM 0Ah,
 * 81 PIT cycles per sample; Phase 285A). OBSERVER (may be NULL)
 * sees every modelled 187E9 call. Call after the audio is enabled. */
/* Phase 286: [audio] speech = off (wox_config.h): the cutscenes' voice tests take the no-voice branch and the panel's
 * Speech is held off (xeen_native_audio_speech_hold). The voice model and the engine are set apart (none, silent). */
void xeen_game_set_speech_off(xeen_game *g, int off);
void xeen_game_set_voice_status(xeen_game *, int model, unsigned trim,
                                void (*observer)(void *, const xeen_voice_request *),
                                void *context);
/* The host's INT 1Ah service for the voice model's clock: a start
 * or a query reads it (xeen_timer_peek: the timer's sample words are not
 * written). Without one the model uses the game's last BIOS reading. */
void xeen_game_set_clock_service(xeen_game *, xeen_timer_bios, void *);
/* Phase 287: the host's hold for the PC speaker's blocking voice start (XEEN_VOICE_MODEL_PCSPEAKER, audio.h): called
 * inside the start with the driver's interrupt count; it returns when the start would have returned (the window waits,
 * a headless run advances its BIOS clock). NULL: the start returns at once. */
void xeen_game_set_voice_block(xeen_game *, void (*hold)(void *, uint32_t irqs), void *user);
/* Before the first command, load the original map .mob object records and
 * world animation table into the same restored DS. Only this bounded subset
 * is prepared: monster initialization continues at 1A344; audible playback,
 * other segments and full map startup remain unresolved. No command unlock. */
/* runtime_assets supplies the archive containing clouds.dat/dark.dat (the
 * available combined installation stores both in DARK.CC). Borrowed for call. */
xeen_resource_status xeen_game_prepare_map_objects(xeen_game *, const uint8_t *, size_t);
/* Complete the bounded monster/wall expansion before acting. runtime_assets
 * owns xeen.mon/dark.mon. random is an explicit original0..7 service; sprite
 * references are native cache handles (16 attack, then16 ordinary), resolved
 * and owned by the caller, not DOS segments. No default RNG/cache is fabricated.
 * Currently only Darkside29 has authenticated database backing. Cloud49's FF
 * record returns NOT_FOUND unchanged. Does not imply complete map startup. */
xeen_resource_status xeen_game_prepare_monsters(xeen_game *, const uint8_t *, size_t,
                                                xeen_entity_random_fn, void *,
                                                const uint16_t sprite_references[32]);
/* Bind native object/wall sprite cache references before acting. Buffers and
 * cache ownership remain with caller; no missing asset is treated as loaded.
 * This explicit adapter does not implement sprite decoding or DOS cache layout. */
/* Test support (the xeen_test_support library, src/test_support/; not in the game): */
xeen_resource_status xeen_game_bind_scene_resources(xeen_game *, const uint16_t[16],
                                                    const uint16_t[16]);
/* Explicit native frame/cursor provider, borrowed until game destroy. Binding
 * allowed before acting or at pending 4E33F; does not acknowledge the call.
 * Caller must provide decoded pixels and authentic frame contents for real
 * presentation. Diagnostic surfaces remain diagnostic. No default is invented. */
int xeen_game_bind_presentation(xeen_game *, xeen_presentation *);
/* Program load: install the initialized data segment DS:0000..8C4B, as DOS
 * does when loading the executable. the image is compiled in
 * (xeen/program_data.h, generated from src/data); no file is read. Before
 * party restoration only, once. Rejects any already-written native byte that
 * disagrees with the image. Startup code changes to these bytes are not
 * applied, except B95 (15D15). The DS-backed operations (party restoration,
 * world entry, the map preparations, the menu's session hand-off) refuse a
 * session without it; the menu, saved and loaded sessions have it. */
int xeen_game_load_program_data(xeen_game *);
/* Optional other world's CC archive (xeen.cc for a Darkside session), bound
 * before party restoration; borrowed until destroy. Startup 15D15 sets B95
 * when xeen.cc is available (Clouds session: its own CC). */
int xeen_game_bind_other_archive(xeen_game *, const uint8_t *, size_t);
/* Optional intro.cc archive (the Rest dream's scene1.raw, 28F45), borrowed
 * until destroy. Without it a dream stays the explicit stop 28F45. */
int xeen_game_bind_intro_archive(xeen_game *, const uint8_t *, size_t);
/* World-entry resident sprite handles (see sprites.h), after restoration and
 * before map objects. Startup handles 1..7 are reserved, identity unresolved.
 * The names are the DS's: like the map preparations, it requires the program
 * data. */
xeen_resource_status xeen_game_enter_world(xeen_game *);
/* Map-load sprite allocation 1A7A6..1B0AC (+2A0FB) after monsters; replaces
 * synthetic bind_scene_resources/references. Darkside draws one random(0..6)
 * for music through `random`. Music requests are recorded, not played. */
xeen_resource_status xeen_game_prepare_map_sprites(xeen_game *, const uint8_t *, size_t,
                                                   xeen_entity_random_fn, void *);
/* Scene frame pacing at 1C1B3 with the BIOS tick provider: one original 4EA44
 * sample per call (palette writes go to the bound presentation). Returns
 * SERVICE_REQUIRED either way; the boundary becomes 17B09 (caller input poll)
 * only after the scene genuinely returns (>= 2 ticks since the 4EA35 base). */
xeen_status xeen_game_resume_frame_wait(xeen_game *, xeen_timer_bios, void *);
/* The Rest dream's two waits (28F9D, 2900C): one 4EA1C sample per call
 * until 7 ticks have passed since the 4EA0D base; SERVICE_REQUIRED either way. */
xeen_status xeen_game_resume_rest_wait(xeen_game *, xeen_timer_bios, void *);
/* The treasure item lines' 30E05 waits (boundaries 30E05, 30E0D, 30E1A). */
xeen_status xeen_game_resume_item_wait(xeen_game *, xeen_timer_bios, void *);
/* The 1791F loop exit at 17F28 once F60A is set (party defeat): one call
 * per presented frame through the 1596F death sequence and the 15647(1,0)
 * book, with their original 189E3 polls and 4EA0D/4EA1C waits, then the
 * 1624F teardown and the 15F9C outer loop up to the main menu 1660E, which
 * is an explicit boundary (title screen, new game, load, credits are not
 * native). SERVICE_REQUIRED while frames remain. F64A alone stops at 17F4B. */
xeen_status xeen_game_resume_exit(xeen_game *, xeen_timer_bios, void *);
/* The title-screen main menu 1660E (DS 95 set: the World of Xeen
 * title) after a defeat (the 15F9C outer loop) or at program start. One
 * call per frame or poll, with the original 4EA0D/4EA1C waits and 189E3
 * polls: the title animation, any key opens the four buttons, Esc returns
 * to the title. 'L' lists dark??.wox (18E77) and runs the 2AD6D Load Game
 * Menu (digits select, Enter or 'S' loads, Esc returns); with no save the
 * 18F2F scroll. A loaded slot runs the 1978C copies (kept in memory), the
 * 16FAB tail, 15FE9, 43C37 (19953 restores the CUR party) and the 1791F
 * prologue, then waits at 19E82 (session_ready). Explicit stops: 16A53
 * (new game), 16B11 (credits), 16B23 (options), 197F5 (a missing slot
 * file: the original's fatal 2C80D) and 1660E itself without the save-file
 * service or with DS 95 clear. the program-start 18363 repairs
 * zero ids (18441, 18037 for a Clouds half) and asks 2937A about an invalid
 * pair (184DE: Delete unlinks both files, Exit to DOS ends the program at
 * 2C80D); 184DE inside a new game's 19278 stays an explicit stop. */
xeen_status xeen_game_resume_menu(xeen_game *, xeen_timer_bios, void *);
/* The in-game control panel (Tab at the 17B09 poll, 297D9). One
 * call per poll: the panel (29946), the "game saved" key (296B9), the
 * 2AD6D Save/Load Game Menu (2B1EA) and the 2894F name entry (288C4,
 * 2890C); each panel, dialog and name poll follows a 4EA0D/1B503(0) scene
 * (boundary 4EA0D, the ordinary scene continuations). 'S' saves into the
 * current slot (95A6), 'A' (Save As) chooses a slot (2937A before
 * overwriting a named one) and a name, 'L' loads a slot: 1978C, F64A = 2,
 * 17F28, the 15647 book and 16159..1791F to 19E82 (session_ready, as a
 * menu load). E, M, T, P toggle; other keys close the panel. Q's yes
 * (29B4D) is the program exit 2C80D. Explicit
 * stops: 29925 (the debug clock), 19278/19629 (no CUR
 * images or no writable save directory). Native since: 29517 (P112), the
 * combat refusals 29A94/29B12 (P120), Mr Wizard 29B76 (P121), the speech
 * toggle's close 29C5B (P122: 19CC3, its CD-side request, 191DE(96E2)). */
xeen_status xeen_game_resume_panel(xeen_game *, xeen_timer_bios, void *);
/* Host DOS file access for the menu (borrowed until destroy). */
int xeen_game_bind_save_files(xeen_game *, const xeen_save_files *);
/* Owner-approved add-ons (include/xeen/addons.h; NOT original behavior):
 * the host's state, borrowed until destroy and kept across the host's game
 * objects (a Load's new session). NULL (the default) or every add-on off is
 * the original game. */
void xeen_game_bind_addons(xeen_game *, xeen_addons *);
/* The session RNG of a program-start menu game (before its first
 * resume): the 18441 save id and a new game's 41D20 wares draw from it.
 * The menu's 'S' starts a new game: 2AD6D(0) "Start Game Menu" (a slot,
 * 2937A before a named one, the 16332 name entry), the 2B3E0 game
 * preference (Esc, 'A' Adventurer, 'W' Warrior; boundary 2B44D, a panel
 * poll), the 19314 CURs from the CCs, the CC party, 41D20, the calendar,
 * the 19278 pair save (FA90 = 1: 18363 and its 18441 repair, 1978C), then
 * the 16FAB..1791F tail to 19E82 (session_ready, as a menu load). */
int xeen_game_bind_random(xeen_game *, xeen_entity_random_fn, void *);
/* The carry flag the bound random's last draw left (50525's final
 * SBB), with the random's user; the driver's colour modes 2 and 5 add it
 * into their pixel generator. Without it the carry is taken as 0. */
int xeen_game_bind_random_carry(xeen_game *, int (*)(void *));
/* Program start at 1660E: 15CDB..15F9C on the initialized DS image (the
 * compiled program data; config: XEEN.CFG bytes or NULL; clock: the DOS clock
 * of the 15CF3 play-time base 98B0, or NULL). Both CCs are borrowed. */
xeen_game *xeen_game_create_menu(const uint8_t *, size_t, const uint8_t *, size_t, const uint8_t *,
                                 size_t, const xeen_dos_clock *, char *, size_t);
/* The frontend input provider of a program-start menu game (copied). */
int xeen_game_bind_menu_input(xeen_game *, const xeen_input_provider *);
/* Bytes their holder owns (malloc'd, freed by the holder) and their size; NULL and 0 while there are none. */
typedef struct {
    uint8_t *bytes;
    size_t size;
} xeen_owned_bytes;
/* A loaded game at 19E82: its DS and the two in-memory CUR images (world
 * 0 Clouds, 1 Darkside; the session's until xeen_game_create_loaded takes
 * them or xeen_game_session_free frees them); the F42C block's modelled
 * bytes (f42c_known of them). */
typedef struct {
    uint8_t ds[65536];
    xeen_owned_bytes cur[2];
    uint8_t slot;
    uint8_t *f42c;
    uint32_t f42c_known;
} xeen_game_session;
/* Transfers the session out of a game at session_ready (the old game stays
 * stopped at 19E82 and is then destroyed by the host). */
int xeen_game_take_session(xeen_game *, xeen_game_session *);
void xeen_game_session_free(xeen_game_session *);
/* Phase 310: the "Please Wait..." (or "Oops...") box of the session's map load, shown by the game that ends (the host
 * calls it after xeen_game_take_session, before destroying that game): display only, the game's state and pages
 * unchanged. Nothing without a box, a presentation or a display hook, or with add-on please_wait_skip. */
void xeen_game_show_session_wait(xeen_game *, const xeen_game_session *);
/* The new runtime for a taken session: the map startup of its world over
 * the carried DS; takes the CUR images. Then, as at program start, enter
 * world, the map objects/monsters/sprites, audio, presentation and the
 * world-entry tail (xeen_game_start_world_entry). CCs are borrowed. */
xeen_game *xeen_game_create_loaded(xeen_game_session *, const uint8_t *, size_t, const uint8_t *,
                                   size_t, char *, size_t);
/* A loaded session's CUR image of world 0/1 (for saving), else NULL; the
 * active world's is its live state archive. */
const uint8_t *xeen_game_cur(const xeen_game *, unsigned world, size_t *);
/* Read-only native sprite cache after world entry, else NULL. */
const xeen_sprite_cache *xeen_game_sprites(const xeen_game *);
/* Recorded music requests (count) and last requested name. */
unsigned long xeen_game_music(const xeen_game *, const char **name);
/* Explicit continuation of timer-bound restored sessions. Supplies
 * external BIOS inputs; never resets DS, repeats dispatch or accepts another
 * command. Registers describe the selected timer ABI, not whole caller CPU.
 * May suspend between caller and scene samples without replaying either.
 * Returns BAD_INPUT at every boundary other than 4EA0D/4EA35. */
xeen_status xeen_game_resume_timer(xeen_game *, xeen_timer_registers *, xeen_timer_bios, void *);
/* Test support (the xeen_test_support library, src/test_support/; not in the game): */
const uint8_t *xeen_game_timer_cs(const xeen_game *);
/* Explicit continuation at 1B70F (render records) or 1C9D4 (outdoor terrain /
 * visibility), or 1BBDF (effect records), and the initialized monster/object/wall helper stages
 *1ED46/20011/208FB, record shuffle 1BDE4, bound native 4E33F, terrain
 * construction 22034, visibility/surface geometry 22B25 (stopping before
 * renderer call 2469A), then draw-list submission to the bound presentation,
 * 22034 return, saved-group restoration 1BED9 and common animation 1B0F6
 * (stopping before 1E467 at caller 1BF5D).
 * Outdoor maps (F608!=0) follow 1B7F1 effects, 2126C monsters, 219E4
 * objects, 1B9B6 shuffle, 4E33F, 246CD terrain/walls, the 2538B list-1166
 * submission, 25390 party scan and 1BAA2 restoration into the same 1B0F6.
 * Retains completed effects and stops at the next missing scene
 * boundary; never unlocks input. No retry after partial map-query failure. */
xeen_status xeen_game_resume_scene(xeen_game *);
/* Continues a continuous session stopped for a fight: at COMBAT_CONTACT_BOUNDARY (any encounter's contact, 1C17E)
 * the fight begins through the verified combat prefix to its first input poll 3912C, one nesting level pushed (the
 * levels exhausted: XEEN_SERVICE_REQUIRED, an unsupported command); at COMBAT_CLEANUP_TAIL_BOUNDARY, after the
 * cleanup's own event, the fight's tail goes on (3957C). XEEN_BAD_INPUT in any other state. */
xeen_status xeen_game_resume_combat(xeen_game *);
/* Opt-in connected world-entry tail, after the diagnostic map/party resource
 * preparation. Starts original scene(0), then world-entry UI and scene(1).
 * Provider is copied; its user object remains borrowed until destroy. This
 * does not turn diagnostic placement into original saved-world startup. */
int xeen_game_start_world_entry(xeen_game *, const xeen_input_provider *);
xeen_status xeen_game_resume_world_entry(xeen_game *);
/* Caller 17B09/17B12 and retained mouse-release 18A22 can poll. A command is dispatched through the same
 * movement/clock/post-action path, and scenes retain their explicit resumes. */
xeen_status xeen_game_poll_input(xeen_game *, xeen_timer_bios, void *);
/* Resume an automatic event-tile entry or retained caller response. */
xeen_status xeen_game_resume_event(xeen_game *);
/* Continues an outdoor step off the map's edge stopped at its 17334 boundary
 * (XEEN_SURFACE_EDGE; 17334 -> 19D94, either world). An edge with no linked
 * map wraps on the same map (only a Shoot's or a spell's tail gets there).
 * Otherwise the source map is first persisted into a private original-layout
 * archive generation, then the destination DAT/EVT/MOB/entities/sprites are
 * staged and committed to this same game object, and the step's post-action
 * continuation (or its damage scene) follows. After a tail's edge (either
 * case) the edge's damage, a defeat or the command's tail follows instead
 * (game_transition.c edge_step_tail). XR_BAD_INPUT unless stopped there,
 * outdoors, with nothing pending. Failure leaves the source owners intact and
 * input locked at the transition boundary. */
xeen_resource_status xeen_game_resume_transition(xeen_game *);
void xeen_game_destroy(xeen_game *);
xeen_game_view xeen_game_inspect(const xeen_game *);
/*
 * What the game is doing at the boundary it reports (xeen_game_view.lifecycle_boundary) while the host holds it:
 * the host picks its resume call, and decides whether an unchanged boundary is a wait or a stall, from this kind
 * rather than from the original addresses (which stay what the game reports and what traces name). A boundary no
 * kind claims is XEEN_SITE_COMPUTE: a compute stage the scene resume continues. Phase 262.
 */
typedef enum xeen_site_kind {
    XEEN_SITE_COMPUTE = 0,       /* a compute stage: the scene, a step of a command */
    XEEN_SITE_MENU_ENTRY,        /* the title-screen main menu begins (program start, after a teardown) */
    XEEN_SITE_MENU_POLL,         /* a key poll of the title, the main menu, its save dialog, Other Options, credits */
    XEEN_SITE_DIALOG_POLL,       /* a key poll of a dialog (control panel, town, items, spells, views, the map) */
    XEEN_SITE_LOCATION_VOICE,    /* a location's goodbye or "come" voice, until it has played */
    XEEN_SITE_INPUT_POLL,        /* the command poll, its idle wait, a click's release, combat's command poll */
    XEEN_SITE_EVENT,             /* a step event, a redispatched command, Space's wait */
    XEEN_SITE_WORLD_ENTRY,       /* the world entry's finish */
    XEEN_SITE_TIMER_MARK,        /* the frame or scene timer is marked */
    XEEN_SITE_FRAME_WAIT,        /* the scene's frame wait */
    XEEN_SITE_REST_WAIT,         /* the Rest dream's timed and voice waits */
    XEEN_SITE_ITEM_WAIT,         /* a treasure item line's wait */
    XEEN_SITE_LOOP_EXIT,         /* the exploration loop's exit, the game-over sequence's start */
    XEEN_SITE_EXIT_STAGE,        /* the death sequence, the book, a teardown's voice wait */
    XEEN_SITE_CUTSCENE,          /* a cutscene's wait, a talk's subtitle, a location talk's voice */
    XEEN_SITE_MAP_EDGE,          /* the party crosses a map edge */
    XEEN_SITE_COMBAT_CONTACT,    /* a contact fight begins */
    XEEN_SITE_COMBAT_CLEANUP,    /* the combat cleanup's tail */
    XEEN_SITE_COMBAT_MONSTER_TURN, /* a monster's turn combat could not continue natively */
    XEEN_SITE_DRIVER_EXIT,       /* the sound driver's fatal exit */
    XEEN_SITE_PROGRAM_EXIT,      /* the program's own exit */
    XEEN_SITE_XEEN_EXE_HANDOFF   /* the exec of XEEN.EXE */
} xeen_site_kind;
/* The kind of reported boundary BOUNDARY (a pure function of its value). */
xeen_site_kind xeen_game_site_kind(uint32_t boundary);
/* The Who-will prompt's key poll (a dialog poll): the host's page evidence marks it, since that pass presents only
 * its prompt (its scene stays unpresented, in the original as natively). */
#define XEEN_GAME_WHO_WILL_POLL 0x2fb8du
/* Explicit interaction supports original Space20 current-edge-zero path.
 * Navigation separately honors automatic metadata bit10 selection. Existing
 * row selector chooses x/y/facing/sequence; no event number is hardcoded. */
int xeen_game_interact(xeen_game *);
xeen_slice_navigation_result xeen_game_navigate(xeen_game *, uint16_t);
const xeen_ui_request *xeen_game_pending(const xeen_game *);
/* Bounded diagnostic sessions: explicit input or zero presentation token.
 * Connected sessions accept modal responses only through their input provider. */
int xeen_game_respond(xeen_game *, uint16_t);
/* Explicit host abandonment of unsupported/suspended test event session.
 * Preserves DS mutations; never claims original cleanup or successful event.
 * Has no effect after enabling lifecycle checking. */
void xeen_game_leave_event(xeen_game *);
uint16_t xeen_game_tile(const xeen_game *, unsigned, unsigned);
uint8_t xeen_game_metadata(const xeen_game *, unsigned, unsigned);
const uint8_t *xeen_game_ds(const xeen_game *);
/* add-on monster_difficulty (read-only views for the host's
 * overlay): the movement state (DS) and the loaded monster database
 * (xeen.mon or dark.mon, 60-byte rows) with its size; NULL without a game. */
const xeen_movement *xeen_game_movement(const xeen_game *);
const uint8_t *xeen_game_monster_database(const xeen_game *, size_t *size);
/* add-on spell_short_description (a read-only view for the host's
 * overlay): the Cast Spell dialog 3D8C1 while it is open (exploration or
 * combat): RECORD is the DS offset of the member whose spell it shows (the
 * dialog's caster; while its New list, 3F05D mode 1, is open, the list's
 * member), LIST_OPEN whether that list is open, LIST_INDEX the highlighted
 * line's index into the class's spell list (the +A4 domain; -1: no line or
 * no list). 0 without a game or an open dialog. */
typedef struct {
    uint16_t record;
    int list_open;
    int list_index;
} xeen_game_cast_view;
int xeen_game_cast(const xeen_game *, xeen_game_cast_view *);
/* add-on spell_short_description: the casting group whose spell list class CLS casts from (0 the clerical, 1 the
 * sorcerous, 2 the druidic list of DS:201E; the game's own choice, 3DA0F). */
unsigned xeen_game_class_spell_list(uint8_t cls);
/* add-on mouse_scroll (a read-only view for the host's input): the
 * list the game is waiting on now, at that dialog's own key poll, whose
 * original Up and Down keys (F2h, F3h) scroll it, with its window's RECT
 * (x, y, width, height): the Cast dialog's New list and the guild's Buy
 * Spells and Spell Info (3F05D, window 19h, poll 3F415), the Quests panel
 * (3E24F, window 1Eh, poll 3E8A3), a character's Awards (41581, window 1Eh,
 * poll 41914) and the inn's roster (43E0B, window 0Bh, poll 441D3). 0 at
 * every other point. */
enum {
    XEEN_SCROLL_CAST_NEW = 1,
    XEEN_SCROLL_GUILD_BUY,
    XEEN_SCROLL_SPELL_INFO,
    XEEN_SCROLL_QUESTS,
    XEEN_SCROLL_AWARDS,
    XEEN_SCROLL_INN
};
int xeen_game_scroll_list(const xeen_game *, uint16_t rect[4]);
/* add-on character_details (a read-only view for the host): while
 * the character view's stat details popup (428F1, window 1Ch) waits at its
 * poll (43183): the stat (0..19, the view's cursor), the member's record,
 * the window's RECT, its text (DS AC0F), the pixels 1871B saved under it
 * (row stride 320, valid inside RECT; NULL when none), the displayed page,
 * and the 4E38C carried colour index and background the text was drawn
 * with. 0 at every other point. */
typedef struct {
    unsigned stat;
    uint16_t record;
    uint16_t rect[4];
    const char *text;
    const uint8_t *under, *displayed;
    uint8_t colour, background;
} xeen_game_details_view;
int xeen_game_details(const xeen_game *, xeen_game_details_view *);
/* The driver's fatal-exit message (4AD19 -> 505E3: text mode, the
 * message in red on row 0, DOS exit code 2) once the session ended there
 * (boundary 4AD19, XGC_ORIGINAL_EXIT), else NULL. */
const char *xeen_game_driver_exit(const xeen_game *);
/* The program's own exit 2C80D (184DE's "Exit to DOS!"): the
 * message 11E25 prints after text mode ("Thank you for playing Might and
 * Magic: World of Xeen.") once the session ended there (184DE's Exit, or
 * the control panel's Quit 29B4D; boundary 2C80D,
 * XGC_ORIGINAL_EXIT), else NULL; the DOS exit code is DS F606's low byte
 * (1061F(F606)). */
const char *xeen_game_program_exit(const xeen_game *);
/* The XEEN.EXE hand-off at 123A1 (16241, the program's exec of
 * "xeen.exe" after 1617E's teardown): the XEENCOMMAND string handed to
 * XEEN.EXE through the environment (145D4), or NULL when the session is not
 * there; its program name; whether xeen.cfg was written in the save
 * directory. XEEN.EXE itself is not run (the final project phase). */
const char *xeen_game_handoff(const xeen_game *);
const char *xeen_game_handoff_program(const xeen_game *);
int xeen_game_handoff_cfg(const xeen_game *);
/* The CD-side requests (47F7F from 19CC3's 19D3C: the world CBW
 * differs from F61A) the session made, and the last request's world. 47F7F
 * is the CD platform's (the native has both worlds' data: no swap, no DS
 * write); counted only, from the speech toggle's close 29C5B. */
unsigned long xeen_game_cd_side_requests(const xeen_game *, uint8_t *world);
/* The DS as the latest modal key poll found it (before its key). */
const uint8_t *xeen_game_modal_poll_ds(const xeen_game *);
/* Current map's persistent EVT handle-store image. Borrowed until destroy. */
const uint8_t *xeen_game_event_store(const xeen_game *, size_t *);
/* The inn's 19953 roster image (30 records of 162h; NULL outside
 * the inn and creation), for evidence and tests. */
const uint8_t *xeen_game_roster(const xeen_game *, size_t *);
/* Active-world original-layout archive generation. It equals the borrowed
 * input until the first successful transition, then names the private clone
 * containing transition-time source-map persistence. */
const uint8_t *xeen_game_state_archive(const xeen_game *, size_t *);
#endif
