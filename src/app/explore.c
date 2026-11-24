#define _POSIX_C_SOURCE 200809L
#include "xeen/ds_globals.h"
#include "xeen/party.h"
#include "xeen/byte_order.h"
#include "xeen/character_record.h"
#include "xeen/game.h"
#include "xeen/caller.h"
#include "xeen/scene.h"
#include "xeen/world_entry.h"
#include "xeen/post_action.h"
#include "xeen/program_data.h"
#include "xeen/wox_config.h"
#include "xeen/addons.h"
#include "xeen/bugfixes.h" /* owner-approved bug fixes (not original; off by default) */
#include "explore_options.h"
#include "host_files.h"
#include "host_iso.h"
#include "host_evidence.h"
#include "host_log.h"
#include "stop_diagnostics.h"
#include "host_startup.h"
#include "xeen_wox_version.h" /* XEEN_WOX_VERSION: generated from versions.cmake (Phase 338) */
/* Owner-approved add-ons (NOT original behavior; off unless --addon NAME or
 * wox.cfg [addons] NAME = on): one state for the program, bound to every
 * game object (a Load's new session keeps it). */
static xeen_addons host_addons;
/* add-on damage_numbers: the page a window shows (the page itself
 * while the add-on is off or shows nothing); headless uses its fixed clock. */
static int dn_headless;
#define DN_PAGE(page, pal)                                                                       \
    (xeen_damage_numbers_on                                                                      \
             ? xeen_damage_numbers_page((page), (pal), dn_headless ? 0 : xeen_platform_now_ns()) \
             : (page))
/* add-on monster_difficulty: its icons first, the damage numbers
 * over them (each the page itself while off or showing nothing). */
#define MD_PAGE(page) (xeen_monster_difficulty_on ? xeen_monster_difficulty_page(page) : (page))
/* add-on spell_short_description: its box first (outside the 3D
 * view and the portraits), then the icons and the numbers. */
#define SSD_PAGE(page) \
    (xeen_spell_short_description_on ? xeen_spell_short_description_page(page) : (page))
/* add-on town_icons: its icons on the map and the minimap first. */
#define TI_PAGE(page) (xeen_town_icons_on ? xeen_town_icons_page(page) : (page))
/* add-on wizard_eye_plus: its minimap first (under town_icons' icons). */
#define WEP_PAGE(page) (xeen_wizard_eye_plus_on ? xeen_wizard_eye_plus_page(page) : (page))
/* add-on character_details: its windows in the character view first. */
#define CD_PAGE(page) (xeen_character_details_on ? xeen_character_details_page(page) : (page))
/* add-on quest_journal: its numbers or its window over the Quests dialog first. */
#define QJ_PAGE(page) (xeen_quest_journal_on ? xeen_quest_journal_page(page) : (page))
/* add-on world_map: over everything (its button, or the whole screen). */
#define WM_PAGE(page) (xeen_world_map_on ? xeen_world_map_page(page) : (page))
/** What a window shows: PAGE after the add-ons' overlays (PAGE itself while every add-on is off or draws nothing),
 * observed by the shown-page capture (XEEN_HOST_PAGE_CAPTURE, headless; host_shown_page returns it unchanged). */
static const uint8_t *host_page(const uint8_t *page, const uint8_t *pal)
{
    return host_shown_page(page, WM_PAGE(DN_PAGE(MD_PAGE(SSD_PAGE(TI_PAGE(WEP_PAGE(CD_PAGE(QJ_PAGE(page)))))), pal)),
                           pal);
}
#define HOST_PAGE(page, pal) host_page((page), (pal))
#include <stdio.h>
/* Phase 290 (owner-approved host start-up): a window's SC-55-backed audio opens after the window, once the engine's
 * boot (xeen_audio_sc55_boot_start) has finished; XEEN.CFG's image for that open is kept until then. */
static int audio_deferred;
static uint8_t *audio_deferred_cfg;
static size_t audio_deferred_cfg_size;
#if defined(XEEN_HAVE_AUDIO)
#include "xeen/audio_engine.h"
#include <sys/stat.h>
/* The window's native audio engine (none headless). */
static xeen_audio_engine *audio_engine;
static xeen_audio_sc55_boot *sc55_boot; /* Phase 290: the window's SC-55 boot (start_deferred_audio) */
/* --audio-capture FILE logs every sink request
 * of a run (headless included: the engine is offline, no device) and forwards
 * it; the game cannot tell (the sink only observes). */
static FILE *audio_log;
/* INTRO.CC as the audio engine's driver archive for canmus (XEEN.DAT loads its drivers from INTRO.CC, 15E12); NULL
 * without INTRO.CC (then music_override = sc55 has no music). It borrows the host's INTRO.CC bytes, read at the engine's
 * open only. */
#if defined(XEEN_HAVE_AUDIO)
static xeen_archive audio_intro_archive;
static const xeen_archive *audio_intro;
#endif
static const xeen_audio_sink *audio_inner;
static unsigned long audio_seq;
/** 32-bit FNV-1a hash of the N bytes at B (identifies audio payloads in the audio log). */
static unsigned long fnv1a_hash(const uint8_t *b, size_t n)
{
    unsigned long h = 2166136261ul;
    for (size_t i = 0; i < n; ++i) {
        h = ((h ^ b[i]) * 16777619ul) & 0xfffffffful;
    }
    return h;
}
/** Audio-log sink: records a sound effect and passes it to the real sink. */
static void cap_effect(void *u, uint16_t n)
{
    fprintf(audio_log, "%lu effect %u\n", ++audio_seq, (unsigned)n);
    audio_inner->effect(u, n);
}
/** Audio-log sink: records a voice payload (size and hash) and passes it to the real sink. */
static void cap_voice(void *u, const uint8_t *b, size_t n)
{
    fprintf(audio_log, "%lu voice bytes=%zu fnv=%08lx\n", ++audio_seq, n, fnv1a_hash(b, n));
    audio_inner->voice(u, b, n);
}
/** Audio-log sink: records a voice by name and archive and passes it to the real sink. */
static void cap_voice_name(void *u, const char *name, unsigned cc)
{
    fprintf(audio_log, "%lu voice-name %s cc=%u\n", ++audio_seq, name, cc);
    audio_inner->voice_name(u, name, cc);
}
/** Audio-log sink: records a song payload (size and hash) and passes it to the real sink. */
static void cap_song(void *u, const uint8_t *b, size_t n)
{
    fprintf(audio_log, "%lu song bytes=%zu fnv=%08lx\n", ++audio_seq, n, fnv1a_hash(b, n));
    audio_inner->song(u, b, n);
}
/** Audio-log sink: records a song-buffer payload (size and hash) and passes it to the real sink. */
static void cap_buffer(void *u, const uint8_t *b, size_t n)
{
    fprintf(audio_log, "%lu song-buffer bytes=%zu fnv=%08lx\n", ++audio_seq, n, fnv1a_hash(b, n));
    audio_inner->song_buffer(u, b, n);
}
/** Audio-log sink: records the start of the buffered song and passes it to the real sink. */
static void cap_buffer_start(void *u)
{
    fprintf(audio_log, "%lu song-buffer-start\n", ++audio_seq);
    audio_inner->song_buffer_start(u);
}
/** Audio-log sink: records a music command and passes it to the real sink. */
static void cap_music(void *u, unsigned v)
{
    fprintf(audio_log, "%lu music %u\n", ++audio_seq, v);
    audio_inner->music(u, v);
}
/** Audio-log sink: records a volume change and passes it to the real sink. */
static void cap_volume(void *u, unsigned v)
{
    fprintf(audio_log, "%lu volume %u\n", ++audio_seq, v);
    audio_inner->volume(u, v);
}
/* The CD voice requests, with the offline engine's outcome (the
 * play is applied at once offline: the file opened and sought, or the error). */
static void cap_cd_play(void *u, unsigned disc, unsigned track, int32_t from, int32_t to)
{
    xeen_audio_engine_stats a, b;
    char name[16] = "-";
    xeen_audio_engine_get_stats(audio_engine, &a);
    audio_inner->cd_play(u, disc, track, from, to);
    xeen_audio_engine_get_stats(audio_engine, &b);
    (void)xeen_cd_track_file(disc, track, name, sizeof name);
    fprintf(audio_log, "%lu cd-play disc=%u track=%u from=%ld to=%ld file=%s %s%s\n", ++audio_seq,
            disc + 1u, track, (long)from, (long)to, name,
            b.cd_errors != a.cd_errors ? "error " : "ok",
            b.cd_errors != a.cd_errors ? xeen_audio_engine_cd_error(audio_engine) : "");
}
/** Audio-log sink: records a CD stop and passes it to the real sink. */
static void cap_cd_stop(void *u)
{
    fprintf(audio_log, "%lu cd-stop\n", ++audio_seq);
    audio_inner->cd_stop(u);
}
/** Audio-log sink: records a voice stop and passes it to the real sink when it has one. */
static void cap_voice_stop(void *u)
{
    fprintf(audio_log, "%lu voice-stop\n", ++audio_seq);
    if (audio_inner->voice_stop) {
        audio_inner->voice_stop(u);
    }
}
static void cap_music_restart(void *u)
{
    fprintf(audio_log, "%lu music-restart\n", ++audio_seq);
    if (audio_inner->music_restart) {
        audio_inner->music_restart(u);
    }
}
static xeen_audio_sink audio_capture_sink = {
        cap_effect, cap_voice,  cap_voice_name, cap_song,    cap_buffer, cap_buffer_start,
        cap_music,  cap_volume, cap_cd_play,    cap_cd_stop, NULL,       cap_voice_stop, cap_music_restart};
/* The CD rips' directory. An explicit [files] cd_tracks_dir is used as given; auto is music beside wox.cfg only (Phase
 * 331, owner decision: the former search inside and beside the game's resource directory and the $XEEN_CD_TRACKS
 * override are gone). NULL: auto names no directory (CD voice then fails, reported, never substituted). */
static const char *cd_tracks_dir(const xeen_wox_config *w)
{
    struct stat st;
    if (!w->cd_tracks_auto || (!stat(w->cd_tracks, &st) && S_ISDIR(st.st_mode))) {
        return w->cd_tracks;
    }
    return NULL;
}
/**
 * Returns the session's audio sink: the audio engine's, wrapped by the logging sink when an audio
 * log is open; NULL without an audio engine.
 */
static const xeen_audio_sink *session_sink(void)
{
    if (!audio_engine) {
        return NULL;
    }
    if (!audio_log) {
        return xeen_audio_engine_sink(audio_engine);
    }
    audio_inner = xeen_audio_engine_sink(audio_engine);
    audio_capture_sink.context = audio_inner->context;
    return &audio_capture_sink;
}
#endif
#include "xeen/save.h"
#include "xeen/raster.h"
#include "xeen/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
/* Platform adapter only: every command enters the existing input provider and
 * game continuation. Headless replay supplies explicit time samples. */
typedef struct {
    xeen_entity_rng state;
    unsigned calls; /* draws so far */
    unsigned limit; /* the inclusive bound of each answer (raw % (limit + 1)): 7 while the saved map's monsters are
                     * installed (their frames 0..7), else 65535, the raw word */
    int deterministic; /* 1: every draw uses replay_time, never the host's clock */
    xeen_rng_time replay_time;
    int carry; /* the carry of the last draw, read by the raster's colour modes 2 and 5 (random_carry) */
} random_source;
/**
 * RNG time source of the session: draws with the replay's fixed DOS time when one is set, else with
 * the host's local time of day; answers the draw bounded by the source's limit and keeps its carry.
 */
static uint16_t random_draw(void *u, uint16_t incoming)
{
    random_source *r = u;
    unsigned h = r->replay_time.hour, m = r->replay_time.minute, s = r->replay_time.second,
             cs = r->replay_time.hundredth;
#ifdef _WIN32
    /* The local time of day (the Windows C runtime lacks timespec_get). */
    if (!r->deterministic) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        h = t.wHour;
        m = t.wMinute;
        s = t.wSecond;
        cs = (unsigned)t.wMilliseconds / 10u;
    }
#else
    struct timespec ts;
    struct tm *parts;
    time_t seconds;
    if (!r->deterministic && timespec_get(&ts, TIME_UTC) == TIME_UTC) {
        seconds = ts.tv_sec;
        parts = localtime(&seconds);
        if (parts) {
            h = (unsigned)parts->tm_hour;
            m = (unsigned)parts->tm_min;
            s = (unsigned)parts->tm_sec;
        }
        cs = (unsigned)(ts.tv_nsec / 10000000L);
    }
#endif
    ++r->calls;
    {
        xeen_rng_transition t;
        uint16_t raw = xeen_entity_rng_draw(
                &r->state, incoming,
                (xeen_rng_time){(uint8_t)h, (uint8_t)m, (uint8_t)s, (uint8_t)cs}, &t);
        r->carry = xeen_entity_rng_carry(&t);
        return (uint16_t)(raw % (r->limit + 1u));
    }
}
/*
 * Phase 271 (owner-requested diagnostic): the CC files the host read (XEEN.CC, DARK.CC, INTRO.CC) by their
 * storage, so a member the game could not find is named with the file it was looked up in.
 */
static struct {
    const uint8_t *bytes;
    size_t size;
    char path[1200]; /* Phase 287: host_cc_set where */
} cc_files[3];
/** Registers BYTES (SIZE) as the CC file at WHERE (a path, or an image's member; slot I of cc_files). */
static void register_cc_file(unsigned i, const uint8_t *bytes, size_t size, const char *where)
{
    cc_files[i].bytes = bytes;
    cc_files[i].size = size;
    snprintf(cc_files[i].path, sizeof cc_files[i].path, "%s", where); /* Phase 287: a file or an image's member */
}
/**
 * When a lookup missed a member of a registered CC file (the one kept at the game's stop STOP, else the last since
 * xeen_archive_miss_clear): 1 with the member's name (or "the member with id NNNN") in MEMBER and the file's path in
 * *ARCHIVE; else 0. A diagnostic: the game's own handling of the miss is unchanged.
 */
static int cc_miss_lookup(uint32_t stop, char member[48], const char **archive)
{
    xeen_archive_miss m;
    unsigned i;
    if (!(stop && xeen_archive_kept_miss(stop, &m)) && !xeen_archive_last_miss(&m)) {
        return 0;
    }
    for (i = 0; i < 3u; i++) {
        if (cc_files[i].bytes && m.bytes >= cc_files[i].bytes &&
            m.bytes < cc_files[i].bytes + cc_files[i].size) {
            if (m.name[0]) {
                snprintf(member, 48, "%s", m.name);
            } else {
                snprintf(member, 48, "the member with id %04X", (unsigned)m.id);
            }
            *archive = cc_files[i].path;
            return 1;
        }
    }
    return 0;
}
/** Prints "Cannot read MEMBER from FILE (possible floppy/CD-ROM version mismatch)" for cc_miss_lookup(STOP). */
static void report_cc_miss(uint32_t stop)
{
    char member[48];
    const char *archive;
    if (cc_miss_lookup(stop, member, &archive)) {
        host_report(HOST_ERROR, "Cannot read %s from %s (possible floppy/CD-ROM version mismatch)", member, archive);
    }
}
/* The carry of the last draw (raster colour modes 2 and 5). */
static int random_carry(void *u)
{
    return ((random_source *)u)->carry;
}
/**
 * Extracts member NAME of archive A into a new buffer (owned by the caller); returns NULL when it
 * is missing.
 */
static uint8_t *extract_resource(const xeen_archive *a, const char *name, size_t *n)
{
    xeen_resource_info i;
    uint8_t *b;
    if (xeen_archive_find_name(a, name, &i) != XR_OK) {
        return NULL;
    }
    b = malloc(i.size);
    if (!b) {
        return NULL;
    }
    if (xeen_archive_extract(a, XR_BANK_ASSETS_XOR35, &i, b, i.size, n) != XR_OK) {
        free(b);
        return NULL;
    }
    return b;
}
/* A key at a control-panel poll: letters, digits and space are their keys,
 * '.' Enter, '!' the game's Esc, '<' Backspace; the other
 * punctuation is its own character. */
static int panel_key(char ch, xeen_native_key *key)
{
    static const xeen_native_key letters[26] = {
            XKEY_A, XKEY_B, XKEY_C, XKEY_D, XKEY_E, XKEY_F, XKEY_G, XKEY_H, XKEY_I,
            XKEY_J, XKEY_K, XKEY_L, XKEY_M, XKEY_N, XKEY_O, XKEY_P, XKEY_Q, XKEY_R,
            XKEY_S, XKEY_T, XKEY_U, XKEY_V, XKEY_W, XKEY_X, XKEY_Y, XKEY_Z};
    static const xeen_native_key digits[10] = {XKEY_0, XKEY_1, XKEY_2, XKEY_3, XKEY_4,
                                               XKEY_5, XKEY_6, XKEY_7, XKEY_8, XKEY_9};
    if (ch >= 'A' && ch <= 'Z') {
        *key = letters[ch - 'A'];
    } else if (ch >= '0' && ch <= '9') {
        *key = digits[ch - '0'];
    } else if (ch == ' ') {
        *key = XKEY_SPACE;
    } else if (ch == '.') {
        *key = XKEY_ENTER;
    } else if (ch == '!') {
        *key = XKEY_GAME_ESCAPE;
    } else if (ch == '<') {
        *key = XKEY_BACKSPACE;
    } else if (ch >= 'a' && ch <= 'z') {
        *key = (xeen_native_key)(XKEY_LOWER_FIRST + (ch - 'a')); /* A button's code */
    } else {
        return xeen_input_punctuation_key((unsigned char)ch, key);
    }
    return 1;
}
/* --frontend-input sends the replay's keys through the window
 * backends' key-event entry (a press and its release), not the headless
 * queue: the frontend's translation and queueing against the same route. */
static int frontend_input;
/* --boss-each-key puts an F10 boss-screen
 * round trip before every replay key: F10 pressed, repeated, released; a
 * dismissing A pressed, repeated, released (all consumed); then the key. A
 * route must stay byte-identical with it. */
static int boss_each_key;
/* --ctrl-q-keys N[b][m],... sends a Ctrl+Q
 * through the window key path before the Nth replay key (any context): its
 * press, an auto-repeat and its release; 'm' with other modifiers held
 * (Shift/Alt/Super: xeen_platform_key_event_modified's flag); 'b' from the F10
 * calculator (F10 first; the game thread's decision is taken at once by a
 * pump). A close leaves the replay key unsent. Implies --frontend-input. */
static const char *ctrl_q_keys;
static unsigned long frontend_keys;
/**
 * Delivers the --ctrl-q-keys Ctrl+Q listed for host key N, if any (the list is parsed up to N's entry).
 * Returns 0 to go on and send key N: no entry for N, or its Ctrl+Q was delivered (a 'b' entry's
 * calculator dismissed without closing the window). Returns 1 when a 'b' entry's Ctrl+Q closed the
 * window: key N is not sent. Returns -1 for a malformed entry (a suffix other than 'b' or 'm') or a
 * calculator that did not open or did not close. A plain entry's close is not detected here.
 */
static int ctrl_q_before(xeen_platform *platform, unsigned long n)
{
    const char *c = ctrl_q_keys;
    const xeen_native_key ctrl_q = (xeen_native_key)(XKEY_Q | XKEY_CTRL);
    while (c && *c) {
        char *end;
        unsigned long k = strtoul(c, &end, 10);
        int boss = 0, mods = 0;
        for (; *end && *end != ','; ++end) {
            if (*end == 'b') {
                boss = 1;
            } else if (*end == 'm') {
                mods = 1;
            } else {
                return -1;
            }
        }
        if (k == n) {
            xeen_platform_boss_info info;
            if (boss) {
                (void)xeen_platform_key_event(platform, XKEY_F10, 1, 0);
                (void)xeen_platform_key_event(platform, XKEY_F10, 0, 0);
                xeen_platform_boss_info_get(platform, &info);
                if (!info.active) {
                    return -1;
                }
            }
            (void)xeen_platform_key_event_modified(platform, ctrl_q, 1, 0, mods);
            (void)xeen_platform_key_event_modified(platform, ctrl_q, 1, 1, mods);
            (void)xeen_platform_key_event_modified(platform, ctrl_q, 0, 0, mods);
            if (boss) {
                xeen_platform_pump(platform);
                xeen_platform_boss_info_get(platform, &info);
                if (xeen_platform_should_close(platform)) {
                    return 1;
                }
                if (info.active) {
                    return -1;
                }
                printf("host ctrl-q calculator dismissed before key %lu\n", n);
            }
            return 0;
        }
        c = *end ? end + 1 : end;
    }
    return 0;
}
/**
 * Feeds one key to the platform frontend, delivering any scripted Ctrl+Q first; returns 0 when the
 * frontend refused it.
 */
static int frontend_key(xeen_platform *platform, xeen_native_key key)
{
    if (ctrl_q_keys) {
        int r = ctrl_q_before(platform, ++frontend_keys);
        if (r < 0) {
            return 0;
        }
        if (r > 0) {
            return 1;
        }
    }
    if (boss_each_key) {
        xeen_platform_boss_info info;
        (void)xeen_platform_key_event(platform, XKEY_F10, 1, 0);
        (void)xeen_platform_key_event(platform, XKEY_F10, 1, 1);
        (void)xeen_platform_key_event(platform, XKEY_F10, 0, 0);
        xeen_platform_boss_info_get(platform, &info);
        if (!info.active || strcmp(info.title, "Calculator")) {
            return 0;
        }
        (void)xeen_platform_key_event(platform, XKEY_A, 1, 0);
        (void)xeen_platform_key_event(platform, XKEY_A, 1, 1);
        (void)xeen_platform_key_event(platform, XKEY_A, 0, 0);
        xeen_platform_boss_info_get(platform, &info);
        if (info.active || strcmp(info.title, "WoX")) {
            return 0;
        }
    }
    return xeen_platform_key_event(platform, key, 1, 0) &&
           xeen_platform_key_event(platform, key, 0, 0);
}
/* The boss screen pauses the audio (and logs it in a capture). */
static void boss_audio(void *u, int active)
{
    (void)u;
#if defined(XEEN_HAVE_AUDIO)
    if (audio_engine) {
        xeen_audio_engine_pause(audio_engine, active);
    }
    if (audio_log) {
        fprintf(audio_log, "# boss-%s after %lu\n", active ? "pause" : "resume",
                audio_seq); /* no request number */
    }
#else
    (void)active;
#endif
}
/* --clicks: each '*' of the replay is the next listed window
 * pixel, sent through the backends' pointer entry (a motion, a left press
 * and its release); a point outside the image is sent and ignored there. */
static int clicks[64][2];
static unsigned click_count, click_next;
/** Delivers the next scripted mouse click to the platform; returns 0 when none is left. */
static int host_click(xeen_platform *platform)
{
    int x, y;
    if (click_next >= click_count) {
        return 0;
    }
    x = clicks[click_next][0];
    y = clicks[click_next][1];
    ++click_next;
    (void)xeen_platform_mouse_event(platform, x, y, 0, 0);
    (void)xeen_platform_mouse_event(platform, x, y, 1, 1);
    (void)xeen_platform_mouse_event(platform, x, y, 1, 0);
    return 1;
}
/** The replay's wheel notch (NOTCHES, +1 up or -1 down) at the next --clicks point; 0 when no point is left. A notch
 * the platform drops (the wheel is off: no wheel add-on) is no input, as a real wheel there. */
static int host_wheel(xeen_platform *platform, int notches)
{
    int x, y;
    if (click_next >= click_count) {
        return 0;
    }
    x = clicks[click_next][0];
    y = clicks[click_next][1];
    ++click_next;
    (void)xeen_platform_wheel_input(platform, x, y, notches);
    return 1;
}
/* A macro: the headless queue call stays in main (test fixtures hook it: tests/test_phase50_fixture.c redefines
 * xeen_platform_queue_key over the locals G and REPLAY of each call site). */
#define host_key(platform, key) \
    (frontend_input ? frontend_key((platform), (key)) : xeen_platform_queue_key((platform), (key)))
/*
 * Where the host continues the game. The game reports a lifecycle boundary whenever it stops; its kind
 * (xeen_game_site_kind) and the game's own stage decide which resume entry continues it.
 */
typedef enum host_route {
    ROUTE_SCENE,       /* xeen_game_resume_scene: every compute stage */
    ROUTE_MENU,        /* xeen_game_resume_menu */
    ROUTE_PANEL,       /* xeen_game_resume_panel: dialogs, a location's voice, a cutscene's waits */
    ROUTE_INPUT,       /* xeen_game_poll_input */
    ROUTE_EVENT,       /* xeen_game_resume_event */
    ROUTE_WORLD_ENTRY, /* xeen_game_resume_world_entry */
    ROUTE_TIMER,       /* xeen_game_resume_timer */
    ROUTE_FRAME_WAIT,  /* xeen_game_resume_frame_wait */
    ROUTE_REST_WAIT,   /* xeen_game_resume_rest_wait */
    ROUTE_ITEM_WAIT,   /* xeen_game_resume_item_wait */
    ROUTE_EXIT,        /* xeen_game_resume_exit: the game-over and teardown stages */
    ROUTE_TRANSITION,  /* xeen_game_resume_transition: a map edge */
    ROUTE_COMBAT       /* xeen_game_resume_combat: a contact fight, the cleanup's tail */
} host_route;
/* A menu stage owns the game until it returns (unless the game waits for input), then a dialog or a location's voice
 * the panel resumes, then the waiting phase; the other boundaries go by their kind. A cutscene's sites go to the
 * panel only while a cutscene runs. */
static host_route host_route_at(uint32_t b, const xeen_game_view *v)
{
    xeen_site_kind kind = xeen_game_site_kind(b);
    if ((v->menu_stage && v->phase != XG_WAITING) || kind == XEEN_SITE_MENU_ENTRY) {
        return ROUTE_MENU;
    }
    if (kind == XEEN_SITE_DIALOG_POLL || kind == XEEN_SITE_LOCATION_VOICE) {
        return ROUTE_PANEL;
    }
    if (v->phase == XG_WAITING || kind == XEEN_SITE_INPUT_POLL) {
        return ROUTE_INPUT;
    }
    switch (kind) {
        case XEEN_SITE_EVENT:
            return ROUTE_EVENT;
        case XEEN_SITE_WORLD_ENTRY:
            return ROUTE_WORLD_ENTRY;
        case XEEN_SITE_TIMER_MARK:
            return ROUTE_TIMER;
        case XEEN_SITE_FRAME_WAIT:
            return ROUTE_FRAME_WAIT;
        case XEEN_SITE_REST_WAIT:
            return ROUTE_REST_WAIT;
        case XEEN_SITE_ITEM_WAIT:
            return ROUTE_ITEM_WAIT;
        case XEEN_SITE_LOOP_EXIT:
        case XEEN_SITE_EXIT_STAGE:
            return ROUTE_EXIT;
        case XEEN_SITE_CUTSCENE:
            return v->cut_kind ? ROUTE_PANEL : ROUTE_SCENE;
        case XEEN_SITE_MAP_EDGE:
            return ROUTE_TRANSITION;
        case XEEN_SITE_COMBAT_CONTACT:
        case XEEN_SITE_COMBAT_CLEANUP:
            return ROUTE_COMBAT;
        default:
            return ROUTE_SCENE;
    }
}
/* Whether a step that ends where it began (the same boundary, a service required) is a wait rather than a stall:
 * a poll for a key (the input, menu and dialog polls) or a wait for time (the frame wait, the Rest dream, the exit
 * stages but the loop exit, a location's voice, the cutscenes' waits), or the game waiting for input. V is the
 * game after the step. */
static int host_waits_at(uint32_t b, const xeen_game_view *v)
{
    switch (xeen_game_site_kind(b)) {
        case XEEN_SITE_FRAME_WAIT:
        case XEEN_SITE_REST_WAIT:
        case XEEN_SITE_EXIT_STAGE:
        case XEEN_SITE_INPUT_POLL:
        case XEEN_SITE_MENU_POLL:
        case XEEN_SITE_DIALOG_POLL:
        case XEEN_SITE_LOCATION_VOICE:
        case XEEN_SITE_CUTSCENE:
            return 1;
        default:
            return v->phase == XG_WAITING;
    }
}
/* Phase 272: the session ended by the program's own exit or the XEEN.EXE hand-off (no fatal error, whatever its
 * exit status). */
static int host_clean_end;
/* The program's three ends, taken at a step's start (KIND the boundary's kind): the driver's fatal exit, the program's
 * own exit and the exec of XEEN.EXE. Each is reported (and the last two set host_clean_end); returns 1 when the session
 * ends here, 0 to go on with the step. */
static int program_ends(xeen_game *g, xeen_site_kind kind)
{
    /* The driver's fatal exit (4AD19 -> 505E3) ends the program. */
    if (kind == XEEN_SITE_DRIVER_EXIT && xeen_game_driver_exit(g)) {
        host_report(HOST_ERROR, "original driver exit at 4ad19: %s (DOS exit code 2)",
                xeen_game_driver_exit(g));
        report_cc_miss(0);
        return 1;
    }
    /* The program's own exit ("Exit to DOS!"): its message, DOS exit code F606. */
    if (kind == XEEN_SITE_PROGRAM_EXIT && xeen_game_program_exit(g)) {
        printf("%s\n", xeen_game_program_exit(g));
        host_report(HOST_INFO, "original program exit at 2c80d: %s (DOS exit code %u)",
                xeen_game_program_exit(g), (unsigned)xeen_game_ds(g)[0xf606]);
        host_clean_end = 1;
        return 1;
    }
    /* The program's exec of XEEN.EXE ends the session. */
    if (kind == XEEN_SITE_XEEN_EXE_HANDOFF && xeen_game_handoff(g)) {
        host_report(HOST_INFO, "xeen-exe handoff at 123a1: %s (%s) cfg=%d", xeen_game_handoff(g),
                xeen_game_handoff_program(g), xeen_game_handoff_cfg(g));
        host_clean_end = 1;
        return 1;
    }
    return 0;
}
/* Continues the game along ROUTE (host_route_at): the polls and waits take the platform's BIOS services, a map edge's
 * transition answers XR_OK as a service request; returns the game's status. */
static xeen_status resume_route(xeen_game *g, xeen_platform *platform, host_route route)
{
    xeen_timer_registers registers = {0, 0, 0, 2};
    switch (route) {
        case ROUTE_MENU:
            return xeen_game_resume_menu(g, xeen_platform_bios, platform);
        case ROUTE_PANEL:
            return xeen_game_resume_panel(g, xeen_platform_bios, platform);
        case ROUTE_INPUT:
            return xeen_game_poll_input(g, xeen_platform_bios, platform);
        case ROUTE_EVENT:
            return xeen_game_resume_event(g);
        case ROUTE_WORLD_ENTRY:
            return xeen_game_resume_world_entry(g);
        case ROUTE_TIMER:
            return xeen_game_resume_timer(g, &registers, xeen_platform_bios, platform);
        case ROUTE_FRAME_WAIT:
            return xeen_game_resume_frame_wait(g, xeen_platform_bios, platform);
        case ROUTE_REST_WAIT:
            return xeen_game_resume_rest_wait(g, xeen_platform_bios, platform);
        case ROUTE_ITEM_WAIT:
            return xeen_game_resume_item_wait(g, xeen_platform_bios, platform);
        case ROUTE_EXIT:
            return xeen_game_resume_exit(g, xeen_platform_bios, platform);
        case ROUTE_TRANSITION:
            return xeen_game_resume_transition(g) == XR_OK ? XEEN_SERVICE_REQUIRED : XEEN_BAD_INPUT;
        case ROUTE_COMBAT:
            return xeen_game_resume_combat(g);
        default:
            return xeen_game_resume_scene(g);
    }
}
/* A step from boundary B that ended the session (status S other than a service request, or a stall: the same
 * boundary, not a wait; V the game after the step). Phase 288: the player's sentence (the stop catalogue's, or a
 * missing CC member's own report), then the technical line with the address, status, next boundary and the stop's
 * names (stop_diagnostics.h). */
static void report_stop(uint32_t b, xeen_status s, const xeen_game_view *v)
{
    xeen_stop_report report = {b, v->lifecycle_boundary, (int)s, v->phase == XG_STOPPED, NULL, NULL};
    xeen_stop_diagnostic d;
    char member[48];
    const char *archive = NULL;
    if (cc_miss_lookup(b, member, &archive)) {
        report.member = member;
        report.archive = archive;
    }
    xeen_stop_diagnose(&report, &d);
    host_report(HOST_ERROR, "%s", d.message);
    host_report(HOST_ERROR, "%s", d.details);
}
/**
 * Runs the game for one host step: the program's ends first (program_ends), then the game resumed along its route
 * (resume_route; the scripted key or click was queued at its poll by the feeders); a step that fails or stalls is
 * reported (report_stop). Returns 0 when the session ends.
 */
static int run_host_step(xeen_game *g, xeen_platform *platform)
{
    xeen_game_view v = xeen_game_inspect(g);
    uint32_t b = v.lifecycle_boundary;
    xeen_status s;
    if (program_ends(g, xeen_game_site_kind(b))) {
        return 0;
    }
    xeen_archive_miss_clear(); /* the step's own lookups (report_cc_miss) */
    s = resume_route(g, platform, host_route_at(b, &v));
    v = xeen_game_inspect(g);
    if (s != XEEN_SERVICE_REQUIRED || (b == v.lifecycle_boundary && !host_waits_at(b, &v))) {
        report_stop(b, s, &v);
        return 0;
    }
    return 1;
}
/* The shared session start (program start and after a menu load): the
 * world entry and map preparation of a saved or loaded game, the silent
 * audio, presentation surfaces from the world's CC (back.raw, mm4.pal,
 * mouse.icn; 179D9 reloads them at world entry) and the world-entry tail.
 * A menu game (program start at 1660E) only binds its presentation. */
/* The platform whose mouse moves the cursor (a window, or a
 * headless run with --clicks); NULL keeps the cursor plane at the origin. */
static xeen_platform *pointer_platform;
static xeen_platform *clock_platform; /* The voice model's INT 1Ah service */
/* A window paints the cursor itself at the live
 * pointer (xeen_platform_set_cursor); wox.cfg [display] authentic_cursor keeps the
 * original's cadence (drawn into the page at each present only). */
static int authentic_cursor, host_cursor;
static const uint8_t no_cursor[32 * 32];
/** Pointer service: the platform's current mouse position. */
static void host_pointer(void *u, uint16_t *x, uint16_t *y)
{
    (void)xeen_platform_pointer(u, x, y);
}
/* The frames the original holds on screen inside one game step
 * (xeen_presentation display). A window shows each: a pressed button's
 * flips one VGA retrace apart (4E90A: 70 Hz), a combat-module scene (a
 * volley's projectile tier) until its 1C1B3 frame wait would end (2 BIOS
 * ticks past its 4EA35's tick). XEEN_DISPLAY_CAPTURE=DIR (headless) writes
 * those pages (display-NNNN-R.bin, R the reason; at most 400) instead:
 * offscreen evidence, nothing shown and no wait. Display only. */
typedef struct {
    xeen_platform *platform;
    uint64_t scene_start;
    const char *capture;
    unsigned captured;
    const xeen_presentation *presentation; /* Its mouse flag */
    const char *pointer_path;
    unsigned scale;
} display_state;
static display_state display_host;
/* 
 * the host loop's wall-clock costs: every pass of 20 ms or more (its
 * boundary, the step's and the display hook's hold time) and each hold of
 * 20 ms or more; at the end the whole session's distributions (median, p95,
 * worst), the window's pointer-only repaints and the longest run of passes
 * without the host's wait. Observation
 * only: it reads the monotonic clock and never touches the game, its clock
 * or its RNG. */
#define TIMING_BINS 2000u /* 0..9.99 ms in 10 us bins, then 10..10009 ms in 1 ms bins */
typedef struct {
    FILE *f;
    uint64_t hold_ns, pass_start, run_start, run_max, run_passes, run_passes_max;
    uint32_t steps[2 * TIMING_BINS], holds[2 * TIMING_BINS];
    uint64_t step_max, hold_max;
    unsigned long nsteps, nholds;
    unsigned long passes, spins;
} host_timing;
static host_timing timing;
/* 
 * XEEN_TEST_SKIP_INVALID_KEYS skips a replay key the current poll kind
 * (exploration, combat, modal) does not accept instead of ending the run,
 * so one seeded random sequence can cross combat entries and ends. */
static int skip_invalid;
/* Test interface (harness adapter modal-typeahead): queue each
 * modal key at the wait's first host visit, i.e. already buffered when its
 * first 189E3 poll runs (DOS type-ahead), instead of after four visits. */
static int modal_typeahead;
/**
 * Adds one frame time NS to a timing histogram (10-microsecond bins below 10 ms, 1 ms bins above).
 */
static void timing_add(uint32_t *bins, uint64_t *max, unsigned long *n, uint64_t ns)
{
    uint64_t us = ns / 1000u;
    unsigned b = us < 10000u ? (unsigned)(us / 10u)
                             : TIMING_BINS + (unsigned)((us - 10000u) / 1000u < TIMING_BINS - 1u
                                                                ? (us - 10000u) / 1000u
                                                                : TIMING_BINS - 1u);
    ++bins[b];
    ++*n;
    if (ns > *max) {
        *max = ns;
    }
}
/** Returns the lower bound in microseconds of timing histogram bin B. */
static unsigned long timing_bin_us(unsigned b)
{
    return b < TIMING_BINS ? (unsigned long)b * 10ul
                           : 10000ul + (unsigned long)(b - TIMING_BINS) * 1000ul;
}
/** Writes a timing histogram's count, median, 95th percentile and maximum to the timing log. */
static void timing_dist(const char *what, const uint32_t *bins, uint64_t max, unsigned long n)
{
    unsigned long seen = 0, p50 = 0, p95 = 0;
    unsigned b;
    int got50 = 0;
    if (!timing.f) {
        return;
    }
    for (b = 0; b < 2u * TIMING_BINS && n; ++b) {
        seen += bins[b];
        if (!got50 && seen * 2u >= n) {
            p50 = timing_bin_us(b);
            got50 = 1;
        }
        if (seen * 100u >= n * 95u) {
            p95 = timing_bin_us(b);
            break;
        }
    }
    fprintf(timing.f,
            "# %s n=%lu median_us=%lu p95_us=%lu max_us=%llu (bins: 10 us below 10 ms, 1 ms "
            "above)\n",
            what, n, p50, p95, (unsigned long long)(max / 1000u));
}
#define RETRACE_NS   UINT64_C(14285714)
#define BIOS_TICK_NS UINT64_C(54925439)
#define DISPLAY_CAPTURE_LIMIT 400u /* XEEN_DISPLAY_CAPTURE's pages per session */
#define DISPLAY_PALETTE_BYTES 768u /* 256 colours x R, G, B */
/*
 * Test interface: XEEN_TEST_POINTER_PATH=X,Y;X,Y;... moves the pointer to the next listed image point (logical
 * pixels, through the backends' pointer entry) before each display: the mouse moving during a fade. The last point
 * stays. Never set in normal play; nothing without a window.
 */
static void advance_pointer_path(display_state *d)
{
    char *end;
    long x, y = 0;
    if (!d->pointer_path || !*d->pointer_path || !pointer_platform) {
        return;
    }
    x = strtol(d->pointer_path, &end, 10);
    if (*end == ',') {
        d->pointer_path = end + 1;
        y = strtol(d->pointer_path, &end, 10);
    }
    d->pointer_path = *end == ';' ? end + 1 : end;
    (void)xeen_platform_mouse_event(pointer_platform, (int)(x * (long)d->scale), (int)(y * (long)d->scale), 0, 0);
}
/* XEEN_DISPLAY_CAPTURE=DIR: PAGE and PALETTE as DIR/display-NNNN-R.bin and .pal (NNNN the page's number, R the
 * reason), the first DISPLAY_CAPTURE_LIMIT pages only; a file that cannot be written is skipped. */
static void capture_display_page(display_state *d, unsigned reason, const uint8_t *page, const uint8_t *palette)
{
    char name[1200];
    FILE *f;
    if (!d->capture || d->captured >= DISPLAY_CAPTURE_LIMIT) {
        return;
    }
    snprintf(name, sizeof name, "%s/display-%04u-%u.bin", d->capture, d->captured++, reason);
    if ((f = fopen(name, "wb"))) {
        (void)fwrite(page, 1, XEEN_PRESENT_PIXELS, f);
        fclose(f);
    }
    snprintf(name, sizeof name, "%s/display-%04u-%u.pal", d->capture, d->captured - 1u, reason);
    if ((f = fopen(name, "wb"))) {
        (void)fwrite(palette, 1, DISPLAY_PALETTE_BYTES, f);
        fclose(f);
    }
}
/**
 * Display callback (display_state above): a scene's start only marks its time; every other page first moves the test
 * pointer (advance_pointer_path) and is captured (capture_display_page); headless, the damage_numbers overlay sees it
 * and nothing is shown. In a window the cursor follows the game's mouse flag and the page is held until the original's
 * frame ends: one VGA retrace for a button's flip, the Please Wait box's hold, else two BIOS ticks past the scene's
 * start; each hold is timed into the timing log when one is open.
 */
static void host_display(void *u, unsigned reason, const uint8_t *page, const uint8_t *palette)
{
    display_state *d = u;
    uint64_t now = xeen_platform_now_ns(), until;
    if (reason == XEEN_DISPLAY_SCENE_START) {
        d->scene_start = now;
        return;
    }
    advance_pointer_path(d);
    capture_display_page(d, reason, page, palette);
    /* add-on damage_numbers (developer evidence, headless with
     * XEEN_DISPLAY_CAPTURE only): every displayed frame passes the overlay. */
    if (!d->platform) {
        if (dn_headless) {
            (void)HOST_PAGE(page, palette);
        }
        return;
    }
    if (d->presentation) {
        xeen_platform_show_cursor(d->platform, d->presentation->enabled); /* CS:16BE */
    }
    until = reason == XEEN_DISPLAY_RETRACE    ? now + RETRACE_NS
            : reason == XEEN_DISPLAY_WAIT_BOX ? now + XEEN_DISPLAY_WAIT_BOX_MS * UINT64_C(1000000) /* Phase 310 */
                                              : (d->scene_start / BIOS_TICK_NS + 2u) * BIOS_TICK_NS;
    {
        uint64_t t0 = timing.f ? xeen_platform_now_ns() : 0;
        (void)xeen_platform_present_hold(d->platform, HOST_PAGE(page, palette), palette, until);
        if (timing.f) {
            uint64_t t = xeen_platform_now_ns() - t0;
            timing.hold_ns += t;
            timing_add(timing.holds, &timing.hold_max, &timing.nholds, t);
            if (t >= UINT64_C(20000000)) {
                fprintf(timing.f, "hold reason=%u us=%llu\n", reason,
                        (unsigned long long)(t / 1000u));
            }
        }
    }
}
/* The window waits (1 ms) only where the game waits for time or
 * input (polls, frame and timed waits, menus, panels, cutscenes); the
 * compute stages of a command (scene stages, events, the world entry, a
 * map transition, the combat entry, timer samples) run back to back, as the
 * original's code does between its waits. */
static int host_compute_at(uint32_t b, const xeen_game_view *v)
{
    if (v->phase == XG_WAITING || v->menu_stage) {
        return 0;
    }
    /* (A menu poll counts as a wait only through its menu stage.) */
    switch (xeen_game_site_kind(b)) {
        case XEEN_SITE_MENU_ENTRY:
        case XEEN_SITE_DIALOG_POLL:
        case XEEN_SITE_INPUT_POLL:
        case XEEN_SITE_FRAME_WAIT:
        case XEEN_SITE_REST_WAIT:
        case XEEN_SITE_ITEM_WAIT:
        case XEEN_SITE_LOOP_EXIT:
        case XEEN_SITE_EXIT_STAGE:
        case XEEN_SITE_LOCATION_VOICE:
            return 0;
        case XEEN_SITE_CUTSCENE:
            return !v->cut_kind;
        default:
            return 1;
    }
}
static int host_compute(xeen_game *g)
{
    xeen_game_view v = xeen_game_inspect(g);
    return host_compute_at(v.lifecycle_boundary, &v);
}
/* The CD drive's status model (xeen_game_set_cd_status). -1: the
 * default, the modeled drive in the window when the CD rips were found and no
 * device headless (the historical replays' platform); --cd-status model|none
 * or $XEEN_CD_STATUS (host test control) choose it. A headless run prints
 * every 47F3D query (the original-byte side's MSCDEX adapter takes its
 * clock from these lines). */
static int cd_status_mode = -1, cd_status_window;
/* Phase 286: how the music system was chosen (xeen_music_resolve), for the diagnostics and the audio capture; how
 * [audio] speech resolved (xeen_speech_resolve). */
static char music_resolution[300], speech_resolution[300];
static xeen_speech_resolution speech;
static FILE *cd_status_log;
/* The digital driver's voice status (xeen_game_set_voice_status).
 * -1: the default, the modelled driver when the window's audio engine plays
 * voices and no driver headless (the historical replays' silent driver);
 * --voice-status model|none or $XEEN_VOICE_STATUS (host test controls) choose
 * it. The trim follows XEEN.CFG's sound driver (blastsnd 0Eh, else 0Ah). A
 * headless run prints every modelled 187E9 call (the original-byte side's
 * voc-driver adapter takes its clock from these lines). */
static int voice_status_mode = -1, voice_status_window;
static unsigned voice_trim = 0x0a;
/* Phase 285A: the voice model's kind from XEEN.CFG's sound driver (47C1F's index; the switch at 15E52, table
 * 1564:0CDE): 3/4 nullsnd = none (its entry 3 answers 0: nothing plays, never busy); 8 specsnd = the PAS's rate;
 * 6 blastsnd, 7/10 prosnd = SB; 1 ibmsnd, 2 adsnd, 5 source, 9 covxsnd keep the SB approximation (labelled). */
static unsigned voice_kind = XEEN_VOICE_MODEL_SB;
/* Phase 287: the PC speaker's blocking start (XEEN_VOICE_MODEL_PCSPEAKER): whether the BIOS clock stands during it.
 * ibmsnd's interrupts call the music driver's timer (000Ch) M times (audio.h); every music driver then counts the
 * BIOS tick by its own accumulator (+= 4006h, the old INT 8 on each carry), except ibmmus (XEEN.CFG music 7, the PC
 * speaker's, the installer's pair of sound 1): with the word 0Fh = 2 it stops its song and returns without chaining
 * (12BF..12D8, xeen_dat_evidence/phase287/a/h_ibmmus.asm). */
static int voice_block_clock_stands, voice_block_log;
/**
 * The PC speaker's blocking voice start (the game's hold service): IRQS interrupts of 81 PIT cycles. Headless, the BIOS
 * clock advances by the ticks the music driver's chain counts in them (xeen_voc_pcspeaker_bios_ticks: its
 * accumulator's phase taken as 0, labelled; up to one more tick from another phase), none with ibmmus, and a "voice-block" line is printed; a window waits that long, the
 * clock standing with ibmmus.
 */
static void voice_block_hold(void *u, uint32_t irqs)
{
    uint64_t pit = (uint64_t)irqs * XEEN_VOC_PCSPEAKER_SAMPLE_PIT;
    uint64_t calls = xeen_voc_pcspeaker_music_calls(irqs);
    uint64_t ticks = xeen_voc_pcspeaker_bios_ticks(irqs, voice_block_clock_stands);
    xeen_platform *platform = u;
    if (voice_block_log) {
        printf("voice-block irqs=%lu pit=%llu music-calls=%llu bios-ticks=%llu\n", (unsigned long)irqs,
               (unsigned long long)pit, (unsigned long long)calls, (unsigned long long)ticks); /* Evidence */
    }
    xeen_platform_hold(platform, pit, ticks, voice_block_clock_stands);
}
/** Voice observer: writes a voice request to the CD status log. */
static void voice_status_print(void *u, const xeen_voice_request *q)
{
    (void)u;
    if (cd_status_log) {
        fprintf(cd_status_log,
                "voice-request n=%lu op=%s site=%05x now=%llu start=%llu samples=%lu gate=%d "
                "active=%d busy=%d\n",
                q->n,
                q->kind == XEEN_VOICE_QUERY   ? "query"
                : q->kind == XEEN_VOICE_START ? "start"
                                              : "stop",
                (unsigned)q->site, (unsigned long long)q->now, (unsigned long long)q->start,
                (unsigned long)q->samples, q->gate, q->active, q->busy);
    }
}
/** CD status observer: writes a CD play or stop request to the CD status log. */
static void cd_status_print(void *u, const xeen_cd_status_query *q)
{
    (void)u;
    if (cd_status_log && q->request) {
        fprintf(cd_status_log, "cd-request op=%s now=%llu frames=%lu\n",
                q->request == 1 ? "play" : "stop", (unsigned long long)q->now,
                (unsigned long)q->frames);
        return;
    }
    if (cd_status_log) {
        fprintf(cd_status_log,
                "cd-status n=%lu site=%05x now=%llu start=%llu frames=%lu active=%d busy=%d "
                "outstanding=%d\n",
                q->n, (unsigned)q->site, (unsigned long long)q->now, (unsigned long long)q->start,
                (unsigned long)q->frames, q->active, q->busy, q->outstanding);
    }
}
/**
 * What the host owns for the whole run, across every session a Load starts: the startup inputs
 * (load_inputs: the two saves and the two CC archives in FILES/SIZES, XEEN.CFG, INTRO.CC), the
 * presentation and its three 64000-byte planes (the page background, the cursor pixels and their
 * opacity), the three archives (the two worlds' CCs and the session's state archive), the sprite
 * archives and raster that start_session binds, the RNG, the platform's input provider, and the
 * save directory with its callbacks (SAVE_FILES points at DIRECTORY). main frees the buffers once,
 * at its end (free_host_resources).
 */
/* The host's four input files (load_inputs): the two worlds' saves (absent at a menu start) and archives. */
enum { HOST_CLOUDS_SAVE, HOST_DARK_SAVE, HOST_XEEN_CC, HOST_DARK_CC, HOST_FILES };
/* The host's archives: the two worlds' CCs (XEEN.CC, DARK.CC) and the session's state archive (start_session). */
enum { HOST_ARCHIVE_CLOUDS, HOST_ARCHIVE_DARK, HOST_ARCHIVE_STATE, HOST_ARCHIVES };
typedef struct host_resources {
    uint8_t *files[HOST_FILES];
    size_t sizes[HOST_FILES];
    uint8_t *config, *intro;
    size_t config_size, intro_size;
    xeen_presentation *p;
    uint8_t *background, *cursor, *opacity;
    xeen_archive archives[HOST_ARCHIVES];
    xeen_sprite_archives sprite_archives;
    xeen_raster raster;
    random_source random;
    xeen_input_provider provider;
    save_directory directory;
    xeen_save_files save_files;
} host_resources;
/**
 * A saved game's world before its first frame: the world entered, then (from DARK.CC) the map's objects, its
 * monsters (their random draws bounded by 7) and its sprites (bounded by 65535); 0 with the reason on stderr.
 */
static int prepare_saved_world(xeen_game *g, host_resources *res)
{
    uint8_t *cc = res->files[HOST_DARK_CC];
    size_t cc_size = res->sizes[HOST_DARK_CC];
    xeen_resource_status sprite_status;
    if (xeen_game_enter_world(g) != XR_OK || xeen_game_prepare_map_objects(g, cc, cc_size) != XR_OK) {
        host_report(HOST_ERROR, "unsupported saved map object initialization");
        return 0;
    }
    res->random.limit = 7;
    if (xeen_game_prepare_monsters(g, cc, cc_size, random_draw, &res->random, NULL) != XR_OK) {
        host_report(HOST_ERROR, "unsupported saved monster/database backing");
        return 0;
    }
    res->random.limit = 65535;
    sprite_status = xeen_game_prepare_map_sprites(g, cc, cc_size, random_draw, &res->random);
    if (sprite_status != XR_OK) {
        host_report(HOST_ERROR, "unsupported saved map sprite initialization (%d)", (int)sprite_status);
        return 0;
    }
    return 1;
}
/**
 * The session's status reporters and clock: the CD status model (the option's, else the window's: no device, no
 * lines), the voice status model with its trim (likewise), and the platform's BIOS clock.
 */
static void bind_status_models(xeen_game *g)
{
    int cd = cd_status_mode >= 0 ? cd_status_mode : cd_status_window;
    int voice = voice_status_mode >= 0 ? voice_status_mode : voice_status_window;
    xeen_game_set_cd_status(g, cd, cd ? cd_status_print : NULL, NULL);
    xeen_game_set_voice_status(g, voice ? (int)voice_kind : 0, voice_trim, voice ? voice_status_print : NULL, NULL);
    xeen_game_set_speech_off(g, speech.forced_off);
    xeen_game_set_clock_service(g, xeen_platform_bios, clock_platform);
    xeen_game_set_voice_block(g, voice ? voice_block_hold : NULL, clock_platform);
}
/**
 * With a pointer, the decoded cursor plane (mouse.icn's B, N bytes) is cut to frame 0's cell (its offset and size
 * from the header), moves with the pointer, and the cell's width and height bound the INT 33h range (4E0EA..4E10C);
 * a cell of at most 32x32 becomes the platform's own cursor (the pages then carry none). Without a pointer, or with
 * a cell past the screen, the plane stays full size at the origin.
 */
static void fit_pointer_cursor(xeen_presentation *p, const uint8_t *b, size_t n, uint8_t *cursor, uint8_t *opacity)
{
    unsigned cell, x0, w, y0, h, y;
    if (!pointer_platform || n < 10u) {
        return;
    }
    cell = xeen_get_le16(b + 2);
    if (cell + 8u > n) {
        return;
    }
    x0 = xeen_get_le16(b + cell);
    w = xeen_get_le16(b + cell + 2u);
    y0 = xeen_get_le16(b + cell + 4u);
    h = xeen_get_le16(b + cell + 6u);
    if (!w || !h || x0 + w > 320u || y0 + h > 200u) {
        return;
    }
    for (y = 0; y < y0 + h; ++y) {
        memmove(cursor + y * (x0 + w), cursor + y * 320u, x0 + w);
        memmove(opacity + y * (x0 + w), opacity + y * 320u, x0 + w);
    }
    p->cursor_width = (uint16_t)(x0 + w);
    p->cursor_height = (uint16_t)(y0 + h);
    p->cursor_size = (size_t)(x0 + w) * (y0 + h);
    xeen_platform_set_pointer_limits(pointer_platform, w, h);
    p->pointer = host_pointer;
    p->pointer_user = pointer_platform;
    if (host_cursor && x0 + w <= 32u && y0 + h <= 32u) {
        xeen_platform_set_cursor(pointer_platform, cursor, opacity, x0 + w, y0 + h);
        p->cursor_opacity = no_cursor; /* the pages carry no cursor */
    }
}
/**
 * The world's screen resources into the presentation P: back.raw (64,000 bytes) as the frame buffer, the mm4.pal
 * palette, the mouse.icn cursor (fit_pointer_cursor), and the display hooks when a window or a capture shows the
 * pages. Returns 0 on a missing or malformed resource.
 */
static int bind_screen(host_resources *res, unsigned world)
{
    xeen_presentation *p = res->p;
    size_t n = 0;
    uint8_t *b = extract_resource(&res->archives[world], "back.raw", &n);
    if (!b || n != XEEN_PRESENT_PIXELS) {
        free(b);
        return 0;
    }
    memcpy(res->background, b, XEEN_PRESENT_PIXELS);
    free(b);
    if (xeen_raster_palette(p, &res->archives[world], "mm4.pal") != XR_OK) {
        return 0;
    }
    xeen_presentation_trace("palload mm4.pal"); /* Evidence: the host's 179E9 188BB (CS:0C3A only) */
    b = extract_resource(&res->archives[world], "mouse.icn", &n);
    if (!b || xeen_raster_cursor(b, n, 0, res->cursor, res->opacity, XEEN_PRESENT_PIXELS) != XEEN_OK) {
        free(b);
        return 0;
    }
    /* The display driver's state belongs to the program, not the session: across a menu Load it carries over.
     * Only the first start (no page yet) sets the pending window to none and the mouse off; the menu keeps the
     * mouse off until the world entry turns it on, and a saved-game start enters as after a Load. (Resetting them
     * per session let a loaded session's first present copy the cursor's saved rectangles into the menu's page.)
     * Original: CS:C24 (FFFFh) and CS:16BE (0) in the image; the menu's 15647 4E231; the world entry's 17AA6
     * 4E06B; 1B503(0)'s present at 1BECF. */
    if (!p->framebuffer) {
        p->dirty = XEEN_PRESENT_DIRTY_NONE;
        p->enabled = 0;
    }
    p->framebuffer = res->background;
    p->framebuffer_size = XEEN_PRESENT_PIXELS;
    if (display_host.platform || display_host.capture) {
        p->display = host_display;
        p->display_user = &display_host;
    }
    display_host.presentation = p;
    p->cursor_pixels = res->cursor;
    p->cursor_opacity = res->opacity;
    p->cursor_size = XEEN_PRESENT_PIXELS;
    p->cursor_width = 320;
    p->cursor_height = 200;
    fit_pointer_cursor(p, b, n, res->cursor, res->opacity);
    free(b);
    return 1;
}
/**
 * Binds the session's archives, saves, presentation and audio (all from RES) to game G and starts it, either at the
 * main menu (MENU) or from the loaded saves (prepare_saved_world): the intro archive, the save files and the
 * add-ons; the random sources (a menu start's bound 65535); the audio and the status models (bind_status_models);
 * the state archive (a menu start reads DARK.CC itself); the screen (bind_screen) and the raster's sprite
 * archives; then the menu's input or the world entry. Returns 0 on failure.
 */
static int start_session(xeen_game *g, int menu, host_resources *res)
{
    xeen_presentation *p = res->p;
    xeen_archive *archives = res->archives;
    unsigned world = menu ? 1u : xeen_game_inspect(g).position.world; /* a menu start: DARK.CC is the state */
    size_t state_size = 0;
    const uint8_t *state;
    if (res->intro && !xeen_game_bind_intro_archive(g, res->intro, res->intro_size)) {
        host_report(HOST_ERROR, "intro.cc rejected");
        return 0;
    }
    if (!xeen_game_bind_save_files(g, &res->save_files)) {
        return 0;
    }
    xeen_game_bind_addons(g, &host_addons);
    if (!menu && !prepare_saved_world(g, res)) {
        return 0;
    }
    if (menu) {
        res->random.limit = 65535;
        if (!xeen_game_bind_random(g, random_draw, &res->random)) {
            return 0;
        }
    }
    if (!xeen_game_bind_random_carry(g, random_carry) || !xeen_game_enable_silent_audio(g)) {
        return 0;
    }
#if defined(XEEN_HAVE_AUDIO)
    if (audio_engine) {
        xeen_game_set_audio_sink(g, session_sink());
    }
#endif
    bind_status_models(g);
    state = menu ? res->files[HOST_DARK_CC] : xeen_game_state_archive(g, &state_size);
    if (menu) {
        state_size = res->sizes[HOST_DARK_CC];
    }
    if (!state || xeen_archive_open(&archives[HOST_ARCHIVE_STATE], state, state_size) != XR_OK ||
        !bind_screen(res, world)) {
        return 0;
    }
    res->sprite_archives = (xeen_sprite_archives){{&archives[HOST_ARCHIVE_CLOUDS], &archives[HOST_ARCHIVE_DARK]},
                                                    &archives[HOST_ARCHIVE_STATE]};
    res->raster = (xeen_raster){p, xeen_game_sprites(g), &res->sprite_archives, {0, 0}};
    p->raster_scene = xeen_raster_scene;
    p->raster_sprites = xeen_raster_sprites;
    p->raster_user = &res->raster;
    if (!xeen_game_bind_presentation(g, p)) {
        return 0;
    }
    return menu ? xeen_game_bind_menu_input(g, &res->provider) : xeen_game_start_world_entry(g, &res->provider);
}
/*
 * Whether the game is in the title menu's context (DS IN_MENU), where the key read treats Ctrl+Q as the menu's quit.
 * The flag is set when the menu's buttons open and cleared by the menu's exit into a game (a Load or a new game);
 * nothing else writes it. So it holds at the main menu, Start Game, Load Game, the credits, Other Options, their
 * dialogs and the title animation after Esc, and never in a game session nor at the program's first title (with
 * its save-pair question) before the buttons open. Read on the game's thread when its poll takes the key (or, for
 * the boss screen, while the game thread is held).
 * Original: 8C77, tested by 189E3's key read (18AA0..18AB3: 11h -> 2C80D); set at 16A28 (16996), cleared at 17024
 * in 16FAB.
 */
static int menu_context_active(void *u)
{
    xeen_game *const *g = u;
    const uint8_t *ds;
    if (!g || !*g || !(ds = xeen_game_ds(*g))) {
        return 0;
    }
    return ds[XEEN_DS_IN_MENU] != 0;
}
/* The host's button state for add-on map_movement's drag. */
static int addon_button_held(void *platform)
{
    return xeen_platform_button_held(platform);
}
/* The host's wheel for add-ons mouse_scroll and world_map. */
static int addon_wheel_take(void *platform, uint16_t *x, uint16_t *y)
{
    return xeen_platform_take_wheel(platform, x, y);
}
/*
 * The command line's host-wide switches into the host's state, after a successful parse: the input,
 * pointer and status callbacks read them and have no options record.
 */
static void adopt_host_switches(const explore_options *o)
{
    voice_status_mode = o->voice_status_mode;
    cd_status_mode = o->cd_status_mode;
    frontend_input = o->frontend_input;
    boss_each_key = o->boss_each_key;
    ctrl_q_keys = o->ctrl_q_keys;
    authentic_cursor = o->authentic_cursor;
    memcpy(clicks, o->clicks, sizeof clicks);
    click_count = o->click_count;
}
/**
 * Phase 287: the game's XEEN.CFG for all its readers (the menu start's configuration image, 47C1F, and the audio's
 * music_override = auto / speech = auto), from one place: the xeen_cfg_dir directory (auto: the data directory), as
 * XEEN.CFG or xeen.cfg, never the save directory. A headless developer run without an explicit location keeps its
 * model of the one DOS directory (the save directory overlay, then the data directory: the harness's installation
 * files). WHERE gets the file's path for messages. NULL when there is none (or it is empty).
 */
static uint8_t *read_xeen_cfg(const explore_options *opt, size_t *n, char *where, size_t where_size)
{
    char path[1100];
    if (!opt->headless || opt->xeen_cfg_explicit) {
        if (!host_dos_path(opt->xeen_cfg_dir, "XEEN.CFG", path, sizeof path)) {
            snprintf(where, where_size, "%s/XEEN.CFG", opt->xeen_cfg_dir);
            return NULL;
        }
        snprintf(where, where_size, "%s", path);
        return host_read_file(path, n);
    }
    {
        save_directory overlay = {.dir = opt->dir, .save_dir = opt->save_dir, .deterministic = 0, .clock = {0},
                                  .trace = 0, .cfg_dir = NULL};
        snprintf(where, where_size, "XEEN.CFG (the save directory, else %s)", opt->dir);
        return host_save_read(&overlay, "XEEN.CFG", n);
    }
}
/**
 * Reads the startup inputs: both worlds' saves (not for a --menu start), the two CC archives from the
 * resource directory, XEEN.CFG (a --menu start: read_xeen_cfg, 47C1F) and the optional INTRO.CC. Returns 0 after a missing save or archive (the host exits
 * with status 1); the buffers read so far stay in FILES/SIZES/CONFIG/INTRO for main's cleanup.
 */
static int load_inputs(const explore_options *opt, uint8_t *files[HOST_FILES], size_t sizes[HOST_FILES],
                       uint8_t **config, size_t *config_size, uint8_t **intro, size_t *intro_size)
{
    /* The program data is compiled in; XEEN.DAT is never read. */
    const char *names[2] = {"XEEN.CC", "DARK.CC"};
    host_cc_set cc;
    int i;
    if (!opt->menu_start) {
        files[HOST_CLOUDS_SAVE] = host_read_file(opt->cloud_save, &sizes[HOST_CLOUDS_SAVE]);
        files[HOST_DARK_SAVE] = host_read_file(opt->dark_save, &sizes[HOST_DARK_SAVE]);
    }
    /* Phase 287: the three archives as a unit: GAME1.GOG, else GAME2.GOG (a CD image holding all three), else the
     * loose files (host_cc_select). The image is read member by member through one sector buffer and closed; the
     * archives' bytes then live as the loose files' do, until main's end. */
    host_cc_select(opt->dir, &cc);
    for (i = 0; i < 2; i++) {
        if (cc.rejected[i][0]) {
            host_report(HOST_WARN, "note: archive image not used: %s", cc.rejected[i]);
        }
    }
    if (cc.image) {
        host_report(HOST_INFO, "note: game archives XEEN.CC, DARK.CC, INTRO.CC read from %s", cc.image_path);
    }
    for (i = 0; i < 2; i++) {
        files[HOST_XEEN_CC + i] = cc.bytes[HOST_CC_XEEN + i];
        sizes[HOST_XEEN_CC + i] = cc.sizes[HOST_CC_XEEN + i];
        register_cc_file((unsigned)i, files[HOST_XEEN_CC + i], sizes[HOST_XEEN_CC + i], cc.where[HOST_CC_XEEN + i]);
    }
    /* Optional intro.cc (loose; an image must hold it): the Rest dream's scene1.raw (otherwise stop 28F45), the music
     * and sound drivers. Taken before any failure below, so main's cleanup frees it. */
    *intro = cc.bytes[HOST_CC_INTRO];
    *intro_size = cc.sizes[HOST_CC_INTRO];
    register_cc_file(2u, *intro, *intro ? *intro_size : 0u, cc.where[HOST_CC_INTRO]);
    for (i = opt->menu_start ? HOST_XEEN_CC : HOST_CLOUDS_SAVE; i < HOST_FILES; i++) {
        if (!files[i]) {
            if (i >= HOST_XEEN_CC) {
                host_report(HOST_ERROR, "cannot read %s/%s (the game data directory: --data-dir DIR or wox.cfg "
                                "[files] data_dir)",
                        opt->dir, names[i - HOST_XEEN_CC]);
                /* Phase 287: every source tried, in order. */
                host_report(HOST_ERROR, "game archives tried: GAME1.GOG (%s), GAME2.GOG (%s), then the loose files",
                            cc.rejected[0][0] ? "not usable: above" : "absent",
                            cc.rejected[1][0] ? "not usable: above" : "absent");
            } else {
                host_report(HOST_ERROR, "cannot read the save %s",
                        i == HOST_CLOUDS_SAVE ? opt->cloud_save : opt->dark_save);
            }
            host_report(HOST_ERROR, "original save/resources unavailable");
            return 0;
        }
    }
    /* 47C1F reads XEEN.CFG from the game's one DOS directory: Phase 287, the xeen_cfg_dir directory
     * (read_xeen_cfg), where the game also writes it (an ending's 33518, Other Options D: host_files.c), so the
     * configuration they wrote is the next start's. */
    if (opt->menu_start) {
        char where[1200];
        *config = read_xeen_cfg(opt, config_size, where, sizeof where);
        if (!*config && !opt->headless) {
            host_report(HOST_WARN, "note: no XEEN.CFG at %s ([files] xeen_cfg_dir%s); the game starts with an "
                                   "empty configuration (run the DOS installer to make one)",
                        where, opt->xeen_cfg_explicit ? "" : " = auto: the data directory");
        }
    }

    return 1;
}
/**
 * Takes the CD and voice status models from XEEN_CD_STATUS / XEEN_VOICE_STATUS ("model" or "none")
 * when no option chose them. Returns 0 after an invalid value (the host exits with status 1).
 */
static int status_modes_from_environment(void)
{
    if (cd_status_mode < 0 && getenv("XEEN_CD_STATUS")) {
        const char *e = getenv("XEEN_CD_STATUS");
        if (!strcmp(e, "model")) {
            cd_status_mode = 1;
        } else if (!strcmp(e, "none")) {
            cd_status_mode = 0;
        } else {
            host_report(HOST_ERROR, "XEEN_CD_STATUS takes model or none");
            return 0;
        }
    }
    if (voice_status_mode < 0 && getenv("XEEN_VOICE_STATUS")) {
        const char *e = getenv("XEEN_VOICE_STATUS");
        if (!strcmp(e, "model")) {
            voice_status_mode = 1;
        } else if (!strcmp(e, "none")) {
            voice_status_mode = 0;
        } else {
            host_report(HOST_ERROR, "XEEN_VOICE_STATUS takes model or none");
            return 0;
        }
    }
    return 1;
}
/** Whether file PATH exists (can be opened for reading). */
static int file_present(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) {
        fclose(f);
    }
    return f != NULL;
}
/**
 * Phase 335 (owner request): before the default wox.cfg at WHERE is created, the player is advised to run the setup
 * tool first and asked whether to create the defaults and start. 1 to go on; 0 when the player declines, or chose
 * "Run setup" and the setup tool (beside wox.cfg, that is beside the game) was started; -1 when it could not be
 * started. Without a dialog (not Windows) the advice goes to the log and the game goes on, as before.
 */
static int confirm_default_config(const char *where)
{
#if defined(_WIN32)
    static const char setup_name[] = "setup.exe";
#else
    static const char setup_name[] = "setup";
#endif
    char text[1600];
    int answer;
    snprintf(text, sizeof text,
             "wox.cfg was not found:\n%s\n\nIt is recommended to run %s (beside the game) first, to choose the "
             "game's folders, music and display settings.\n\nCreate the default wox.cfg and start the game now?",
             where, setup_name);
    answer = xeen_platform_ask("WoX - wox.cfg not found", text, "Run setup");
    if (answer < 0) {
        host_report(HOST_WARN, "note: %s not found; creating it with the defaults (running %s first is recommended)",
                    where, setup_name);
        return 1;
    }
    if (answer == 2) {
        char setup[1100];
        const char *cut = strrchr(where, '\\');
        int dir_len = cut ? (int)(cut - where) : 0;
        snprintf(setup, sizeof setup, "%.*s%s%s", dir_len, where, cut ? "\\" : "", setup_name);
        if (!xeen_platform_run(setup)) {
            host_report(HOST_ERROR, "cannot start %s", setup);
            return -1;
        }
        host_report(HOST_INFO, "note: %s not found; %s started", where, setup);
        return 0;
    }
    if (!answer) {
        host_report(HOST_INFO, "note: %s not found; the player chose not to create it (run %s first)", where,
                    setup_name);
    }
    return answer;
}
/* The owner-approved add-ons and bug fixes a window's wox.cfg turns on ([addons], [bugfix]), in the generated wox.cfg's
 * order: each flag of xeen_wox_config and the name its module is enabled by (xeen/addons.h XEEN_ADDON_NAMES,
 * xeen/bugfixes.h XEEN_BUGFIX_NAMES). */
static const struct {
    size_t flag; /* offsetof(xeen_wox_config, ...): an int, nonzero when on */
    const char *name;
} window_addons[] = {
        {offsetof(xeen_wox_config, addon_identify), "identify"},
        {offsetof(xeen_wox_config, addon_map_movement), "map_movement"},
        {offsetof(xeen_wox_config, addon_training_preview), "training_preview"},
        {offsetof(xeen_wox_config, addon_damage_numbers), "damage_numbers"},
        {offsetof(xeen_wox_config, addon_damage_numbers_stack), "damage_numbers.stack"},
        {offsetof(xeen_wox_config, addon_monster_difficulty), "monster_difficulty"},
        {offsetof(xeen_wox_config, addon_spell_short_description), "spell_short_description"},
        {offsetof(xeen_wox_config, addon_mouse_scroll), "mouse_scroll"},
        {offsetof(xeen_wox_config, addon_town_icons), "town_icons"},
        {offsetof(xeen_wox_config, addon_character_details), "character_details"},
        {offsetof(xeen_wox_config, addon_spell_list_sort), "spell_list_sort"},
        {offsetof(xeen_wox_config, addon_world_map), "world_map"},
        {offsetof(xeen_wox_config, addon_quest_journal), "quest_journal"},
        {offsetof(xeen_wox_config, addon_please_wait_skip), "please_wait_skip"},
        {offsetof(xeen_wox_config, addon_wizard_eye_plus), "wizard_eye_plus"}},
  window_bugfixes[] = {{offsetof(xeen_wox_config, bugfix_spell_description), "spell_description"},
                       {offsetof(xeen_wox_config, bugfix_left_clip_colors), "left_clip_colors"},
                       {offsetof(xeen_wox_config, bugfix_sprite_width_trim), "sprite_width_trim"},
                       {offsetof(xeen_wox_config, bugfix_quest_up_down), "quest_up_down"}};
static int window_flag(const xeen_wox_config *wox, size_t flag)
{
    return *(const int *)(const void *)((const char *)wox + flag);
}
/**
 * A window's wox.cfg: located beside the executable (or --wox-config), created with the defaults
 * only when absent (after the player's confirmation, confirm_default_config; a failed creation is warned about and
 * tried once more, then the defaults stay), and loaded into WOX; --fullscreen overrides its display mode, and its
 * [addons]/[bugfix] switches are enabled (window_addons, window_bugfixes). Returns 0, or the host's exit status
 * after a failure: 1 when wox.cfg cannot be located or the setup tool the player chose cannot be started, 2 when it
 * does not load; -1 when the player declined to create it or started the setup tool instead (the host then ends with
 * status 0).
 */
static int load_window_config(const explore_options *opt, const char *argv0, xeen_wox_config *wox)
{
    char where[1024], why[600];
    size_t i;
    int present = opt->wox_path != NULL;
    if (!xeen_wox_config_locate(opt->wox_path, argv0, where, sizeof where)) {
        host_report(HOST_ERROR, "cannot locate wox.cfg");
        return 1;
    }
    if (!present && !file_present(where)) {
        int go = confirm_default_config(where);
        if (go <= 0) {
            return go < 0 ? 1 : -1; /* the setup tool did not start: an error; declined or setup started: a clean end */
        }
    }
    if (!present && !(present = xeen_wox_config_create(where))) {
        host_report(HOST_WARN, "note: cannot create %s; using the defaults", where);
        present = xeen_wox_config_create(where);
    }
    if (present && !xeen_wox_config_load(where, wox, why, sizeof why)) {
        host_report(HOST_ERROR, "%s", why);
        return 2;
    }
    if (opt->display_override >= 0) {
        wox->fullscreen = opt->display_override;
    }
    for (i = 0; i < sizeof window_addons / sizeof window_addons[0]; ++i) {
        if (window_flag(wox, window_addons[i].flag)) {
            (void)xeen_addons_enable(&host_addons, window_addons[i].name);
        }
    }
    for (i = 0; i < sizeof window_bugfixes / sizeof window_bugfixes[0]; ++i) {
        if (window_flag(wox, window_bugfixes[i].flag)) {
            (void)xeen_bugfixes_enable(window_bugfixes[i].name);
        }
    }
    return 0;
}
#if defined(XEEN_HAVE_AUDIO)
/* The engine's options both opens share, into O (cleared first): WOX, the archives (XEEN.CC and DARK.CC's VOC names,
 * INTRO.CC's drivers), MODE, the CD rips' directory (cd_tracks_dir), the configured digital driver's voice layout
 * and XEEN.CFG's image. */
static void fill_audio_options(xeen_audio_engine_options *o, xeen_wox_config *wox, xeen_archive *archives,
                               const uint8_t *xeen_cfg, size_t xeen_cfg_size, xeen_audio_mode mode)
{
    memset(o, 0, sizeof *o);
    o->config = wox;
    o->drivers = &archives[HOST_ARCHIVE_CLOUDS];
    o->intro_drivers = audio_intro;
    o->cc[0] = &archives[HOST_ARCHIVE_CLOUDS];
    o->cc[1] = &archives[HOST_ARCHIVE_DARK];
    o->mode = mode;
    o->cd_dir = cd_tracks_dir(wox);
    xeen_audio_engine_voice_options(o, voice_kind, voice_trim, speech.audible); /* the driver's layout, [audio] speech */
    o->xeen_cfg = xeen_cfg;
    o->xeen_cfg_size = xeen_cfg_size;
}
#endif
/**
 * A window's audio engine (unless --no-audio): realtime, with XEEN.CC's drivers (INTRO.CC's canmus), the CD voice tracks
 * when found and the sound driver's voice trim; sets the CD and voice status sources the window
 * models. A failure leaves the game silent.
 */
static void open_window_audio(const explore_options *opt, xeen_wox_config *wox,
                              xeen_archive *archives, const uint8_t *xeen_cfg, size_t xeen_cfg_size)
{
#if defined(XEEN_HAVE_AUDIO)
    if (!opt->no_audio) {
        xeen_audio_engine_options o;
        char why[600];
        fill_audio_options(&o, wox, archives, xeen_cfg, xeen_cfg_size, XEEN_AUDIO_REALTIME);
        o.sc55_boot = sc55_boot; /* Phase 290: the booted SC-55 (NULL: the open boots it) */
        if (!o.cd_dir) {
            host_report(HOST_WARN,
                    "note: CD voice tracks not found (%s; [files] cd_tracks_dir); CD speech is "
                    "silent",
                    wox->cd_tracks);
        }
        xeen_archive_miss_clear();
        audio_engine = xeen_audio_engine_open(&o, why, sizeof why);
        cd_status_window = audio_engine && o.cd_dir; /* A drive with its discs */
        voice_status_window = audio_engine != NULL;  /* The engine plays voices */
        if (!audio_engine) {
            host_report(HOST_WARN, "%s; the game runs silent", why);
            report_cc_miss(0);
        } else if (!xeen_audio_engine_music_ready(audio_engine)) {
            /* Phase 286: music_override = auto resolved to no music was reported with its reason (configure_host). */
            if (!(wox->music_auto && wox->system == XEEN_MUSIC_OFF)) {
                host_report(HOST_WARN, "%s (voices still play)", why);
            }
            report_cc_miss(0);
        } else {
            host_report(HOST_INFO, "audio: %s music, INTRO.CC %s driver, 32000 Hz",
                    xeen_music_system_name(wox->system), xeen_music_system_driver(wox->system));
        }
    }
#else
    (void)wox; /* no audio engine in this build */
    (void)archives;
    (void)xeen_cfg;
    (void)xeen_cfg_size;
    if (!opt->no_audio) {
        host_report(HOST_WARN, "note: built without the audio engine (SDL2); the game runs silent");
    }
#endif
}
/**
 * Phase 290 (owner-approved host start-up, not original behaviour): whether a window's audio waits for the window
 * (an SC-55-backed system, sc55/wave/tap10, also from music_override = auto, whose ROM directory passes the open's own first
 * check; a failing check keeps the open, and its error, before the window as before; a window with --audio-capture
 * as before too). Other systems are unchanged.
 */
static int defer_window_audio(const explore_options *opt, const xeen_wox_config *wox)
{
#if defined(XEEN_HAVE_AUDIO)
    char why[256];
    /* --audio-capture replaces the window's engine with its offline one right after (open_capture_audio): unchanged. */
    return !opt->no_audio && !opt->audio_capture && xeen_audio_sc55_backed(wox->system) &&
           xeen_wox_config_check_roms(wox, why, sizeof why);
#else
    (void)opt;
    (void)wox;
    return 0;
#endif
}
#if defined(XEEN_HAVE_AUDIO)
/* The wait's context: the window and the boot; the shown frame and the bar's per mille in it (-1: no bar yet). */
typedef struct {
    xeen_platform *platform;
    xeen_audio_sc55_boot *boot;
    uint8_t *page, *palette;
    int shown;
} startup_context;
/* The owner-requested progress bar: drawn under the message once the boot reports progress, and presented again
 * whenever its per mille changes (a library without progress shows the message alone). */
static void startup_refresh(void *u)
{
    startup_context *c = u;
    int permille = xeen_audio_sc55_boot_progress(c->boot);
    if (permille >= 0 && permille != c->shown) {
        c->shown = permille;
        host_startup_progress_bar(c->page, (unsigned)permille);
        (void)xeen_platform_present(c->platform, c->page, c->palette);
    }
}
static int startup_finished(void *u)
{
    return xeen_audio_sc55_boot_finished(((startup_context *)u)->boot);
}
static int startup_should_close(void *u)
{
    return xeen_platform_should_close(((startup_context *)u)->platform);
}
static void startup_pump(void *u)
{
    xeen_platform_pump(((startup_context *)u)->platform);
}
static void startup_wait(void *u, unsigned ms)
{
    xeen_platform_wait(((startup_context *)u)->platform, ms);
}
/* Developer instrumentation (XEEN_HOST_TIMING): each start-up point in ms since the window opened. */
static uint64_t startup_origin_ns;
static void startup_mark(const char *what)
{
    uint64_t now = xeen_platform_now_ns();
    if (!startup_origin_ns) {
        startup_origin_ns = now;
    }
    if (timing.f) {
        fprintf(timing.f, "# startup %s %.1f ms\n", what, (double)(now - startup_origin_ns) / 1e6);
    }
}
#endif
/**
 * Phase 290 (owner-approved host start-up, not original behaviour): the deferred window audio. The SC-55 engine boots
 * on its worker (the unchanged synth_open_sc55) while the window shows HOST_STARTUP_SC55_TEXT in the game's font
 * and palette and keeps its events; then the message is cleared, the audio engine opens with the booted engine
 * (open_window_audio, as before) and the input queued meanwhile is discarded with the game clock started anew
 * (xeen_platform_startup_done). Returns 0 when the window was closed during the boot: the window is closed at once,
 * the boot joined (it cannot be interrupted) and its engine closed unused; nothing of the game was created.
 */
static int start_deferred_audio(const explore_options *opt, xeen_wox_config *wox, host_resources *res,
                                xeen_platform **platform)
{
#if defined(XEEN_HAVE_AUDIO)
    int opened = 1;
    if (!audio_deferred) {
        return 1;
    }
    audio_deferred = 0;
    startup_mark("window-open");
    if ((sc55_boot = xeen_audio_sc55_boot_start(wox)) != NULL) {
        static uint8_t page[XEEN_PRESENT_PIXELS], palette[DISPLAY_PALETTE_BYTES];
        startup_context context = {*platform, sc55_boot, page, palette, -1};
        host_startup_wait_ops ops = {&context,     startup_finished, startup_should_close, startup_pump, startup_wait,
                                     NULL,         startup_refresh};
        if (!host_startup_loading_frame(&res->archives[HOST_ARCHIVE_CLOUDS], HOST_STARTUP_SC55_TEXT, page, palette, NULL,
                                        NULL, NULL)) {
            host_report(HOST_WARN, "note: the SC-55 loading message needs XEEN.CC's fnt and mm4.pal; a black window "
                                   "shows while the SC-55 initializes");
        }
        (void)xeen_platform_present(*platform, page, palette);
        startup_mark("message-shown");
        opened = host_startup_wait(&ops) == HOST_STARTUP_READY;
        if (!opened) {
            startup_mark("closed");
            xeen_platform_close(*platform); /* the window goes at once; the boot ends unseen */
            *platform = NULL;
            xeen_audio_sc55_boot_close(sc55_boot);
            sc55_boot = NULL;
            startup_mark("boot-joined");
        } else {
            startup_mark("sc55-ready");
            memset(page, 0, sizeof page);
            (void)xeen_platform_present(*platform, page, palette); /* the message removed */
        }
    } else {
        startup_mark("no-worker"); /* the open boots it here (no thread could start) */
    }
    if (opened) {
        open_window_audio(opt, wox, res->archives, audio_deferred_cfg, audio_deferred_cfg_size);
        xeen_audio_sc55_boot_close(sc55_boot); /* joined; the engine took the booted SC-55 */
        sc55_boot = NULL;
        startup_mark("audio-open");
        xeen_platform_startup_done(*platform);
    }
    free(audio_deferred_cfg);
    audio_deferred_cfg = NULL;
    audio_deferred_cfg_size = 0;
    return opened;
#else
    (void)opt;
    (void)wox;
    (void)res;
    (void)platform;
    return 1;
#endif
}
/**
 * --audio-capture: replaces any window engine with an offline one (deterministic, no device, no
 * threads; music is not rendered) that logs every request to the capture file. Returns 0 when the
 * file cannot be created, or in a build without the audio engine (the host exits with status 1).
 */
static int open_capture_audio(const explore_options *opt, xeen_wox_config *wox,
                              xeen_archive *archives, const uint8_t *xeen_cfg, size_t xeen_cfg_size)
{
#if defined(XEEN_HAVE_AUDIO)
    if (opt->audio_capture) {
        /* Offline: deterministic, no device, no threads; music is not rendered. */
        xeen_audio_engine_options o;
        char why[600];
        if (audio_engine) {
            xeen_audio_engine_close(audio_engine);
            audio_engine = NULL;
        }
        if (!(audio_log = fopen(opt->audio_capture, "w"))) {
            perror(opt->audio_capture);
            return 0;
        }
        fill_audio_options(&o, wox, archives, xeen_cfg, xeen_cfg_size, XEEN_AUDIO_OFFLINE);
        audio_engine = xeen_audio_engine_open(&o, why, sizeof why);
        if (wox->music_auto) {
            fprintf(audio_log, "# %s\n", music_resolution);
        }
        if (wox->speech != XEEN_SPEECH_AUTO) {
            fprintf(audio_log, "# %s\n", speech_resolution);
        }
        fprintf(audio_log, "# backend %s music-ready %d %s\n", xeen_music_system_name(wox->system),
                xeen_audio_engine_music_ready(audio_engine),
                audio_engine ? xeen_audio_engine_music_error(audio_engine) : why);
    }
#else
    (void)wox; /* no audio engine in this build */
    (void)archives;
    (void)xeen_cfg;
    (void)xeen_cfg_size;
    if (opt->audio_capture) {
        host_report(HOST_ERROR, "--audio-capture needs the audio engine build");
        return 0;
    }
#endif
    return 1;
}
/**
 * Prints one note per enabled add-on and bug fix (owner-approved, not original behavior) to stderr.
 */
static void report_addons(void)
{
    if (host_addons.item_stats) {
        host_report(HOST_INFO,
                "add-on identify enabled (not original behavior): identified item statistics");
    }
    if (host_addons.map_movement) {
        host_report(HOST_INFO, "add-on map_movement enabled (not original behavior): the map pans with "
                        "the arrow keys and a mouse drag");
    }
    if (host_addons.training_preview) {
        host_report(HOST_INFO, "add-on training_preview enabled (not original behavior): the trainer's "
                        "Prev button shows the level ladder");
    }
    if (host_addons.damage_numbers) {
        host_report(HOST_INFO,
                "add-on damage_numbers enabled (not original behavior): damage, healing and misses "
                "as numbers%s",
                host_addons.damage_numbers_stack ? " (stack layout)" : "");
    } else if (host_addons.damage_numbers_stack) {
        host_report(HOST_WARN, "add-on damage_numbers.stack has no effect without damage_numbers");
    }
    if (host_addons.monster_difficulty) {
        host_report(HOST_INFO, "add-on monster_difficulty enabled (not original behavior): difficulty "
                        "icons over the monsters");
    }
    if (host_addons.spell_short_description) {
        host_report(HOST_INFO, "add-on spell_short_description enabled (not original behavior): a spell "
                        "box in the Cast Spell dialog");
    }
    if (host_addons.mouse_scroll) {
        host_report(HOST_INFO, "add-on mouse_scroll enabled (not original behavior): the mouse wheel "
                        "scrolls the lists");
    }
    if (host_addons.town_icons) {
        host_report(HOST_INFO, "add-on town_icons enabled (not original behavior): icons for the town "
                        "places on the map and the minimap");
    }
    if (host_addons.character_details) {
        host_report(HOST_INFO, "add-on character_details enabled (not original behavior): what the "
                        "character view's stats, skills and conditions do");
    }
    if (host_addons.spell_list_sort) {
        host_report(HOST_INFO, "add-on spell_list_sort enabled (not original behavior): the spell lists "
                        "sorted A-Z or by type (O)");
    }
    if (host_addons.world_map) {
        host_report(HOST_INFO, "add-on world_map enabled (not original behavior): W or the World Map "
                        "button in the map (M) shows the whole world");
    }
    if (host_addons.quest_journal) {
        host_report(HOST_INFO, "add-on quest_journal enabled (not original behavior): numbered rows in the "
                        "Quests dialog (V): item descriptions%s",
                host_addons.world_map ? ", quests' and notes' places on the world map" : "");
    }
    if (host_addons.please_wait_skip) {
        host_report(HOST_INFO, "add-on please_wait_skip enabled (not original behavior): the map load's "
                        "\"Please Wait...\" box is not shown");
    }
    if (xeen_bugfix_spell_description_on) {
        host_report(HOST_INFO, "bug fix spell_description enabled (not original behavior): corrected "
                        "spell descriptions");
    }
    if (xeen_bugfix_left_clip_colors_on) {
        host_report(HOST_INFO, "bug fix left_clip_colors enabled (not original behavior): sprites cut by "
                        "the 3D view's left edge keep their own colours");
    }
    if (xeen_bugfix_sprite_width_trim_on) {
        host_report(HOST_INFO, "bug fix sprite_width_trim enabled (not original behavior): shrunk "
                        "pictures trimmed to their stated width");
    }
    if (xeen_bugfix_quest_up_down_on) {
        host_report(HOST_INFO, "bug fix quest_up_down enabled (not original behavior): U and D scroll "
                        "the Quests dialog");
    }
}
/**
 * Starts the add-ons that must exist before the first scene: mouse_scroll's and world_map's wheel keys (in
 * PROVIDER), please_wait_skip's flag (before the first map load), town_icons and wizard_eye_plus. A failed start
 * leaves that add-on off: mouse_scroll silently, the others with a warning (world_map also clears its flag).
 */
static void start_host_addons(xeen_platform *platform, xeen_input_provider *provider, xeen_game **g)
{
    if (host_addons.mouse_scroll) { /* add-on: the wheel's keys */
        xeen_platform_set_wheel(platform, 1);
        if (!xeen_mouse_scroll_start(provider, addon_wheel_take, platform, g)) {
            xeen_platform_set_wheel(platform, 0);
        }
    }
    xeen_please_wait_skip_on = host_addons.please_wait_skip; /* add-on: before the first map load */
    if (host_addons.town_icons && !xeen_town_icons_start()) { /* Before the first scene */
        host_report(HOST_WARN, "add-on town_icons: out of memory; the add-on is off");
    }
    if (host_addons.wizard_eye_plus && !xeen_wizard_eye_plus_start(host_addons.wizard_eye_plus_style)) {
        host_report(HOST_WARN, "add-on wizard_eye_plus: out of memory; the add-on is off");
    }
    if (host_addons.world_map) { /* add-on: after mouse_scroll, so its wheel wrapper is outermost */
        xeen_platform_set_wheel(platform, 1);
        if (!xeen_world_map_start(provider, addon_wheel_take, platform)) {
            if (!host_addons.mouse_scroll) {
                xeen_platform_set_wheel(platform, 0);
            }
            host_addons.world_map = 0;
            host_report(HOST_WARN, "add-on world_map: out of memory; the add-on is off");
        }
    }
}
/**
 * Starts the add-ons that draw with the session's fonts (damage_numbers, monster_difficulty,
 * spell_short_description, character_details, quest_journal) and binds the ones that draw into the game to G. A
 * missing font turns one off with a warning; monster_difficulty loads its sprites later and its start refuses only
 * without SPRITE_ARCHIVES (then it stays unbound, without a warning).
 */
static void bind_session_addons(xeen_game *g, const xeen_sprite_archives *sprite_archives)
{
    if (host_addons.damage_numbers &&
        !xeen_damage_numbers_start(host_addons.damage_numbers_stack, sprite_archives)) {
        host_report(HOST_WARN, "add-on damage_numbers: the font (fnt) is missing; the add-on is off");
    }
    if (host_addons.monster_difficulty && xeen_monster_difficulty_start(sprite_archives)) {
        xeen_monster_difficulty_bind(g);
    }
    if (host_addons.spell_short_description) {
        if (xeen_spell_short_description_start(sprite_archives)) {
            xeen_spell_short_description_bind(g);
        } else {
            host_report(HOST_WARN, "add-on spell_short_description: the font (fnt) is missing; the add-on "
                            "is off");
        }
    }
    if (host_addons.character_details) {
        if (xeen_character_details_start(sprite_archives)) {
            xeen_character_details_bind(g);
        } else {
            host_report(HOST_WARN,
                    "add-on character_details: the font (fnt) is missing; the add-on is off");
        }
    }
    if (host_addons.quest_journal && !xeen_quest_journal_start(sprite_archives)) {
        host_report(HOST_WARN, "add-on quest_journal: the font (fnt) is missing; the add-on is off");
    }
}
/**
 * --save-output: commits the session's final save pair to DIR (the active world's state archive, the
 * other world's CUR image or its starting save, the DS image and the event store). Returns 0 after a
 * failed commit.
 */
static int commit_save_output(const char *save_output, xeen_game *g, uint8_t *files[HOST_FILES],
                              size_t sizes[HOST_FILES])
{
    char error[256];
    size_t event_size = 0;
    const uint8_t *event_store = xeen_game_event_store(g, &event_size);
    size_t active_size = 0;
    const uint8_t *active = xeen_game_state_archive(g, &active_size);
    unsigned active_world = xeen_game_inspect(g).position.world ? 1u : 0u;
    /* By world (0 Clouds, 1 Darkside): the active world's state archive; the other world's CUR image, or without
     * one its starting save. */
    const uint8_t *world[2];
    size_t world_size[2];
    world[active_world] = active;
    world_size[active_world] = active_size;
    world_size[!active_world] = 0;
    world[!active_world] = xeen_game_cur(g, !active_world, &world_size[!active_world]);
    if (!world[!active_world]) {
        world[!active_world] = files[active_world ? HOST_CLOUDS_SAVE : HOST_DARK_SAVE];
        world_size[!active_world] = sizes[active_world ? HOST_CLOUDS_SAVE : HOST_DARK_SAVE];
    }
    const uint8_t *cloud = world[0], *dark = world[1];
    size_t cloud_size = world_size[0], dark_size = world_size[1];
    if (!xeen_save_commit_pair_event(save_output, cloud, cloud_size, dark, dark_size,
                                     xeen_game_ds(g), 65536, event_store, event_size, error,
                                     sizeof error)) {
        host_report(HOST_ERROR, "save commit failed: %s", error);
        return 0;
    } else {
        printf("SAVE committed %s/XEEN.WOX %s/DARK.WOX\n", save_output, save_output);
    }
    return 1;
}
/**
 * XEEN_HOST_TIMING (developer instrumentation): writes the step and hold distributions and the pass
 * counters to the timing file and closes it.
 */
static void report_timing(xeen_platform *platform)
{
    if (timing.f) {
        timing_dist("step", timing.steps, timing.step_max, timing.nsteps);
        timing_dist("hold", timing.holds, timing.hold_max, timing.nholds);
        fprintf(timing.f, "# cursor-repaints=%lu\n", xeen_platform_cursor_repaints(platform));
        fprintf(timing.f,
                "# passes=%lu no-wait-passes=%lu longest-no-wait-run passes=%llu us=%llu\n",
                timing.passes, timing.spins, (unsigned long long)timing.run_passes_max,
                (unsigned long long)(timing.run_max / 1000u));
        fclose(timing.f);
        timing.f = NULL;
    }
}
/**
 * Stops every add-on and frees the add-on switches.
 */
static void stop_host_addons(void)
{
    xeen_damage_numbers_stop();
    xeen_monster_difficulty_stop();
    xeen_spell_short_description_stop();
    xeen_mouse_scroll_stop();
    xeen_town_icons_stop();
    xeen_wizard_eye_plus_stop();
    xeen_character_details_stop();
    xeen_world_map_stop();
    xeen_quest_journal_stop();
    xeen_please_wait_skip_on = 0;
    xeen_addons_free(&host_addons);
}
/**
 * Closes the audio engine (before the archives' bytes are freed), first logging its request counters
 * to the capture file, and closes that file.
 */
static void close_audio(void)
{
#if defined(XEEN_HAVE_AUDIO)
    if (audio_engine) {
        xeen_audio_engine_stats st;
        xeen_audio_engine_get_stats(audio_engine, &st);
        if (audio_log) {
            fprintf(audio_log,
                    "# stats effects=%lu voices=%lu voice-errors=%lu songs=%lu controls=%lu "
                    "volumes=%lu dropped=%lu\n",
                    st.effects, st.voices, st.voice_errors, st.songs, st.controls, st.volumes,
                    st.dropped);
        }
        if (audio_log && (st.cd_plays || st.cd_stops)) {
            fprintf(audio_log, "# cd plays=%lu stops=%lu errors=%lu\n", st.cd_plays, st.cd_stops,
                    st.cd_errors);
        }
    }
    xeen_audio_engine_close(audio_engine);
    audio_engine = NULL; /* before the archives' bytes */
    if (audio_log) {
        fclose(audio_log);
        audio_log = NULL;
    }
#endif
}
/** What a feeder tells the run loop to do next (the loop's own continue, break and jumps). */
typedef enum host_flow {
    HOST_NEXT,       /* go on with the rest of the pass */
    HOST_CONTINUE,   /* start the next pass */
    HOST_BREAK,      /* leave the loop: the session ended normally (the final evidence and save) */
    HOST_DONE,       /* leave without the final evidence (an error, or a stop already recorded) */
    HOST_PANEL_WAIT, /* a control-panel key was held: skip to the shared logging after the menus */
    HOST_MODAL_WAIT  /* a modal key was held: skip to the clock advance */
} host_flow;
/**
 * A flow that ends the pass: HOST_CONTINUE (the next pass), HOST_BREAK or HOST_DONE (the loop ends). HOST_NEXT and
 * the two held keys (HOST_PANEL_WAIT, HOST_MODAL_WAIT) go on with the rest of the pass. Every feeder's answer is
 * read by this one rule (the main menu's and the panel's feeders never answer HOST_CONTINUE).
 */
static int ends_pass(host_flow flow)
{
    return flow == HOST_CONTINUE || flow == HOST_BREAK || flow == HOST_DONE;
}

/**
 * The run loop's replay state: the script cursor and the marks that make each poll, scene and
 * save log once. Fields keep the names of main's former locals; all start at 0 except
 * ready_marked (1) and logged_combat (no combat poll logged yet).
 */
typedef struct explore_replay {
    size_t command_index; /* the next --commands character */
    size_t markers;       /* '|' markers passed (not inputs: the evidence labels count inputs) */
    unsigned loads;       /* sessions started by a Load or a new game */
    /* Main menu */
    int menu_logged, menu_armed, menu_last_stage;
    unsigned long menu_ignored;
    size_t menu_presented;
    /* Exploration poll and the '~' idle convention */
    unsigned long ready_calls;
    int ready_marked;
    int ready, idle_done, pending;
    unsigned long idle_mark;
    int idle_pending;
    uint32_t idle_boundary;
    unsigned long logged_commands;
    /* Control panel and saves */
    unsigned long panel_last_session, logged_saves, saves_before;
    int panel_armed, panel_logged;
    /* Scenes, modal waits, item passes, location ticks and cutscenes */
    unsigned long previous_scenes;
    unsigned long logged_modal_polls, modal_poll_base, logged_item_passes, logged_loc_ticks;
    int modal_queued;
    unsigned modal_visits;
    int logged_modal;
    int modal_idle;
    unsigned long modal_idle_mark;
    unsigned long modal_ignored;
    size_t modal_steps, modal_presentations;
    unsigned long logged_combat; /* the window's last logged combat poll */
    int logged_phase;            /* the window's last logged combat phase */
    unsigned long logged_cut_events;
} explore_replay;

/* A replay key no feeder knows: with XEEN_TEST_SKIP_INVALID_KEYS the key is skipped and the next pass starts, else
 * the run ends with "MESSAGE C" on stderr (the feeder's own message). */
static host_flow invalid_key(explore_replay *replay, const char *message, char ch)
{
    if (skip_invalid) {
        ++replay->command_index;
        return HOST_CONTINUE;
    }
    host_report(HOST_ERROR, "%s %c", message, ch);
    return HOST_DONE;
}
/*
 * The replay's input conventions (one character per input; harness/inputs.py has the grammar): what a character
 * does at each kind of poll. REPLAY_PREFIXED: '^' and the next character are one key (the '^' is a marker, not an
 * input); REPLAY_INVALID_PREFIXED: a '^' with a character this poll does not take.
 */
typedef enum {
    REPLAY_KEY,
    REPLAY_CLICK, /* '*' */
    REPLAY_NO_KEY, /* '~' at the exploration poll */
    REPLAY_PREFIXED,
    REPLAY_WHEEL, /* '^' '{' or '}' at a panel poll: one wheel notch (KEY XKEY_UP or XKEY_DOWN), no key */
    REPLAY_INVALID,
    REPLAY_INVALID_PREFIXED
} replay_input;
/* A replay character and the key it presses at one kind of poll. */
typedef struct {
    char ch;
    xeen_native_key key;
} replay_key;
/** Sets KEY to TABLE's key for CH and returns 1; 0 (KEY unchanged) when CH has no entry. */
static int replay_key_find(const replay_key *table, size_t n, char ch, xeen_native_key *key)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        if (table[i].ch == ch) {
            *key = table[i].key;
            return 1;
        }
    }
    return 0;
}
#define REPLAY_KEYS(table) (sizeof(table) / sizeof(table)[0])
/** A lower-case letter is that letter's own key (raw: the upper-case letter's code, e.g. 'n' the N key). */
static int replay_raw_letter(char ch, xeen_native_key *key)
{
    if (ch >= 'a' && ch <= 'z') {
        (void)panel_key((char)(ch - 'a' + 'A'), key);
        return 1;
    }
    return 0;
}
/** The characters both command polls share after their own tables: '1'..'6' the function keys F1..F6, '=' the game's
 * '=' key, a lower-case letter its own key; anything else is not an input there. */
static replay_input replay_command_key(char ch, xeen_native_key *key)
{
    if (ch >= '1' && ch <= '6') {
        *key = (xeen_native_key)(XKEY_F1 + ch - '1');
        return REPLAY_KEY;
    }
    if (ch == '=') {
        (void)xeen_input_punctuation_key('=', key);
        return REPLAY_KEY;
    }
    return replay_raw_letter(ch, key) ? REPLAY_KEY : REPLAY_INVALID;
}
/** The exploration command poll 17B09's characters (KEY unchanged for '*' and '~'). */
static replay_input exploration_input(char ch, xeen_native_key *key)
{
    static const replay_key keys[] = {
            {'D', XKEY_DOWN},
            {'A', XKEY_ENTER},
            {'B', XKEY_B},
            {'F', XKEY_S},
            {'Z', XKEY_R},
            {'U', XKEY_UP},
            {'L', XKEY_LEFT},
            {'R', XKEY_RIGHT},
            {'[', (xeen_native_key)(XKEY_LEFT | XKEY_CTRL)},
            {']', (xeen_native_key)(XKEY_RIGHT | XKEY_CTRL)},
            {'S', XKEY_SPACE},
            {'W', XKEY_W},
            {'E', XKEY_ESCAPE},
            {'T', XKEY_TAB},
            {'C', XKEY_C},
            {'I', XKEY_I},
            {'Q', XKEY_Q}, /* 17DD1 Info, 17E28 Quick Reference */
            {'V', XKEY_V}, /* 17E18 Quests */
            {'M', XKEY_M}, /* 17DC4 the map */
            {'7', XKEY_F7},
            {'8', XKEY_F8},
            {'K', XKEY_D}, /* 17E20 the minimap, F7 (CFh), F8 17E10, the D key 17D9B */
            /* The game's Esc (1Bh) at the exploration poll, as at modal and menu polls; 'E' stays the host's
             * close request. */
            {'X', XKEY_GAME_ESCAPE}};
    if (ch == '*') {
        return REPLAY_CLICK;
    }
    /* '~' is this 17B09 poll answered with no key (189E3's 4FD76 gives F5FE = 0; 4EA1C, the F60A test): an idle
     * player after an event that killed the party. */
    if (ch == '~') {
        return REPLAY_NO_KEY;
    }
    if (replay_key_find(keys, REPLAY_KEYS(keys), ch, key)) {
        return REPLAY_KEY;
    }
    /* e.g. 'n' the N key, which 17C10's table lacks */
    return replay_command_key(ch, key);
}
/** The main menu's characters at AT (AT[1] read only after a '^'; KEY unchanged for '*'). */
static replay_input menu_input(const char *at, xeen_native_key *key)
{
    static const replay_key keys[] = {
            {'A', XKEY_ENTER},
            {'X', XKEY_GAME_ESCAPE},
            {'L', XKEY_L},
            {'S', XKEY_S},
            {'C', XKEY_C},
            {'O', XKEY_O},
            {'V', XKEY_V},
            {'0', XKEY_0},
            {'1', XKEY_1},
            {'2', XKEY_2},
            {'3', XKEY_3},
            /* E, D and W are letters (Other Options' buttons); X stays Escape. */
            {'E', XKEY_E},
            {'D', XKEY_D},
            {'W', XKEY_W},
            {'Y', XKEY_Y},
            {'N', XKEY_N}, /* 2937A's keys */
            {'Q', XKEY_Q}}; /* the plain letter */
    char ch = at[0];
    if (ch == '*') {
        return REPLAY_CLICK;
    }
    if (replay_key_find(keys, REPLAY_KEYS(keys), ch, key)) {
        return REPLAY_KEY;
    }
    if (ch >= '4' && ch <= '9') {
        *key = (xeen_native_key)(XKEY_4 + (ch - '4'));
        return REPLAY_KEY;
    }
    /* '^' and a letter: that letter with Ctrl (^Q the title menu's quit). */
    if (ch == '^' && at[1] >= 'A' && at[1] <= 'Z') {
        (void)panel_key(at[1], key);
        *key = (xeen_native_key)(*key | XKEY_CTRL);
        return REPLAY_PREFIXED;
    }
    return REPLAY_INVALID;
}
/** The combat command poll 39131's characters at AT (AT[1] read only after a '^'; KEY unchanged for '*'). */
static replay_input combat_input(const char *at, xeen_native_key *key)
{
    static const replay_key keys[] = {
            {'1', XKEY_1},
            {'2', XKEY_2},
            {'3', XKEY_3},
            {'A', XKEY_A},
            {'B', XKEY_B},
            {'C', XKEY_C}, /* Cast (391E5) */
            {'F', XKEY_S}, /* Shoot; CS:2F7D has no S, 39161 -> 3934F */
            {'U', XKEY_U}, /* Use (39223) */
            {'R', XKEY_R}, /* Run (39260) */
            /* The other CS:2F7D commands: G Quick Fight (F, 391DE; the host's F stays Shoot), I Info, O Options,
             * Q Quick Reference, T Tab, '<' and '>' the turns (F0/F1); '^' 1..6 F1..F6. */
            {'G', XKEY_F},
            {'I', XKEY_I},
            {'O', XKEY_O},
            {'Q', XKEY_Q},
            {'T', XKEY_TAB},
            {'<', XKEY_LEFT},
            {'>', XKEY_RIGHT}};
    char ch = at[0];
    if (ch == '*') {
        return REPLAY_CLICK;
    }
    if (replay_key_find(keys, REPLAY_KEYS(keys), ch, key)) {
        return REPLAY_KEY;
    }
    if (ch == '^') {
        char d = at[1];
        if (d >= '1' && d <= '6') {
            *key = (xeen_native_key)(XKEY_F1 + (d - '1'));
        } else if (d == 'L') {
            *key = XKEY_LEFT;
        } else if (d == 'R') {
            *key = XKEY_RIGHT;
        } else {
            return REPLAY_INVALID_PREFIXED;
        }
        return REPLAY_PREFIXED;
    }
    /* e.g. 'z', unlisted in CS:2F7D: 39161 -> 3934F */
    return replay_raw_letter(ch, key) ? REPLAY_KEY : REPLAY_INVALID;
}
/** A modal wait's characters (CHOICE: 2F5B1's numeric choice is open; KEY unchanged for '*'). '~' is the
 * feeder's (an idle poll). */
static replay_input modal_input(char ch, int choice, xeen_native_key *key)
{
    static const replay_key keys[] = {
            {'A', XKEY_ENTER},
            {'U', XKEY_UP},
            {'S', XKEY_SPACE},
            {'L', XKEY_LEFT},
            {'R', XKEY_RIGHT},
            {'E', XKEY_ESCAPE},
            {'Y', XKEY_Y},
            {'N', XKEY_N},
            {'X', XKEY_GAME_ESCAPE},
            /* A dialog's closing key is redispatched by 17C10: the exploration letters (Z Rest, F Shoot, K the D
             * key). */
            {'7', XKEY_F7},
            {'8', XKEY_F8},
            {'Z', XKEY_R},
            {'F', XKEY_S},
            {'K', XKEY_D},
            {'B', XKEY_B},
            {'C', XKEY_C},
            {'I', XKEY_I},
            {'Q', XKEY_Q},
            {'M', XKEY_M},
            {'V', XKEY_V},
            {'W', XKEY_W},
            {'T', XKEY_TAB}};
    if (ch == '*') {
        return REPLAY_CLICK;
    }
    if (choice && ch >= '1' && ch <= '9') {
        (void)panel_key(ch, key); /* 2F5B1's keys are digits */
        return REPLAY_KEY;
    }
    if (replay_key_find(keys, REPLAY_KEYS(keys), ch, key)) {
        return REPLAY_KEY;
    }
    /* 'x' the X key, not the game Esc of 'X' */
    return replay_command_key(ch, key);
}
/* The number the replay's next command carries in its labels (command%u-before, -handle, -events, the saves): the
 * commands fed so far (the '|' markers not counted), plus one. */
static unsigned next_command_number(const explore_replay *replay)
{
    return (unsigned)(replay->command_index - replay->markers) + 1u;
}
/* One pass of the run loop: the game's view (inspected once, at the pass's start; the feeders only read it) and the
 * run's state its stages feed and log. RESULT is main's exit status. */
typedef struct {
    explore_replay *replay;
    const explore_options *opt;
    xeen_game *g;
    xeen_platform *platform;
    host_resources *res;
    xeen_game_view v;
    int *result;
} host_pass_state;
/* The "before" evidence of exploration command LABEL: a session's first command marks the RNG count it starts from
 * when the session's idle scene drew RNG (a colour mode 2/5 monster in view), then the DS and sprite snapshots. 0
 * when a write fails. */
static int exploration_before(const host_pass_state *s, const char *label)
{
    explore_replay *replay = s->replay;
    const random_source *random = &s->res->random;
    if (!replay->ready_marked) {
        replay->ready_marked = 1;
        if (random->calls != replay->ready_calls) {
            printf("rng-mark %s rng=%lu\n", label, (unsigned long)random->calls);
        }
    }
    return host_write_snapshot(s->opt->evidence, label, xeen_game_ds(s->g), 65536) &&
           host_snapshot_sprites(s->opt->evidence, label, s->g);
}
/* The evidence after a command's KEY, in this order: the driver's last sprite handle (4A8A5's CS:03D0: a replay
 * starting here numbers its loads after it), then a Tab's saves (whose snapshot holds the event store too) or, for
 * any other key, the event store (the original replay's 9C62 scripts mid-session). 0 when a write fails. */
static int exploration_after(const host_pass_state *s, xeen_native_key key)
{
    const explore_replay *replay = s->replay;
    const explore_options *opt = s->opt;
    xeen_game *g = s->g;
    char label[40];
    if (opt->evidence && xeen_game_sprites(g)) {
        uint8_t last[2] = {(uint8_t)xeen_game_sprites(g)->last,
                           (uint8_t)(xeen_game_sprites(g)->last >> 8)};
        snprintf(label, sizeof label, "command%u-handle", next_command_number(replay));
        if (!host_write_snapshot(opt->evidence, label, last, 2)) {
            return 0;
        }
    }
    if (key == XKEY_TAB) {
        snprintf(label, sizeof label, "command%u", next_command_number(replay));
        return host_snapshot_saves(opt->evidence, label, g, &s->res->directory, 1);
    }
    {
        size_t en = 0;
        const uint8_t *eb = xeen_game_event_store(g, &en);
        snprintf(label, sizeof label, "command%u-events", next_command_number(replay));
        return !eb || host_write_snapshot(opt->evidence, label, eb, en);
    }
}
/**
 * Feeds the replay's next exploration command at the 17B09 poll: its "before" evidence (exploration_before), the key
 * or click itself (none for the '~' idle convention), then the evidence after it (exploration_after). The first
 * command of a session is fed after one idle scene (17B24 -> 17AFC, the 1B503 with no key; feed_exploration_poll).
 * Returns HOST_DONE on an invalid key (invalid_key: or the next pass when skipped) or a failed write, else HOST_NEXT
 * with the command pending.
 */
static host_flow feed_exploration_command(const host_pass_state *s)
{
    explore_replay *replay = s->replay;
    xeen_game *g = s->g; /* host_key's call sites keep G and REPLAY: the test fixtures' queue hook reads them */
    xeen_native_key key = XKEY_UP;
    char ch = s->opt->commands[replay->command_index];
    char label[40];
    replay_input input = exploration_input(ch, &key);
    (void)g;
    if (input == REPLAY_INVALID) {
        return invalid_key(replay, "invalid replay key", ch);
    }
    snprintf(label, sizeof label, "command%u-before", next_command_number(replay));
    if (!exploration_before(s, label) ||
        !(input == REPLAY_NO_KEY || (input == REPLAY_CLICK ? host_click(s->platform) : host_key(s->platform, key))) ||
        !exploration_after(s, key)) {
        return HOST_DONE;
    }
    ++replay->command_index;
    replay->pending = 1;
    return HOST_NEXT;
}
/* A session's first exploration poll: the "input-ready" record ("loaded-input-ready" after a Load or a new game),
 * the RNG count the first command's mark compares with, and the DS snapshot ("initial-ds", or "loadedN-ds"). 0
 * when the snapshot cannot be written. */
static int log_input_ready(const host_pass_state *s)
{
    explore_replay *replay = s->replay;
    const random_source *random = &s->res->random;
    char label[40];
    replay->ready = 1;
    host_print_record(s->g, s->res->p, replay->loads ? "loaded-input-ready" : "input-ready", random->calls);
    replay->ready_calls = random->calls;
    replay->ready_marked = 0;
    snprintf(label, sizeof label, "loaded%u-ds", replay->loads);
    return host_write_snapshot(s->opt->evidence, replay->loads ? label : "initial-ds", xeen_game_ds(s->g), 65536);
}
/* The records of the commands completed since the last poll: a window's "command-complete" for each accepted command
 * (logged once), then the pending replay command's "commandN-complete" and, headless after a fight's cleanup, the
 * RNG state. */
static void log_completed_commands(const host_pass_state *s)
{
    explore_replay *replay = s->replay;
    const random_source *random = &s->res->random;
    const xeen_game_view *v = &s->v;
    if (!s->opt->headless && v->accepted_commands != replay->logged_commands) {
        replay->logged_commands = v->accepted_commands;
        host_print_record(s->g, s->res->p, "command-complete", random->calls);
    }
    if (replay->pending) {
        char label[40];
        snprintf(label, sizeof label, "command%u-complete",
                 (unsigned)(replay->command_index - replay->markers));
        host_print_record(s->g, s->res->p, label, random->calls);
        if (s->opt->headless && v->combat_cleanup_complete) {
            printf("combat-rng-state=%u,%u,%u calls=%u\n", random->state.words[0],
                   random->state.words[1], random->state.words[2], random->calls);
        }
        replay->pending = 0;
    }
}
/**
 * The exploration command poll 17B09: a session's first poll logs its readiness (log_input_ready), every poll the
 * commands completed since the last (log_completed_commands); headless, once the session's idle scene has run, the
 * replay's next key or click is fed here (one command per poll: feed_exploration_command), and the end of the script
 * ends the session (HOST_BREAK, status 0).
 */
static host_flow feed_exploration_poll(const host_pass_state *s)
{
    explore_replay *replay = s->replay;
    const explore_options *opt = s->opt;
    if (!replay->ready && !log_input_ready(s)) {
        return HOST_DONE;
    }
    log_completed_commands(s);
    if (opt->headless && opt->commands) {
        if (!replay->idle_done && s->v.idle_scenes) {
            replay->idle_done = 1;
        }
        if (replay->idle_done) {
            if (opt->commands[replay->command_index]) {
                return feed_exploration_command(s);
            }
            *s->result = 0;
            return HOST_BREAK;
        }
    }
    return HOST_NEXT;
}
/* With --diagnostic-state, a defeat's game-over summary at the menu's entry: its frames, skipped draws and its
 * first 12 events. */
static void print_game_over(const xeen_game_view *v)
{
    unsigned i;
    if (!v->game_over_frames || !host_diagnostic_state()) {
        return;
    }
    printf("game-over frames=%u skipped-draws=%u events=", v->game_over_frames, v->game_over_skipped_draws);
    for (i = 0; i < v->game_over_event_count && i < 12u; ++i) {
        printf("%s%s", i ? "," : "", v->game_over_events[i]);
    }
    printf("\n");
}
/**
 * The title-screen main menu 1660E was entered (by the 15F9C outer loop after a defeat: 1596F, 15647, 1624F; or at
 * program start): logs it once ("main-menu", the game-over summary, the DS snapshots "main-menu-ds" and
 * "main-menu-cN-ds" and the frame). Headless input continues into the menu only after a '|' marker (not an input);
 * otherwise the session ends here (HOST_BREAK, status 0) with any remaining input undelivered.
 */
static host_flow log_main_menu_entry(const host_pass_state *s)
{
    explore_replay *replay = s->replay;
    const explore_options *opt = s->opt;
    const uint8_t *ds = xeen_game_ds(s->g);
    char label[40];
    host_print_record(s->g, s->res->p, "main-menu", s->res->random.calls);
    replay->menu_logged = 1;
    print_game_over(&s->v);
    snprintf(label, sizeof label, "main-menu-c%u-ds", (unsigned)(replay->command_index - replay->markers));
    if (!host_write_snapshot(opt->evidence, "main-menu-ds", ds, 65536) ||
        !host_write_snapshot(opt->evidence, label, ds, 65536) ||
        !host_write_snapshot(opt->evidence, "main-menu-frame", s->res->p->framebuffer, XEEN_PRESENT_PIXELS)) {
        return HOST_DONE;
    }
    if (opt->headless) {
        if (!opt->commands || opt->commands[replay->command_index] != '|') {
            *s->result = 0;
            return HOST_BREAK;
        }
        ++replay->command_index;
        ++replay->markers;
    }
    return HOST_NEXT;
}
/* A menu command's evidence before its key ("menu-cN-...", N the command's number, markers excluded): the record, the
 * DS, the displayed page and the palette. 0 when a write fails. */
static int menu_before(const host_pass_state *s)
{
    const xeen_presentation *p = s->res->p;
    unsigned n = next_command_number(s->replay);
    char label[40];
    snprintf(label, sizeof label, "menu-c%u-before", n);
    host_print_record(s->g, s->res->p, label, s->res->random.calls);
    if (!host_write_snapshot(s->opt->evidence, label, xeen_game_ds(s->g), 65536)) {
        return 0;
    }
    snprintf(label, sizeof label, "menu-c%u-frame", n);
    if (!host_write_snapshot(s->opt->evidence, label, p->pages[p->displayed_page], XEEN_PRESENT_PIXELS)) {
        return 0;
    }
    snprintf(label, sizeof label, "menu-c%u-palette", n);
    return host_write_snapshot(s->opt->evidence, label, p->palette, DISPLAY_PALETTE_BYTES);
}
/**
 * A main-menu poll armed for input: queues the replay's next menu key or click (menu_input; one per menu poll
 * session, before its first 189E3) after its evidence (menu_before). An unknown key ends the run with its diagnostic
 * (never skipped); the end of the script ends the session (HOST_BREAK, status 0).
 */
static host_flow feed_menu_command(const host_pass_state *s)
{
    explore_replay *replay = s->replay;
    xeen_game *g = s->g; /* host_key's call sites keep G and REPLAY: the test fixtures' queue hook reads them */
    char ch = s->opt->commands[replay->command_index];
    xeen_native_key key = XKEY_ENTER;
    (void)g;
    if (!ch) {
        *s->result = 0;
        return HOST_BREAK;
    }
    switch (menu_input(s->opt->commands + replay->command_index, &key)) {
        case REPLAY_PREFIXED:
            ++replay->command_index;
            ++replay->markers;
            break;
        case REPLAY_INVALID:
            host_report(HOST_ERROR, "invalid menu replay key %c", ch);
            return HOST_DONE;
        default:
            break;
    }
    if (!menu_before(s) || !(ch == '*' ? host_click(s->platform) : host_key(s->platform, key))) {
        return HOST_DONE;
    }
    ++replay->command_index;
    replay->menu_armed = 0;
    return HOST_NEXT;
}
/** A control-panel poll's characters at AT (AT[1] read only after a '^'): a panel key (panel_key); '*' a click
 * (--clicks; KEY Enter); '^' and a digit 1..6 F1..F6 (a member: the inn's Remove), '^' and 7 8 F7 F8 (CFh D0h, the
 * keys the window sends for them: the character view's F-keys 44E39 take F1..F8, so F7 and F8 are a party's absent
 * members; Phase 322, a test input only), '^' and U D L R the arrows (F2 F3 F0 F1: the inn's scroll, creation's
 * highlight and slot), '^' and H E P N I Home End PgUp PgDn Ins (DCh DDh DEh DFh E0h: the Quests viewer, the spell
 * lists). F9 (D1h) has no token: the harness pairs it as '^9'. The '^' is a marker, not an input. ('~', one idle
 * poll, is the feeder's.) Phase 330, a developer test input for the wheel add-ons (mouse_scroll, world_map): '^' and
 * '{' or '}' one wheel notch up or down at the next --clicks point, with no key (the original has no wheel; the
 * original-byte harness's grammar, harness/inputs.py, rejects these tokens). */
static replay_input panel_input(const char *at, xeen_native_key *key)
{
    static const replay_key prefixed[] = {
            {'7', XKEY_F7},
            {'8', XKEY_F8},
            {'U', XKEY_UP},
            {'D', XKEY_DOWN},
            {'L', XKEY_LEFT},
            {'R', XKEY_RIGHT},
            {'H', XKEY_HOME},
            {'E', XKEY_END},
            {'P', XKEY_PGUP},
            {'N', XKEY_PGDN},
            {'I', XKEY_INSERT}};
    char ch = at[0];
    if (ch == '^') {
        char d = at[1];
        if (d >= '1' && d <= '6') {
            *key = (xeen_native_key)(XKEY_F1 + (d - '1'));
            return REPLAY_PREFIXED;
        }
        if (d == '{' || d == '}') {
            *key = d == '{' ? XKEY_UP : XKEY_DOWN;
            return REPLAY_WHEEL;
        }
        return replay_key_find(prefixed, REPLAY_KEYS(prefixed), d, key) ? REPLAY_PREFIXED : REPLAY_INVALID_PREFIXED;
    }
    if (ch == '*') {
        *key = XKEY_ENTER;
        return REPLAY_CLICK;
    }
    return panel_key(ch, key) ? REPLAY_KEY : REPLAY_INVALID;
}
/* The record and the DS and sprite snapshots before a panel command ("panel-cN-before", N the command's number,
 * markers excluded): 0 when a snapshot could not be written. */
static int panel_before(const explore_replay *replay, const explore_options *opt, xeen_game *g,
                        xeen_presentation *p, const random_source *random)
{
    char label[40];
    snprintf(label, sizeof label, "panel-c%u-before", next_command_number(replay));
    host_print_record(g, p, label, random->calls);
    return host_write_snapshot(opt->evidence, label, xeen_game_ds(g), 65536) &&
           host_snapshot_sprites(opt->evidence, label, g);
}
/* '~' at a location's or any other panel poll: one idle poll (no key; the location loop's tick and keeper animation,
 * or the panel's own scene or tick, run), so the next input waits for the following poll rather than forever: the
 * "before" evidence, then the idle mark at this poll. Original: 26E65's 189E3 in the 26E60 loop (4EA1C, 26978). */
static host_flow feed_panel_idle(const host_pass_state *s)
{
    explore_replay *replay = s->replay;
    if (!panel_before(replay, s->opt, s->g, s->res->p, &s->res->random)) {
        return HOST_DONE;
    }
    ++replay->command_index;
    replay->idle_mark = s->v.panel_polls;
    replay->idle_pending = 1;
    replay->idle_boundary = s->v.lifecycle_boundary;
    replay->panel_armed = 0;
    return HOST_PANEL_WAIT;
}
/* A panel command's evidence before its key, in this order: the "before" evidence (panel_before), the inn's roster
 * image (DOS memory in the original) when the game has one, and the displayed page. 0 when a write fails. */
static int panel_evidence(const host_pass_state *s)
{
    const explore_replay *replay = s->replay;
    xeen_presentation *p = s->res->p;
    const uint8_t *roster;
    size_t rn = 0;
    char label[40];
    if (!panel_before(replay, s->opt, s->g, p, &s->res->random)) {
        return 0;
    }
    roster = xeen_game_roster(s->g, &rn);
    snprintf(label, sizeof label, "panel-c%u-roster", next_command_number(replay));
    if (roster && !host_write_snapshot(s->opt->evidence, label, roster, rn)) {
        return 0;
    }
    snprintf(label, sizeof label, "panel-c%u-frame", next_command_number(replay));
    return host_write_snapshot(s->opt->evidence, label, p->pages[p->displayed_page], XEEN_PRESENT_PIXELS);
}
/* The replay's next panel key or click (panel_input), after its evidence. An unknown character ends the run with its
 * diagnostic (the panel's invalid keys are never skipped, unlike invalid_key's). HOST_NEXT once the key is queued,
 * HOST_DONE on an invalid key or a failed write. */
static host_flow feed_panel_key(const host_pass_state *s)
{
    explore_replay *replay = s->replay;
    xeen_game *g = s->g; /* host_key's call sites keep G and REPLAY: the test fixtures' queue hook reads them */
    const char *at = s->opt->commands + replay->command_index;
    xeen_native_key key = XKEY_ENTER;
    replay_input input = panel_input(at, &key);
    (void)g;
    if (input == REPLAY_INVALID_PREFIXED) {
        host_report(HOST_ERROR, "invalid panel replay key ^%c", at[1]);
        return HOST_DONE;
    }
    if (input == REPLAY_INVALID) {
        host_report(HOST_ERROR, "invalid panel replay key %c", at[0]);
        return HOST_DONE;
    }
    if (input == REPLAY_WHEEL) {
        /* No key: the notch is the add-on's to turn into one at this poll, else the poll idles ('~'). */
        if (!host_wheel(s->platform, key == XKEY_UP ? 1 : -1)) {
            host_report(HOST_ERROR, "panel replay wheel ^%c without a --clicks point", at[1]);
            return HOST_DONE;
        }
        ++replay->command_index;
        ++replay->markers;
        return feed_panel_idle(s);
    }
    if (input == REPLAY_PREFIXED) {
        ++replay->command_index;
        ++replay->markers;
    }
    if (!panel_evidence(s) || !(input == REPLAY_CLICK ? host_click(s->platform) : host_key(s->platform, key))) {
        return HOST_DONE;
    }
    ++replay->command_index;
    replay->panel_armed = 0;
    return HOST_NEXT;
}
/**
 * A control-panel poll armed for input. A window logs the pending panel once ("panel-pending"); headless, the
 * replay's next character there: '~' one idle poll (feed_panel_idle: HOST_PANEL_WAIT, the input waits for a later
 * poll), else a key or click (feed_panel_key). The end of the script ends the session (HOST_BREAK, status 0).
 */
static host_flow feed_panel_command(const host_pass_state *s)
{
    explore_replay *replay = s->replay;
    if (!s->opt->headless) {
        if (!replay->panel_logged) {
            host_print_record(s->g, s->res->p, "panel-pending", s->res->random.calls);
            replay->panel_logged = 1;
        }
    } else if (s->opt->commands) {
        char ch = s->opt->commands[replay->command_index];
        if (!ch) {
            *s->result = 0;
            return HOST_BREAK;
        }
        return ch == '~' ? feed_panel_idle(s) : feed_panel_key(s);
    }
    replay->panel_armed = 0;
    return HOST_NEXT;
}
/* The pending combat command's completion at the next command poll: its record "commandN-complete" (N the command's
 * number), the DS and the page background ("commandN-complete-frame"); the command is no longer pending. 0 when a
 * write fails. (snapshot_completed_command, a monster turn's, also prints the RNG state headless.) */
static int complete_combat_command(const host_pass_state *s)
{
    explore_replay *replay = s->replay;
    char label[48];
    unsigned n = (unsigned)(replay->command_index - replay->markers);
    snprintf(label, sizeof label, "command%u-complete", n);
    host_print_record(s->g, s->res->p, label, s->res->random.calls);
    if (!host_write_snapshot(s->opt->evidence, label, xeen_game_ds(s->g), 65536)) {
        return 0;
    }
    snprintf(label, sizeof label, "command%u-complete-frame", n);
    if (!host_write_snapshot(s->opt->evidence, label, s->res->background, XEEN_PRESENT_PIXELS)) {
        return 0;
    }
    replay->pending = 0;
    return 1;
}
/* The combat poll's record "combat-ready" (headless with the RNG state) and, at the fight's first poll, the DS
 * snapshots "combat-ready-ds" and "combat-ready-cN-ds" and the page background. 0 when a write fails. */
static int log_combat_ready(const host_pass_state *s)
{
    explore_replay *replay = s->replay;
    const random_source *random = &s->res->random;
    const uint8_t *ds = xeen_game_ds(s->g);
    char label[40];
    replay->logged_combat = s->v.combat_commands;
    host_print_record(s->g, s->res->p, "combat-ready", random->calls);
    if (s->opt->headless) {
        printf("combat-rng-state=%u,%u,%u calls=%u\n", random->state.words[0], random->state.words[1],
               random->state.words[2], random->calls);
    }
    if (s->v.combat_commands) {
        return 1;
    }
    snprintf(label, sizeof label, "combat-ready-c%u-ds", (unsigned)(replay->command_index - replay->markers));
    return host_write_snapshot(s->opt->evidence, "combat-ready-ds", ds, 65536) &&
           host_write_snapshot(s->opt->evidence, label, ds, 65536) &&
           host_write_snapshot(s->opt->evidence, "combat-ready-frame", s->res->background, XEEN_PRESENT_PIXELS);
}
/* The replay's next combat key or click (combat_input) after its "before" evidence (DS and sprites); the command is
 * then pending. A '^' with a key combat does not take ends the run with its diagnostic; another unknown key goes to
 * invalid_key (skipped with XEEN_TEST_SKIP_INVALID_KEYS). HOST_NEXT once queued, else HOST_DONE (or HOST_CONTINUE
 * for a skipped key). */
static host_flow feed_combat_key(const host_pass_state *s)
{
    explore_replay *replay = s->replay;
    xeen_game *g = s->g; /* host_key's call sites keep G and REPLAY: the test fixtures' queue hook reads them */
    const char *at = s->opt->commands + replay->command_index;
    char ch = at[0];
    xeen_native_key key = XKEY_A;
    char label[40];
    switch (combat_input(at, &key)) {
        case REPLAY_PREFIXED:
            ++replay->command_index;
            ++replay->markers;
            break;
        case REPLAY_INVALID_PREFIXED:
            host_report(HOST_ERROR, "invalid combat replay key ^%c", at[1]);
            return HOST_DONE;
        case REPLAY_INVALID:
            return invalid_key(replay, "invalid combat replay key", ch);
        default:
            break;
    }
    snprintf(label, sizeof label, "combat-command%u-before", next_command_number(replay));
    if (!host_write_snapshot(s->opt->evidence, label, xeen_game_ds(g), 65536) ||
        !host_snapshot_sprites(s->opt->evidence, label, g) ||
        !(ch == '*' ? host_click(s->platform) : host_key(s->platform, key))) {
        return HOST_DONE;
    }
    ++replay->command_index;
    replay->pending = 1;
    return HOST_NEXT;
}
/**
 * The combat command poll 3912C: completes the pending command's evidence (complete_combat_command), logs the poll
 * (log_combat_ready: every poll headless, a window's once per new poll), then, headless, feeds the replay's next
 * combat key there (feed_combat_key); the end of the script ends the session (HOST_BREAK, status 0).
 */
static host_flow feed_combat_command(const host_pass_state *s)
{
    explore_replay *replay = s->replay;
    const explore_options *opt = s->opt;
    if (replay->pending && !complete_combat_command(s)) {
        return HOST_DONE;
    }
    /* No command is pending from here on. */
    if ((opt->headless || s->v.combat_commands != replay->logged_combat) && !log_combat_ready(s)) {
        return HOST_DONE;
    }
    if (!opt->headless || !opt->commands) {
        return HOST_NEXT;
    }
    if (!opt->commands[replay->command_index]) {
        *s->result = 0;
        return HOST_BREAK;
    }
    return feed_combat_key(s);
}
/* A new presentation inside the wait, or a yes/no key it ignored (2937A goes on polling), lets the modal feeder queue
 * the next key afresh: its visits start again. */
static void restart_modal_visits(explore_replay *replay, const xeen_game_view *v)
{
    if (v->presentations != replay->modal_presentations) {
        replay->modal_presentations = v->presentations;
        replay->modal_queued = 0;
        replay->modal_visits = 0;
    }
    if (v->confirm_ignored_keys != replay->modal_ignored) {
        replay->modal_ignored = v->confirm_ignored_keys;
        replay->modal_queued = 0;
        replay->modal_visits = 0;
    }
}
/* A window's first modal pass: the "modal-pending" record and the frame and DS snapshots (in that order), once. 0
 * when a write fails. */
static int log_window_modal(const host_pass_state *s)
{
    xeen_presentation *p = s->res->p;
    host_print_record(s->g, p, "modal-pending", s->res->random.calls);
    s->replay->logged_modal = 1;
    return host_write_snapshot(s->opt->evidence, "modal-frame", p->framebuffer, XEEN_PRESENT_PIXELS) &&
           host_write_snapshot(s->opt->evidence, "modal-ds", xeen_game_ds(s->g), 65536);
}
/* A headless modal visit's evidence: the "modal-pending" record, the DS and frame snapshots (in that order) and the
 * visit's own DS ("modal-cN-vV-ds", V the visit, from 1). 0 when a write fails. */
static int modal_visit_evidence(const host_pass_state *s)
{
    const explore_replay *replay = s->replay;
    xeen_presentation *p = s->res->p;
    const uint8_t *ds = xeen_game_ds(s->g);
    char label[40];
    host_print_record(s->g, p, "modal-pending", s->res->random.calls);
    if (!host_write_snapshot(s->opt->evidence, "modal-ds", ds, 65536) ||
        !host_write_snapshot(s->opt->evidence, "modal-frame", p->framebuffer, XEEN_PRESENT_PIXELS)) {
        return 0;
    }
    snprintf(label, sizeof label, "modal-c%u-v%u-ds", (unsigned)(replay->command_index - replay->markers),
             replay->modal_visits + 1u);
    return host_write_snapshot(s->opt->evidence, label, ds, 65536);
}
/**
 * A headless modal visit with no key queued yet: its evidence (modal_visit_evidence), then the replay's next key
 * after the visits a typed-ahead key needs (4, or 1 with XEEN_MODAL_TYPEAHEAD; HOST_MODAL_WAIT until then). The end
 * of the script ends the session (HOST_BREAK, status 0: an honest suspended event); '~' is one idle poll with nothing
 * queued (HOST_MODAL_WAIT); a key the wait does not take goes to invalid_key.
 */
/* A modal wait's replay key is fed at its fourth visit (the first with XEEN_MODAL_TYPEAHEAD); a modal idle ('~') that
 * ends sets the count to one visit short. */
#define MODAL_KEY_VISIT 4u
static host_flow feed_modal_visit(const host_pass_state *s)
{
    explore_replay *replay = s->replay;
    xeen_game *g = s->g; /* host_key's call sites keep G and REPLAY: the test fixtures' queue hook reads them */
    char ch = s->opt->commands[replay->command_index];
    xeen_native_key key = XKEY_ENTER;
    (void)g;
    if (!modal_visit_evidence(s)) {
        return HOST_DONE;
    }
    if (++replay->modal_visits < (modal_typeahead ? 1u : MODAL_KEY_VISIT)) {
        return HOST_MODAL_WAIT;
    }
    if (!ch) {
        *s->result = 0;
        return HOST_BREAK;
    }
    if (ch == '~') {
        /* '~' at a modal wait: one idle poll, no key while time advances. Nothing is queued: the wait's next
         * key read finds none, and its timer tick and the loop's own scene run.
         * Original: 189E3 with F5FE 0, 4EA1C; the scenes of 316DC (31786 1B503(1)), 18F2F (18FA1), 337D7
         * (33815), 2F170 and 2F2FF. */
        ++replay->command_index;
        replay->modal_queued = 1;
        replay->modal_idle = 1;
        replay->modal_idle_mark = s->v.modal_polls;
        return HOST_MODAL_WAIT;
    }
    if (modal_input(ch, s->v.choice, &key) == REPLAY_INVALID) {
        return invalid_key(replay, "invalid modal replay key", ch);
    }
    if (!(ch == '*' ? host_click(s->platform) : host_key(s->platform, key))) {
        return HOST_DONE;
    }
    ++replay->command_index;
    replay->modal_queued = 1;
    return HOST_NEXT;
}
/**
 * A modal wait (a message, a choice, a scroll or a menu inside an event or a service): restarts the feeder's visits
 * on a new presentation or an ignored key (restart_modal_visits), logs a window's wait once (log_window_modal),
 * refreshes the displayed page with the cursor, and headless feeds the replay's next key (feed_modal_visit; returns
 * HOST_MODAL_WAIT while the key is held for a later visit).
 */
static host_flow feed_modal_wait(const host_pass_state *s)
{
    explore_replay *replay = s->replay;
    xeen_presentation *p = s->res->p;
    restart_modal_visits(replay, &s->v);
    if (!s->opt->headless && !replay->logged_modal && !log_window_modal(s)) {
        return HOST_DONE;
    }
    xeen_platform_show_cursor(s->platform, p->enabled);
    if (!xeen_platform_refresh(s->platform, HOST_PAGE(p->pages[p->displayed_page], xeen_presentation_dac(p)),
                               xeen_presentation_dac(p))) {
        return HOST_DONE;
    }
    if (s->opt->headless && s->opt->commands && !replay->modal_queued) {
        return feed_modal_visit(s);
    }
    return HOST_NEXT;
}
/* XEEN_HOST_TIMING's record of one host step: the pass's time in the step histogram; a run of consecutive passes
 * that end without the host's 1 ms wait (the game computing: its passes, its longest stretch); and a slow pass (20 ms
 * or more) listed with its boundaries BEFORE and after the step. */
static void record_pass_timing(xeen_game *g, uint32_t before)
{
    uint64_t now = xeen_platform_now_ns(), t = now - timing.pass_start;
    int c = host_compute(g);
    ++timing.passes;
    timing_add(timing.steps, &timing.step_max, &timing.nsteps, t);
    if (c) {
        if (!timing.run_passes) {
            timing.run_start = timing.pass_start;
        }
        ++timing.run_passes;
        ++timing.spins;
        if (now - timing.run_start > timing.run_max) {
            timing.run_max = now - timing.run_start;
        }
        if (timing.run_passes > timing.run_passes_max) {
            timing.run_passes_max = timing.run_passes;
        }
    } else {
        timing.run_passes = 0;
    }
    if (t >= UINT64_C(20000000)) {
        fprintf(timing.f, "pass b=%05x next=%05x compute=%d step_us=%llu hold_us=%llu\n", (unsigned)before,
                (unsigned)xeen_game_inspect(g).lifecycle_boundary, c, (unsigned long long)(t / 1000u),
                (unsigned long long)(timing.hold_ns / 1000u));
    }
}
/* The session stopped: the "stopped" record, the DS and page background snapshots, then the exit status: 2 for the
 * driver's fatal exit (INT 21h 4C02h), the program's own (1061F(F606), INT 21h 4Ch) for its exit, 3 for the
 * XEEN.EXE hand-off (otherwise main's status stays). Always HOST_DONE. */
static host_flow report_stopped(const host_pass_state *s)
{
    xeen_game *g = s->g;
    host_print_record(g, s->res->p, "stopped", s->res->random.calls);
    if (!host_write_snapshot(s->opt->evidence, "stopped-ds", xeen_game_ds(g), 65536) ||
        !host_write_snapshot(s->opt->evidence, "stopped-frame", s->res->background, XEEN_PRESENT_PIXELS)) {
        return HOST_DONE;
    }
    if (xeen_game_driver_exit(g)) {
        *s->result = 2;
    }
    if (xeen_game_program_exit(g)) {
        *s->result = xeen_game_ds(g)[0xf606];
    }
    if (xeen_game_handoff(g)) {
        *s->result = 3;
    }
    return HOST_DONE;
}
/**
 * One host step (run_host_step) with its XEEN_HOST_TIMING record (record_pass_timing). When the session stops,
 * writes the stop evidence and sets the exit status (report_stopped) and returns HOST_DONE; else HOST_NEXT.
 */
static host_flow run_host_pass(const host_pass_state *s)
{
    uint32_t before = s->v.lifecycle_boundary;
    int ok = run_host_step(s->g, s->platform);
    if (timing.f) {
        record_pass_timing(s->g, before);
    }
    return ok ? HOST_NEXT : report_stopped(s);
}
/**
 * Phase 270: the startup settings from the command line and wox.cfg, resolved once before any input
 * is read. WOX gets the defaults, then a window's wox.cfg (load_window_config: beside the executable
 * or --wox-config; its add-ons and bug fixes enabled) or, headless, only an explicit --wox-config
 * (its [display] and [files] data_dir; its audio for --audio-capture). Then, into OPT: the display
 * settings ([display] scale, aspect, authentic_cursor); the data directory (--data-dir, else an explicit
 * [files] data_dir, else "auto" = the process's working directory "."), which must be an existing
 * directory; the saves' defaults (DIR/XEEN01.WOX, DIR/DARK01.WOX); and the start (--menu or
 * --no-menu, else a window's main menu, a headless run's saves). wox.cfg is found without the data
 * directory, so the lookup cannot depend on it. Returns 0, or main's exit status: 1 when wox.cfg
 * cannot be located or the data directory is not a directory, 2 when wox.cfg does not load; -1 when the
 * player declined to create a missing wox.cfg (load_window_config).
 */
static int resolve_startup_config(explore_options *opt, const char *argv0, xeen_wox_config *wox)
{
    static char cloud[1100], dark[1100];
    struct stat st;
    const char *source;
    xeen_wox_config_defaults(wox, ".");
    if (!opt->headless) {
        int status = load_window_config(opt, argv0, wox);
        if (status) {
            return status;
        }
    } else if (opt->wox_path) {
        char why[600]; /* headless reads only an explicit wox.cfg */
        if (!xeen_wox_config_load(opt->wox_path, wox, why, sizeof why)) {
            host_report(HOST_ERROR, "%s", why);
            return 2;
        }
    }
    for (unsigned k = 0; k < XEEN_WEP_KINDS; ++k) { /* add-on wizard_eye_plus's styles (also for --addon) */
        host_addons.wizard_eye_plus_style[k] = (uint8_t)wox->addon_wizard_eye_plus_style[k];
    }
    opt->scale = wox->scale;
    opt->aspect = wox->aspect;
    opt->locked_scale = wox->locked_scale;
    opt->authentic_cursor = wox->authentic_cursor;
    if (opt->data_override) {
        opt->dir = opt->data_override;
        source = "--data-dir";
    } else if (!wox->data_auto) {
        opt->dir = wox->data;
        source = "wox.cfg [files] data_dir";
    } else {
        opt->dir = ".";
        source = "wox.cfg [files] data_dir = auto, the working directory";
    }
    if (stat(opt->dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        host_report(HOST_ERROR, "game data directory not found: %s (from %s)", opt->dir, source);
        return 1;
    }
    /* Phase 287: XEEN.CFG's directory: an explicit [files] xeen_cfg_dir (an existing directory), else (auto) the
     * data directory just resolved (after --data-dir). Never the save directory. */
    if (!wox->xeen_cfg_auto) {
        if (stat(wox->xeen_cfg_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
            host_report(HOST_ERROR, "XEEN.CFG location not found: %s (wox.cfg [files] xeen_cfg_dir names the "
                                    "directory holding XEEN.CFG)", wox->xeen_cfg_dir);
            return 1;
        }
        opt->xeen_cfg_dir = wox->xeen_cfg_dir;
        opt->xeen_cfg_explicit = 1;
    } else {
        opt->xeen_cfg_dir = opt->dir;
    }
    if (!opt->cloud_save) {
        snprintf(cloud, sizeof cloud, "%s/XEEN01.WOX", opt->dir);
        opt->cloud_save = cloud;
    }
    if (!opt->dark_save) {
        snprintf(dark, sizeof dark, "%s/DARK01.WOX", opt->dir);
        opt->dark_save = dark;
    }
    opt->menu_start = opt->menu_choice >= 0 ? opt->menu_choice : !opt->headless;
    return 0;
}
/**
 * Puts the session's save files in a writable directory overlay. A window always has one: the
 * original's slot files go into its own DOS directory, which never refuses a create, so a read-only
 * one made every new game, inn change and save end at 19629 (14033's failure, 2C80D).
 * Phase 287, in order:
 * - the command line's --save-dir DIR: as before (created when missing; a failure is noted and the session stays
 *   read-only);
 * - wox.cfg's explicit [files] save_dir (a window's, or a headless run's --wox-config): it must be an existing
 *   writable directory, else the host refuses to start (returns 1: no fallback to ".", nothing created);
 * - auto (the default): a window saves into the process's working directory "."; a headless run has none
 *   (read-only, as before: the protected data directory is never written).
 * Sets OPT's save_dir and notes where the saves go. The data directory and wox.cfg are unaffected.
 */
static int configure_save_directory(explore_options *opt, const xeen_wox_config *wox)
{
    static char chosen[1024];
    char why[300];
    if (opt->save_dir) {
        if (xeen_wox_save_directory(opt->save_dir, chosen, sizeof chosen)) {
            opt->save_dir = chosen;
        } else {
            host_report(HOST_WARN, "note: no writable save directory (%s); saving the game will fail at 19629",
                        opt->save_dir);
        }
        return 0;
    }
    if (!wox->save_dir_auto) {
        if (!xeen_wox_save_dir_check(wox->save_dir, why, sizeof why)) {
            host_report(HOST_ERROR, "save directory %s (wox.cfg [files] save_dir): %s", wox->save_dir, why);
            return 1;
        }
        opt->save_dir = wox->save_dir;
        host_report(HOST_INFO, "note: saving into %s (wox.cfg [files] save_dir; --save-dir overrides)", opt->save_dir);
        return 0;
    }
    if (!opt->headless) {
        if (!xeen_wox_save_dir_check(".", why, sizeof why)) {
            host_report(HOST_ERROR, "save directory . (the working directory: [files] save_dir = auto): %s", why);
            return 1;
        }
        opt->save_dir = ".";
        xeen_wox_working_directory(chosen, sizeof chosen);
        host_report(HOST_INFO, "note: saving into %s (the working directory: [files] save_dir = auto; --save-dir "
                               "overrides)", chosen);
    }
    return 0;
}
/**
 * Prepares the resources every session of the run shares, after load_inputs: the save directory and
 * its DOS clock, the save-file callbacks, the RNG's program-start state, the presentation and the
 * three 64000-byte planes, and the two world archives (from the CC files). Returns 0 when an
 * allocation fails or an archive is rejected (main exits with status 1; free_host_resources
 * releases what was allocated).
 */
static int prepare_host_resources(host_resources *res, const explore_options *opt)
{
    random_source *random = &res->random;
    int i;
    res->directory.dir = opt->dir;
    res->directory.save_dir = opt->save_dir;
    /* Phase 287: the game writes XEEN.CFG where it is read (a window's or an explicit xeen_cfg_dir); a headless
     * developer run without an explicit one writes it into its save directory, as before (never the data directory). */
    res->directory.cfg_dir = !opt->headless || opt->xeen_cfg_explicit ? opt->xeen_cfg_dir : NULL;
    res->directory.deterministic = opt->headless || opt->explicit_rng_time;
    res->directory.trace = opt->headless && opt->evidence != NULL; /* file-op lines */
    res->directory.clock = (xeen_dos_clock){
            (uint16_t)opt->dos_date[0],   (uint8_t)opt->dos_date[1],  (uint8_t)opt->dos_date[2],
            random->replay_time.hour,     random->replay_time.minute, random->replay_time.second,
            random->replay_time.hundredth};
    res->save_files = host_save_services(&res->directory);
    random->deterministic = opt->headless || opt->explicit_rng_time;
    /* The driver RNG 50525's state at program start: segment 4853 CS:16F8,
     * 16FA, 16FC, initialized executable bytes (compiled in). */
    for (i = 0; i < 3; i++) {
        random->state.words[i] = xeen_program_rng_seed[i];
    }
    res->p = calloc(1, sizeof *res->p);
    res->cursor = malloc(XEEN_PRESENT_PIXELS);
    res->opacity = malloc(XEEN_PRESENT_PIXELS);
    res->background = calloc(1, XEEN_PRESENT_PIXELS);
    if (!res->p || !res->cursor || !res->opacity || !res->background) {
        return 0;
    }
    for (i = 0; i < 2; i++) {
        if (xeen_archive_open(&res->archives[i], res->files[HOST_XEEN_CC + i], res->sizes[HOST_XEEN_CC + i]) != XR_OK) {
            return 0;
        }
    }
    return 1;
}
/* The music of the run (xeen_music_resolve over wox.cfg and the XEEN.CFG image CFG): WOX's music system; the PC
 * speaker voice's block clock (Phase 287: the original music driver ibmsnd's block chains to, only when XEEN.CFG
 * selects an unsupported one) and its log (headless runs); music_override = auto reports its resolution ("audio: ..."), a
 * warning when unsupported or without XEEN.CFG. */
static void resolve_host_music(const explore_options *opt, xeen_wox_config *wox, const uint8_t *cfg, size_t cfg_size)
{
    xeen_music_resolution r;
    xeen_music_resolve(wox, cfg, cfg_size, &r, music_resolution, sizeof music_resolution);
    wox->system = r.system;
    voice_block_clock_stands = r.from_xeen_cfg && r.unsupported;
    voice_block_log = opt->headless;
    if (wox->music_auto) {
        host_report(r.unsupported || !r.xeen_cfg_present ? HOST_WARN : HOST_INFO, "audio: %s", music_resolution);
    }
}
/* The voice model of the run, from XEEN.CFG's sound driver (8D38, 47C1F's index 1..10) and [audio] speech
 * (xeen_speech_resolve; Phase 286): off and nullsnd model no voice; on over nullsnd or the PC speaker models the SB
 * driver (prosnd's trim); auto keeps the driver's model and plays no PC-speaker voice. blastsnd's length trim is 0Eh,
 * prosnd's and specsnd's 0Ah (the others: 0Ah, labelled). A start without XEEN.CFG (CFG NULL) keeps the SB model (as
 * before Phase 286) unless speech is off. An explicit speech setting, or a driver that leaves the voice inaudible,
 * reports the resolution. */
static void resolve_host_speech(const xeen_wox_config *wox, const uint8_t *cfg, size_t cfg_size)
{
    xeen_speech_resolve(wox, cfg, cfg_size, &speech, speech_resolution, sizeof speech_resolution);
    /* Phase 286 (Phase 285A's Q1): adsnd and source by their own timing, covxsnd the SB model (labelled); Phase 287 (owner
     * option (a)): ibmsnd by its blocking start, silent (voice_block_hold), never busy after it. No XEEN.CFG (a saved
     * start, or none in the directories): the SB model, as before; speech = off still none. */
    xeen_speech_voice_model(&speech, cfg != NULL, &voice_kind, &voice_trim);
    if (wox->speech != XEEN_SPEECH_AUTO || (cfg && !speech.audible)) {
        host_report(HOST_INFO, "audio: %s", speech_resolution);
    }
}
/* Opens the run's audio with the XEEN.CFG image CFG: INTRO.CC's archive for the drivers (an audio build); a window's
 * audio, deferred until the window shows (Phase 290: defer_window_audio, with a copy of CFG; no copy: opened now) or
 * opened now; then the capture audio (a headless --audio-capture run). Returns 0 when the host can go on, else 1. */
static int open_host_audio(const explore_options *opt, xeen_wox_config *wox, host_resources *res,
                           const uint8_t *cfg, size_t cfg_size)
{
#if defined(XEEN_HAVE_AUDIO)
    if (res->intro && xeen_archive_open(&audio_intro_archive, res->intro, res->intro_size) == XR_OK) {
        audio_intro = &audio_intro_archive;
    }
#endif
    if (!opt->headless && defer_window_audio(opt, wox)) {
        audio_deferred = 1;
        if (cfg && cfg_size && (audio_deferred_cfg = malloc(cfg_size)) != NULL) {
            memcpy(audio_deferred_cfg, cfg, cfg_size);
            audio_deferred_cfg_size = cfg_size;
        } else if (cfg && cfg_size) {
            audio_deferred = 0; /* no copy: the open stays here (the boot as before Phase 290) */
        }
    }
    if (!opt->headless && !audio_deferred) {
        open_window_audio(opt, wox, res->archives, cfg, cfg_size);
    }
    return open_capture_audio(opt, wox, res->archives, cfg, cfg_size) ? 0 : 1;
}
/**
 * Configures the host before the platform opens, after resolve_startup_config read WOX, in order: the voice and CD
 * status models of the environment; the XEEN.CFG image the audio reads (Phase 286/287: a saved start, which has no
 * configuration image of its own, reads XEEN.CFG here when music_override = auto or speech is set; read_xeen_cfg, from where
 * the game reads it: XEEN.DAT reads XEEN.CFG at start-up, 47C1F, and loads its music driver once, 15E10); the music
 * (resolve_host_music) and the voice model (resolve_host_speech); a headless run's CD status log on stdout; then the
 * audio (open_host_audio). Returns 0 when the host can go on, else main's exit status 1.
 */
static int configure_host(const explore_options *opt, xeen_wox_config *wox, host_resources *res)
{
    uint8_t *audio_cfg_own = NULL;
    const uint8_t *audio_cfg = res->config;
    size_t audio_cfg_size = res->config_size;
    int status;
    if (!status_modes_from_environment()) {
        return 1;
    }
    if (!audio_cfg && (wox->music_auto || wox->speech != XEEN_SPEECH_AUTO)) {
        char where[1200];
        audio_cfg = audio_cfg_own = read_xeen_cfg(opt, &audio_cfg_size, where, sizeof where); /* Phase 287 */
    }
    resolve_host_music(opt, wox, audio_cfg, audio_cfg_size);
    resolve_host_speech(wox, audio_cfg, audio_cfg_size);
    if (opt->headless) {
        cd_status_log = stdout;
    }
    status = open_host_audio(opt, wox, res, audio_cfg, audio_cfg_size);
    free(audio_cfg_own);
    return status;
}
/**
 * Connects the host's globals to PLATFORM just after it opened (PLATFORM may be NULL: main reports
 * the failure afterwards; the switches below are read either way). In order: the clock service's
 * platform, map_movement's button state, wox.cfg's fullscreen, the window's display, the headless
 * observation switches of the environment (XEEN_REFRESH_CAPTURE, XEEN_HOST_PAGE_CAPTURE,
 * XEEN_TEST_SKIP_INVALID_KEYS, XEEN_MODAL_TYPEAHEAD, the XEEN_HOST_TIMING file, XEEN_DISPLAY_CAPTURE,
 * XEEN_TEST_POINTER_PATH),
 * then the pointer: a window has the mouse; a headless --clicks run gets it at the image centre
 * before the first poll.
 */
static void bind_host_platform(const explore_options *opt, xeen_platform *platform,
                               const xeen_wox_config *wox)
{
    clock_platform = platform;
    if (platform) {
        host_addons.button_held = addon_button_held;
        host_addons.button_user = platform;
    } /* map_movement's drag */
    if (platform && !opt->headless && wox->fullscreen) {
        xeen_platform_set_fullscreen(platform, 1);
    }
    if (platform && !opt->headless) {
        display_host.platform = platform; /* The window's display */
    }
    if (opt->headless) { /* the headless observation switches (reads only) */
        host_refresh_capture_set(getenv("XEEN_REFRESH_CAPTURE"));
        host_shown_capture_set(getenv("XEEN_HOST_PAGE_CAPTURE")); /* Phase 329 */
        skip_invalid = getenv("XEEN_TEST_SKIP_INVALID_KEYS") != NULL;
        modal_typeahead = getenv("XEEN_MODAL_TYPEAHEAD") != NULL;
        display_host.capture = getenv("XEEN_DISPLAY_CAPTURE");
    }
    {
        const char *t = getenv("XEEN_HOST_TIMING");
        if (t && *t) {
            timing.f = fopen(t, "w");
        }
    } /* Developer instrumentation */
    if (opt->headless && click_count) {
        display_host.pointer_path = getenv("XEEN_TEST_POINTER_PATH");
        display_host.scale = opt->scale;
    }
    if (platform && (!opt->headless || click_count)) {
        pointer_platform = platform;
        host_cursor = !opt->headless && !authentic_cursor;
        if (opt->headless) {
            (void)xeen_platform_mouse_event(platform, (int)(XEEN_PRESENT_WIDTH / 2u * opt->scale),
                                            (int)(XEEN_PRESENT_HEIGHT / 2u * opt->scale), 0, 0); /* the centre */
        }
    }
}
/**
 * Binds G (or NULL, to unbind) to the add-ons that keep the game between passes: monster_difficulty,
 * spell_short_description and character_details, each only when it is on.
 */
static void bind_game_addons(xeen_game *g)
{
    if (xeen_monster_difficulty_on) {
        xeen_monster_difficulty_bind(g); /* add-on */
    }
    if (xeen_spell_short_description_on) {
        xeen_spell_short_description_bind(g); /* add-on */
    }
    if (xeen_character_details_on) {
        xeen_character_details_bind(g); /* add-on */
    }
}
/**
 * Shows the displayed page P on PLATFORM with the cursor state of the driver's mouse flag: after
 * every new scene return (v->scene_returns; headless runs too), and in a window whenever the page
 * changes (menus, dialogs, animations; xeen_platform_refresh throttles it; display only). Returns 0
 * when the platform fails.
 */
static int present_host_frame(explore_replay *replay, const explore_options *opt,
                              xeen_platform *platform, xeen_presentation *p,
                              const xeen_game_view *v)
{
    if (v->scene_returns != replay->previous_scenes) {
        replay->previous_scenes = v->scene_returns;
        xeen_platform_show_cursor(platform, p->enabled);
        return xeen_platform_present(
                platform, HOST_PAGE(p->pages[p->displayed_page], xeen_presentation_dac(p)),
                xeen_presentation_dac(p));
    }
    if (!opt->headless) {
        xeen_platform_show_cursor(platform, p->enabled);
        return xeen_platform_refresh(
                platform, HOST_PAGE(p->pages[p->displayed_page], xeen_presentation_dac(p)),
                xeen_presentation_dac(p));
    }
    return 1;
}
/**
 * Headless only: prints the CD voice requests the silent backend recorded and, when there were any,
 * the CD-side requests of the speech toggle's close (47F7F); routes without them keep their logs.
 */
static void print_cd_counters(const explore_options *opt, xeen_game *g)
{
    if (opt->headless && xeen_game_audio(g)) {
        const xeen_native_audio *a = xeen_game_audio(g);
        printf("cd-voice starts=%lu stops=%lu track=%u from=%u to=%u\n", a->cd_starts, a->cd_stops,
               a->cd_track, a->cd_from, a->cd_to);
    }
    {
        uint8_t w = 0;
        unsigned long n = xeen_game_cd_side_requests(g, &w);
        if (opt->headless && n) {
            printf("cd-side requests=%lu world=%u\n", n, (unsigned)w);
        }
    }
}
/**
 * Restarts the replay's per-session marks after a Load: the new session counts its polls,
 * presentations, ignored yes/no keys and saves from 0, so the feeders' marks restart with it (the
 * saves already logged move into saves_before). Before, a session whose first modal had the same
 * count as the last modal before the Load (e.g. a treasure, then Save/Load, then Rest's "Rest
 * complete" scroll: both presentation 1) was taken as already fed and waited for a key forever
 * (headless replays only).
 */
static void restart_replay_marks(explore_replay *replay)
{
    ++replay->loads;
    replay->ready = 0;
    replay->idle_done = 0;
    replay->pending = 0;
    replay->menu_armed = 0;
    replay->menu_logged = 0;
    replay->menu_last_stage = 0;
    replay->modal_presentations = 0;
    replay->modal_queued = 0;
    replay->modal_visits = 0;
    replay->modal_ignored = 0;
    replay->logged_modal = 0;
    replay->modal_idle = 0;
    replay->saves_before += replay->logged_saves;
    replay->panel_last_session = 0;
    replay->panel_armed = 0;
    replay->logged_saves = 0;
}
/**
 * The load reached the 19E82 map load (v.session_ready): the session's DS and CUR images move to a
 * new native game started like a saved one. In order: the session-ready record and the sessionN
 * snapshots (DS, then the CUR images and slot files at the hand-off: a new game's created ones, a
 * load's copies), the hand-off itself, the session-load and CD lines, the modal-poll label base
 * (the labels continue across the Load), the add-ons unbound from the old game, the old game
 * destroyed and the new one created, bound and started, and the replay's marks restarted.
 *
 * *G is main's game (the Ctrl+Q query and the host add-ons hold its address); it is NULL after a
 * rejected new game. Returns 0 on any failure (main exits with status 1).
 */
static int reload_session(xeen_game **g, explore_replay *replay, const explore_options *opt,
                          host_resources *res)
{
    xeen_game_session *session = malloc(sizeof *session);
    char label[40];
    char error[256];
    host_print_record(*g, res->p, "session-ready", res->random.calls);
    snprintf(label, sizeof label, "session%u-ds", replay->loads + 1u);
    if (!session || !host_write_snapshot(opt->evidence, label, xeen_game_ds(*g), 65536)) {
        free(session);
        return 0;
    }
    snprintf(label, sizeof label, "session%u", replay->loads + 1u);
    if (!host_snapshot_saves(opt->evidence, label, *g, &res->directory, 1) ||
        !xeen_game_take_session(*g, session)) {
        free(session);
        return 0;
    }
    printf("session-load slot=%u count=%u\n", (unsigned)session->slot, replay->loads + 1u);
    print_cd_counters(opt, *g);
    replay->modal_poll_base += xeen_game_inspect(*g).modal_polls;
    replay->logged_modal_polls = 0;
    xeen_game_show_session_wait(*g, session); /* 19E82's box, before the new session's load */
    bind_game_addons(NULL); /* add-ons: never a freed game */
    xeen_game_destroy(*g);
    *g = xeen_game_create_loaded(session, res->files[HOST_XEEN_CC], res->sizes[HOST_XEEN_CC], res->files[HOST_DARK_CC],
                                 res->sizes[HOST_DARK_CC], error, sizeof error);
    xeen_game_session_free(session);
    free(session);
    if (!*g) {
        host_report(HOST_ERROR, "loaded session rejected: %s", error);
        return 0;
    }
    bind_game_addons(*g);
    xeen_archive_miss_clear();
    if (!start_session(*g, 0, res)) {
        report_cc_miss(0);
        return 0;
    }
    restart_replay_marks(replay);
    host_print_record(*g, res->p, "loaded", res->random.calls);
    return 1;
}
/** The main menu's key polls the replay feeds (feed_menu_command) for menu STAGE. */
static int menu_command_poll(int stage)
{
    return stage == XEEN_MENU_TITLE_POLL || stage == XEEN_MENU_POLL || stage == XEEN_MENU_DIALOG_POLL ||
           stage == XEEN_MENU_SCROLL_POLL ||
           (stage >= XEEN_MENU_CREDITS_POLL && stage <= XEEN_MENU_CREDITS_POLL + 3) /* the credits' four pages */
           || stage == XEEN_MENU_OPTIONS_POLL || stage == XEEN_MENU_PAIR_POLL;
}
/**
 * Arms the replay's next menu key: at a new menu stage (a frame, or a poll reached straight from
 * another poll, e.g. the 18F2F scroll), and at 184DE's 2937A (XEEN_MENU_PAIR_POLL) when it ignored a key (it
 * keeps polling) or presents the next slot's question.
 */
static void arm_menu_feed(explore_replay *replay, const xeen_game_view *v)
{
    if (v->menu_stage != replay->menu_last_stage) {
        replay->menu_last_stage = v->menu_stage;
        replay->menu_armed = 1;
    }
    if (v->menu_stage == XEEN_MENU_PAIR_POLL && v->confirm_ignored_keys != replay->menu_ignored) {
        replay->menu_ignored = v->confirm_ignored_keys;
        replay->menu_armed = 1;
    }
    if (v->menu_stage == XEEN_MENU_PAIR_POLL && v->presentations != replay->menu_presented) {
        replay->menu_presented = v->presentations;
        replay->menu_armed = 1;
    }
}
/**
 * Arms the replay's next control-panel key: one key per poll session, queued before its first 189E3
 * (the panel, the saved message, the slot dialog, a name key); after a '~' the next input arms at
 * the next loop poll of the same boundary.
 */
static void arm_panel_feed(explore_replay *replay, const xeen_game_view *v)
{
    if (replay->idle_pending && v->panel_polls != replay->idle_mark &&
        v->lifecycle_boundary == replay->idle_boundary) {
        replay->idle_pending = 0;
        replay->panel_armed = 1;
    }
    if (v->panel_sessions != replay->panel_last_session) {
        replay->panel_last_session = v->panel_sessions;
        replay->panel_armed = 1;
    }
}
/**
 * Logs the new location clock reads (loc-tick) and the cutscenes' new clock reads and 189E3 polls
 * with their return sites (cut-tick/cut-poll; the replay feeds the same values at the same sites).
 * A Load's new session restarts both counts at 0, which logs nothing.
 */
static void log_location_and_cutscene_reads(explore_replay *replay, const explore_options *opt,
                                            const xeen_game_view *v)
{
    if (v->loc_tick_reads != replay->logged_loc_ticks) {
        replay->logged_loc_ticks = v->loc_tick_reads;
        if (opt->headless && v->loc_tick_reads) {
            printf("loc-tick read=%lu ticks=%u\n", v->loc_tick_reads, (unsigned)v->loc_ticks);
        }
    }
    if (v->cut_events != replay->logged_cut_events) {
        if (opt->headless && v->cut_events > replay->logged_cut_events) {
            printf("cut-%s n=%lu ret=%05x value=%x\n", v->cut_is_poll ? "poll" : "tick",
                   v->cut_events, (unsigned)v->cut_ret, (unsigned)v->cut_value);
        }
        replay->logged_cut_events = v->cut_events;
    }
}
/**
 * --cutscene-keys N:K,...: at a cutscene's next poll, queues key K when the poll is the Nth (a
 * headless test control). Returns 0 after a malformed list or key (main exits with status 1).
 * REPLAY and G are named for host_key.
 */
static int feed_cutscene_keys(explore_replay *replay, const explore_options *opt, xeen_game *g,
                              xeen_platform *platform, const xeen_game_view *v)
{
    const char *c = opt->cut_keys;
    (void)replay;
    (void)g;
    while (*c) {
        unsigned long n = strtoul(c, (char **)&c, 10);
        xeen_native_key key;
        if (*c != ':' || !c[1]) {
            host_report(HOST_ERROR, "invalid --cutscene-keys");
            return 0;
        }
        if (n == v->cut_polls + 1u) {
            if (!panel_key(c[1], &key) || !host_key(platform, key)) {
                host_report(HOST_ERROR, "invalid cutscene key %c", c[1]);
                return 0;
            }
            printf("cut-key poll=%lu key=%c\n", n, c[1]);
        }
        c += 2;
        if (*c == ',') {
            ++c;
        }
    }
    return 1;
}
/**
 * Logs a new modal key poll: its entry DS (the original replay's poll states) as
 * modal-pollN-ds, numbered over the run, and the key it got. The poll a modal '~' waited for (no
 * key: its 4EA1C tick and the loop's own scene follow) is marked 'idle'; the harness closes that
 * input's poll group there, and the next input is queued at once (no further keyless poll before
 * it). Returns 0 when that poll got a key instead, or a snapshot fails.
 */
static int log_modal_poll(explore_replay *replay, const explore_options *opt, xeen_game *g,
                          const xeen_game_view *v)
{
    char label[48];
    replay->logged_modal_polls = v->modal_polls;
    snprintf(label, sizeof label, "modal-poll%lu-ds", replay->modal_poll_base + v->modal_polls);
    {
        int idle = replay->modal_idle && v->modal_polls > replay->modal_idle_mark &&
                   !v->modal_poll_key;
        if (opt->headless) {
            printf("modal-poll %lu inputs=%u key=%x%s\n", replay->modal_poll_base + v->modal_polls,
                   (unsigned)(replay->command_index - replay->markers), (unsigned)v->modal_poll_key,
                   idle ? " idle" : "");
        }
        if (replay->modal_idle && v->modal_polls > replay->modal_idle_mark) {
            if (!idle) {
                host_report(HOST_ERROR, "modal idle: poll %lu answered with key %x",
                        replay->modal_poll_base + v->modal_polls, (unsigned)v->modal_poll_key);
                return 0;
            }
            replay->modal_idle = 0;
            replay->modal_queued = 0;
            replay->modal_visits = MODAL_KEY_VISIT - 1u; /* the next visit feeds the key */
        }
    }
    if (xeen_game_modal_poll_ds(g) &&
        !host_write_snapshot(opt->evidence, label, xeen_game_modal_poll_ds(g), 65536)) {
        return 0;
    }
    return 1;
}
/**
 * Logs each new 30E05 pass's 4EA1C value (the replay's clock). A Load's new session restarts the
 * count at 0, which logs nothing.
 */
static void log_item_pass(explore_replay *replay, const explore_options *opt,
                          const xeen_game_view *v)
{
    if (v->item_passes != replay->logged_item_passes) {
        replay->logged_item_passes = v->item_passes;
        if (opt->headless && v->item_passes) {
            printf("item-wait pass=%lu ticks=%u\n", v->item_passes, (unsigned)v->item_ticks);
        }
    }
}
/**
 * The game saved (v->saves grew): the game-saved record and line, and the saveN snapshots of the
 * slot files (numbered over the run). Returns 0 when a snapshot fails.
 */
static int snapshot_new_save(explore_replay *replay, const explore_options *opt, xeen_game *g,
                             host_resources *res, const xeen_game_view *v)
{
    char label[40];
    replay->logged_saves = v->saves;
    host_print_record(g, res->p, "game-saved", res->random.calls);
    printf("game-saved slot=%u count=%lu\n", (unsigned)xeen_game_ds(g)[XEEN_DS_GAME_SLOT], v->saves);
    snprintf(label, sizeof label, "save%lu", v->saves + replay->saves_before);
    return host_snapshot_saves(opt->evidence, label, g, &res->directory, 0);
}
/**
 * The monsters' contact (boundary 1C17E): the contact-ready record, the RNG state line and the
 * contact-ds and contact-cN-ds snapshots (N the inputs fed so far). Returns 0 when a snapshot fails.
 */
static int snapshot_contact(explore_replay *replay, const explore_options *opt, xeen_game *g,
                            host_resources *res)
{
    char label[40];
    host_print_record(g, res->p, "contact-ready", res->random.calls);
    if (opt->headless) {
        printf("contact-rng-state=%u,%u,%u calls=%u\n", res->random.state.words[0],
               res->random.state.words[1], res->random.state.words[2], res->random.calls);
    }
    if (!host_write_snapshot(opt->evidence, "contact-ds", xeen_game_ds(g), 65536)) {
        return 0;
    }
    snprintf(label, sizeof label, "contact-c%u-ds",
             (unsigned)(replay->command_index - replay->markers));
    return host_write_snapshot(opt->evidence, label, xeen_game_ds(g), 65536);
}
/**
 * A fed combat command reached the unsupported continuation 38049 (the end of what the host
 * replays): the commandN-complete record, the combat RNG line, and the DS and frame snapshots.
 * Returns 0 when a snapshot fails.
 */
static int snapshot_completed_command(explore_replay *replay, const explore_options *opt,
                                      xeen_game *g, host_resources *res)
{
    char label[40];
    char frame_label[48];
    snprintf(label, sizeof label, "command%u-complete",
             (unsigned)(replay->command_index - replay->markers));
    host_print_record(g, res->p, label, res->random.calls);
    if (opt->headless) {
        printf("combat-rng-state=%u,%u,%u calls=%u\n", res->random.state.words[0],
               res->random.state.words[1], res->random.state.words[2], res->random.calls);
    }
    if (!host_write_snapshot(opt->evidence, label, xeen_game_ds(g), 65536)) {
        return 0;
    }
    snprintf(frame_label, sizeof frame_label, "command%u-complete-frame",
             (unsigned)(replay->command_index - replay->markers));
    return host_write_snapshot(opt->evidence, frame_label, res->background, XEEN_PRESENT_PIXELS);
}
/**
 * Re-arms the modal feeder once per completed event (its steps), not on every non-waiting pass: a
 * later scroll's scene passes (the Rest completion after a location's event) would reset its visits.
 */
static void rearm_modal_feed(explore_replay *replay, const xeen_game_view *v)
{
    if (v->event_status == XEE_DONE && v->steps != replay->modal_steps) {
        replay->modal_steps = v->steps;
        replay->modal_queued = 0;
        replay->modal_visits = 0;
        replay->logged_modal = 0;
    }
}
/**
 * After a headless pass that left a fed command at the exploration poll (17B09/17B12): the
 * commandN-dispatched DS snapshot. Returns 0 when it fails.
 */
static int snapshot_dispatched_command(explore_replay *replay, const explore_options *opt,
                                       xeen_game *g, const xeen_game_view *v)
{
    if (opt->headless && replay->pending &&
        (v->lifecycle_boundary == XEEN_LOOP_COMMAND_POLL || v->lifecycle_boundary == XEEN_LOOP_IDLE_WAIT)) {
        char label[40];
        snprintf(label, sizeof label, "command%u-dispatched",
                 (unsigned)(replay->command_index - replay->markers));
        return host_write_snapshot(opt->evidence, label, xeen_game_ds(g), 65536);
    }
    return 1;
}
/**
 * After a normal end: the host-shutdown record, the CD counters, and the final-ds, frame and
 * palette snapshots. Returns 0 when a snapshot fails (main's status becomes 1).
 */
static int write_final_evidence(const explore_options *opt, xeen_game *g, host_resources *res)
{
    host_print_record(g, res->p, "host-shutdown", res->random.calls);
    print_cd_counters(opt, g);
    return host_write_snapshot(opt->evidence, "final-ds", xeen_game_ds(g), 65536) &&
           host_write_snapshot(opt->evidence, "frame", res->background, XEEN_PRESENT_PIXELS) &&
           host_write_snapshot(opt->evidence, "palette", res->p->palette, DISPLAY_PALETTE_BYTES);
}
/** Frees the buffers host_resources owns (NULL ones included). */
static void free_host_resources(host_resources *res)
{
    int i;
    free(res->p);
    free(res->intro);
    free(res->background);
    free(res->cursor);
    free(res->opacity);
    free(res->config);
    for (i = 0; i < HOST_FILES; i++) {
        free(res->files[i]);
    }
}
/**
 * The exploration host (main). Its stages, in order:
 * 1. open_host: explore_parse_options (explore_options.c), resolve_startup_config, adopt_host_switches,
 *    adopt_movie_config and the option checks; configure_save_directory; load_inputs (the saves, the CC archives,
 *    XEEN.CFG, INTRO.CC); prepare_host_resources; configure_host (wox.cfg, the status models, the audio);
 *    report_addons;
 * 2. open_platform: the platform opens (bind_host_platform connects the host's globals to it), the deferred audio
 *    starts, the hooks are set and the add-ons start;
 * 3. run_program: the original's program flow (Phase 308): the XEEN.EXE movie when one is due, XEEN.DAT's program
 *    start (start_program, from the two saves or at the main menu), the session's run loop (run_session); after a
 *    normal end finish_session (the final evidence and the --save-output pair); after an XEEN.EXE hand-off,
 *    hand_off and the next movie;
 * 4. close_host: the fatal dialog of a window's error, then the teardown in a fixed order: the timing report, the
 *    add-ons, the audio (before the archives' bytes are freed), the game, the platform, the buffers, the log.
 *
 * Each pass of the run loop (host_pass), over the game's view inspected once at its start:
 * 1. open_pass: pumps the platform and binds the add-ons; a closed window ends the run;
 * 2. presents the displayed page (present_host_frame) and dumps the refresh capture (headless, with
 *    XEEN_HOST_PAGE_CAPTURE, also the page a window's refresh would show after the add-ons' overlays);
 * 3. a session handed off by a Load restarts as a loaded game (reload_session);
 * 4. feed_poll_input: the replay's next input at the poll that owns it before the evidence (the exploration
 *    command poll, the main menu's entry and polls, the control panel);
 * 5. record_pass: the pass's evidence, in a fixed order (the stdout order is evidence), the cutscene keys among it;
 * 6. feed_combat_pass and feed_modal_pass: combat's and the modal waits' input;
 * 7. step_pass: one host step (run_host_pass) and, in a window, a 1 ms wait unless the game is computing.
 * Every feeder returns a host_flow; one that ends_pass ends the pass with it.
 *
 * Exit status:
 * - 0: the session ended normally (the end of the script, or the window or host closed);
 * - 1: an error;
 * - 2: wox.cfg did not load, or the driver's fatal exit;
 * - the program's own status, for its exit;
 * - 3: the XEEN.EXE hand-off.
 */
/* The pass's opening: the platform's events, the add-ons bound to the game. A closed window (or the main menu's
 * Quit) ends the session normally: HOST_BREAK with status 0; otherwise HOST_NEXT. */
static host_flow open_pass(host_pass_state *s)
{
    xeen_platform_pump(s->platform);
    if (xeen_damage_numbers_on) {
        xeen_damage_numbers_bind(s->g); /* add-on */
    }
    bind_game_addons(s->g);
    if (!xeen_platform_should_close(s->platform)) {
        return HOST_NEXT;
    }
    if (xeen_platform_menu_quits(s->platform)) {
        host_print_record(s->g, s->res->p, "menu-quit", s->res->random.calls);
        if (s->opt->headless) {
            printf("host-close menu-quit menu-stage=%d\n", s->v.menu_stage);
        }
    }
    *s->result = 0;
    return HOST_BREAK;
}
/* The replay's input at the polls before the pass's evidence: the exploration command poll, the main menu's entry
 * (logged once), the menu's command polls once armed and the control panel's dialog poll once armed. A held panel
 * key (HOST_PANEL_WAIT) goes on with the pass. */
static host_flow feed_poll_input(host_pass_state *s)
{
    explore_replay *replay = s->replay;
    const explore_options *opt = s->opt;
    const xeen_game_view *v = &s->v;
    host_flow flow;
    if (v->lifecycle_boundary == XEEN_LOOP_COMMAND_POLL &&
        ends_pass(flow = feed_exploration_poll(s))) {
        return flow;
    }
    if (xeen_game_site_kind(v->lifecycle_boundary) == XEEN_SITE_MENU_ENTRY && !v->menu_stage && !replay->menu_logged &&
        ends_pass(flow = log_main_menu_entry(s))) {
        return flow;
    }
    arm_menu_feed(replay, v);
    if (opt->headless && opt->commands && replay->menu_armed && menu_command_poll(v->menu_stage) &&
        ends_pass(flow = feed_menu_command(s))) {
        return flow;
    }
    arm_panel_feed(replay, v);
    if (v->panel_stage && xeen_game_site_kind(v->lifecycle_boundary) == XEEN_SITE_DIALOG_POLL &&
        replay->panel_armed &&
        ends_pass(flow = feed_panel_command(s))) {
        return flow;
    }
    return HOST_NEXT;
}
/* The pass's evidence in its fixed order: the location and cutscene reads, the cutscene keys at their poll (fed
 * here, between the reads and the modal log), the panel's log mark, the modal poll, the items, a new save and the
 * combat contact. Returns 0 on a failed write or key (the run ends without the final evidence). */
static int record_pass(host_pass_state *s)
{
    explore_replay *replay = s->replay;
    const explore_options *opt = s->opt;
    const xeen_game_view *v = &s->v;
    log_location_and_cutscene_reads(replay, opt, v);
    if (opt->headless && opt->cut_keys && v->cut_kind && v->cut_poll_next &&
        !feed_cutscene_keys(replay, opt, s->g, s->platform, v)) {
        return 0;
    }
    if (!v->panel_stage) {
        replay->panel_logged = 0;
    }
    if (v->modal_polls != replay->logged_modal_polls && !log_modal_poll(replay, opt, s->g, v)) {
        return 0;
    }
    log_item_pass(replay, opt, v);
    if (v->saves != replay->logged_saves && !snapshot_new_save(replay, opt, s->g, s->res, v)) {
        return 0;
    }
    return xeen_game_site_kind(v->lifecycle_boundary) != XEEN_SITE_COMBAT_CONTACT ||
           snapshot_contact(replay, opt, s->g, s->res);
}
/* Combat in the pass: the replay's command at the command poll, a window's combat-state record when the phase
 * changes (combat's start and end), and an unsupported monster-turn continuation, which ends the session normally
 * (status 0; logged, Phase 272) after the pending command's snapshot. */
static host_flow feed_combat_pass(host_pass_state *s)
{
    explore_replay *replay = s->replay;
    const explore_options *opt = s->opt;
    host_resources *res = s->res;
    const xeen_game_view *v = &s->v;
    host_flow flow;
    if (v->lifecycle_boundary == XEEN_COMBAT_COMMAND_POLL && v->combat_phase == XC_AWAITING_COMMAND &&
        ends_pass(flow = feed_combat_command(s))) {
        return flow;
    }
    if (!opt->headless && (int)v->combat_phase != replay->logged_phase) {
        replay->logged_phase = (int)v->combat_phase;
        host_print_record(s->g, res->p, "combat-state", res->random.calls);
    }
    if (xeen_game_site_kind(v->lifecycle_boundary) != XEEN_SITE_COMBAT_MONSTER_TURN ||
        v->combat_phase != XC_UNSUPPORTED_CONTINUATION) {
        return HOST_NEXT;
    }
    if (replay->pending && !snapshot_completed_command(replay, opt, s->g, res)) {
        return HOST_DONE;
    }
    host_note(HOST_WARN, "combat stopped at an unsupported monster-turn continuation (%05x); the session ends",
              (unsigned)v->lifecycle_boundary);
    *s->result = 0;
    return HOST_BREAK;
}
/* A modal wait's input (the save-pair question is the menu feeder's poll); outside one the modal feed rearms. A
 * held modal key (HOST_MODAL_WAIT) goes on with the step. */
static host_flow feed_modal_pass(host_pass_state *s)
{
    const xeen_game_view *v = &s->v;
    if (v->phase == XG_WAITING && v->menu_stage != XEEN_MENU_PAIR_POLL) {
        return feed_modal_wait(s);
    }
    rearm_modal_feed(s->replay, v);
    return HOST_NEXT;
}
/* The pass's step: the headless clock (two ticks), the timing report's pass mark, one host step (run_host_pass) and
 * the dispatched command's snapshot; a window then waits 1 ms unless the game is computing. HOST_CONTINUE, or
 * HOST_DONE on an error. */
static host_flow step_pass(host_pass_state *s)
{
    const explore_options *opt = s->opt;
    int compute;
    if (opt->headless && xeen_platform_advance(s->platform, 2) != XEEN_OK) {
        return HOST_DONE;
    }
    compute = !opt->headless && host_compute(s->g);
    if (timing.f) {
        timing.pass_start = xeen_platform_now_ns();
        timing.hold_ns = 0;
    }
    if (run_host_pass(s) == HOST_DONE ||
        !snapshot_dispatched_command(s->replay, opt, s->g, &s->v)) {
        return HOST_DONE;
    }
    if (!opt->headless && !compute) {
        xeen_platform_wait(s->platform, 1);
    }
    return HOST_CONTINUE;
}
/**
 * One pass of the host's run loop (described above open_pass). Returns HOST_CONTINUE for the next pass, HOST_BREAK
 * when the session ended normally (the final evidence and the save follow) and HOST_DONE on an error or a stop
 * already recorded (straight to the teardown). *GP is the run's game pointer (the platform's quit query and the
 * add-ons hold its address): a session reload replaces the game behind it.
 */
static host_flow host_pass(xeen_game **gp, explore_replay *replay, const explore_options *opt,
                           xeen_platform *platform, host_resources *res, int *result)
{
    host_pass_state s = {replay, opt, *gp, platform, res, xeen_game_inspect(*gp), result};
    host_flow flow;
    if (ends_pass(flow = open_pass(&s))) {
        return flow;
    }
    if (!present_host_frame(replay, opt, platform, res->p, &s.v)) {
        return HOST_DONE;
    }
    /* XEEN_REFRESH_CAPTURE=DIR writes the displayed page and DAC whenever they change at this point of the loop, the
     * frames a window's refresh would show (refresh-NNNN-B.bin/.pal, B the boundary; at most 2000). Observation
     * only. */
    if (opt->headless) {
        host_refresh_dump(res->p, s.v.lifecycle_boundary);
    }
    /* XEEN_HOST_PAGE_CAPTURE (Phase 329): the page a window's refresh shows here, after the add-ons' overlays
     * (present_host_frame refreshes it in a window only). Only while the capture is on. */
    if (opt->headless && host_shown_capture_on()) {
        (void)HOST_PAGE(res->p->pages[res->p->displayed_page], xeen_presentation_dac(res->p));
    }
    if (s.v.session_ready) {
        return reload_session(gp, replay, opt, res) ? HOST_CONTINUE : HOST_DONE;
    }
    if (ends_pass(flow = feed_poll_input(&s))) {
        return flow;
    }
    if (!record_pass(&s)) {
        return HOST_DONE;
    }
    if (ends_pass(flow = feed_combat_pass(&s)) || ends_pass(flow = feed_modal_pass(&s))) {
        return flow;
    }
    return step_pass(&s);
}

/*
 * Phase 308: one XEEN.EXE movie in the game's window (host_movie.c), COMMAND the XEENCOMMAND value (NULL: absent,
 * the program's start). ORDINAL counts the movies of the run (its --movie-keys entry). The cursor is hidden while it
 * plays and the game's Ctrl+Q query restored after it. A headless run prints a "movie" record.
 */
#if defined(XEEN_HAVE_AUDIO)
static int movie_music_fading(void *u)
{
    (void)u;
    return audio_engine ? xeen_audio_engine_music_fading(audio_engine) : 0;
}
#endif
static void queue_movie_keys(const explore_options *opt, xeen_platform *platform, unsigned ordinal)
{
    const char *k = opt->movie_keys;
    for (unsigned i = 0; k && i < ordinal; ++i) {
        k = strchr(k, ',');
        k = k ? k + 1 : NULL;
    }
    for (; k && *k && *k != ','; ++k) {
        xeen_native_key key = *k == 'X' ? XKEY_GAME_ESCAPE : *k == 'Q' ? (xeen_native_key)(XKEY_Q | XKEY_CTRL) : XKEY_SPACE;
        (void)(xeen_platform_queue_key)(platform, key); /* not a replay command key (the fixtures' macro) */
    }
}
static xeen_movie_outcome play_movie(const explore_options *opt, const xeen_wox_config *wox, host_resources *res,
                                     xeen_platform *platform, xeen_game **g, const char *command, unsigned ordinal)
{
    host_movie_context c;
    xeen_movie_report r;
    xeen_movie_outcome o;
    memset(&c, 0, sizeof c);
    c.platform = platform;
    c.provider = res->provider;
    c.intro = res->intro;
    c.intro_size = res->intro_size;
    c.xeen = res->files[HOST_XEEN_CC];
    c.xeen_size = res->sizes[HOST_XEEN_CC];
    c.dark = res->files[HOST_DARK_CC];
    c.dark_size = res->sizes[HOST_DARK_CC];
#if defined(XEEN_HAVE_AUDIO)
    c.audio = session_sink();
    c.music_fading = movie_music_fading;
#endif
    c.wox = wox;
    c.xeen_cfg = res->config;
    c.xeen_cfg_size = res->config_size;
    c.save_files = &res->save_files;
    c.data_dir = opt->dir;
    c.cue = wox->movie.cue[0] ? wox->movie.cue : NULL;
    c.cd_last_track = wox->movie.cd_num_tracks;
    c.headless = opt->headless;
    c.developer = &opt->movie;
    if (opt->headless) {
        queue_movie_keys(opt, platform, ordinal);
    }
    xeen_platform_show_cursor(platform, 0);
    o = host_movie_play(&c, command, ordinal == 0u, &r);
    xeen_platform_set_menu_quit_query(platform, menu_context_active, g); /* the game's Ctrl+Q again */
    if (opt->headless) {
        printf("movie ordinal=%u command=%s sequence=%s outcome=%s presents=%llu short-intro=%d cfg-written=%d\n",
               ordinal, command ? command : "(absent)", r.sequence ? r.sequence : "-", xeen_movie_outcome_name(o),
               (unsigned long long)r.presents, r.short_intro, r.cfg_written);
    }
    return o;
}
/* Phase 308: the developer movie controls wox.cfg names (labelled, not original) where the command line left them
 * unset. */
static void adopt_movie_config(explore_options *opt, const xeen_wox_config *wox)
{
    host_movie_developer *m = &opt->movie;
    if (!m->command && !m->sequence) {
        m->command = wox->movie.command[0] ? wox->movie.command : NULL;
        m->sequence = wox->movie.sequence[0] ? wox->movie.sequence : NULL;
    }
    if (!m->dump && wox->movie.dump_kind) {
        m->dump = wox->movie.dump;
        m->timing_only = wox->movie.dump_kind == 3;
    }
    if (!m->dos_date && wox->movie.dos_date[0]) {
        m->dos_date = wox->movie.dos_date;
    }
    if (!m->dos_time && wox->movie.dos_time[0]) {
        m->dos_time = wox->movie.dos_time;
    }
    m->original_timer_quirks |= wox->movie.original_timer_quirks;
    m->legacy_timing |= wox->movie.legacy_timing;
    if (!m->score_given && !m->payload) {
        m->score_given = wox->movie.score_given;
        m->score = wox->movie.score;
        m->payload = wox->movie.payload[0] ? wox->movie.payload : NULL;
    }
    if (!m->badge_given) {
        m->badge_given = wox->movie.badge_given;
        m->badge = wox->movie.badge;
    }
}
/* Phase 272: the fatal sequence's services (host_fatal_present): the audio paused as the boss screen pauses it,
 * and the platform's modal error over the window's last frame. */
static void fatal_pause_audio(void *u)
{
    (void)u;
#if defined(XEEN_HAVE_AUDIO)
    if (audio_engine) {
        xeen_audio_engine_pause(audio_engine, 1);
    }
#endif
}
static int fatal_dialog(void *u, const char *title, const char *text)
{
    return xeen_platform_fatal_dialog(u, title, text);
}
/* A window session: no --headless anywhere on the command line (decided before the options are parsed, so that a
 * rejected command line is logged too). */
static int window_session(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless")) {
            return 0;
        }
    }
    return 1;
}
/* The run's state, owned by main: the options and wox.cfg, the resources, the platform, the session's game (the
 * platform's Ctrl+Q query and the add-ons hold G's address) and the replay. RESULT is the exit status (1 until a
 * stage sets it). */
typedef struct {
    explore_options opt;
    xeen_wox_config wox;
    host_resources res;
    xeen_platform *platform;
    xeen_game *g;
    explore_replay replay;
    int window; /* a window session (window_session) */
    int result;
} host_run;
/* The replay state at a session's start: nothing logged yet (the ready mark set, no combat poll logged). */
static explore_replay fresh_replay(void)
{
    return (explore_replay){.ready_marked = 1, .logged_combat = (unsigned long)-1};
}
/**
 * The host's start before the platform: the command line, wox.cfg and the host switches, the option checks, the
 * save directory, the inputs (the saves, the CC archives, XEEN.CFG, INTRO.CC) and the host's resources, its
 * configuration and the add-ons' report. Returns 0 with RUN->result set (an error, or 0 after --help) when the run
 * ends here.
 */
static int open_host(host_run *run, int argc, char **argv)
{
    explore_options *opt = &run->opt;
    host_resources *res = &run->res;
    int status;
    if (!explore_parse_options(argc, argv, opt, &host_addons)) {
        run->result = opt->help ? 0 : 1;
        return 0;
    }
    if ((status = resolve_startup_config(opt, argv[0], &run->wox))) {
        run->result = status < 0 ? 0 : status; /* -1: the player declined to create wox.cfg (a clean end) */
        return 0;
    }
    adopt_host_switches(opt);
    adopt_movie_config(opt, &run->wox);
    host_set_diagnostic_state(opt->diagnostic_state);
    res->random.replay_time = opt->rng_time;
    if ((opt->commands || frontend_input) && !opt->headless) {
        host_report(HOST_ERROR, "command replay requires explicit --headless");
        return 0;
    }
    /* In a window, --rng-time fixes the DOS clock the RNG and the saves read (host test control, like setting the
     * PC's clock); the window still runs on the wall clock. */
    if (opt->explicit_rng_time && !opt->headless) {
        host_report(HOST_INFO, "note: fixed DOS clock (host test control)");
    }
    if (configure_save_directory(opt, &run->wox)) {
        return 0;
    }
    if (!load_inputs(opt, res->files, res->sizes, &res->config, &res->config_size, &res->intro, &res->intro_size) ||
        !prepare_host_resources(res, opt)) {
        return 0;
    }
    if ((status = configure_host(opt, &run->wox, res))) {
        run->result = status;
        return 0;
    }
    report_addons();
    dn_headless = opt->headless;
    return 1;
}
/**
 * The platform (a window, or headless), its deferred audio, its hooks (the boss screen's audio, the Ctrl+Q query
 * over RUN->g) and the add-ons. Returns 0 when the platform did not open (an error) or when the window was closed
 * while the SC-55 initialized (Phase 290: a clean end with status 0, nothing started).
 */
static int open_platform(host_run *run)
{
    explore_options *opt = &run->opt;
    host_resources *res = &run->res;
    char error[256];
    run->platform = xeen_platform_open("WoX", opt->scale, opt->headless, error, sizeof error);
    bind_host_platform(opt, run->platform, &run->wox);
    if (!run->platform) {
        host_report(HOST_ERROR, "%s", error);
        return 0;
    }
    xeen_platform_set_aspect(run->platform, opt->aspect);
    xeen_platform_set_locked_scale(run->platform, opt->locked_scale);
    if (xeen_platform_scale(run->platform) < opt->scale) { /* Phase 335: shrunk to fit the screen */
        host_report(HOST_INFO, "note: window scale %u does not fit the screen; using %u", opt->scale,
                    xeen_platform_scale(run->platform));
    }
    if (!start_deferred_audio(opt, &run->wox, res, &run->platform)) {
        clock_platform = NULL;
        display_host.platform = NULL;
        pointer_platform = NULL;
        host_addons.button_user = NULL;
        host_clean_end = 1;
        run->result = 0;
        return 0;
    }
    xeen_platform_set_boss_hook(run->platform, boss_audio, NULL);
    xeen_platform_set_menu_quit_query(run->platform, menu_context_active, &run->g); /* Ctrl+Q */
    res->provider = xeen_platform_input(run->platform);
    start_host_addons(run->platform, &res->provider, &run->g);
    return 1;
}
/**
 * XEEN.DAT's program start (15CDB): the game created at the main menu (with the save directory's clock) or from the
 * two saves, its session started, the add-ons bound and the start's record printed. Returns 0 after the
 * diagnostic when the start is rejected.
 */
static int start_program(host_run *run)
{
    const explore_options *opt = &run->opt;
    host_resources *res = &run->res;
    char error[256];
    xeen_archive_miss_clear(); /* the session's creation and start (report_cc_miss) */
    if (opt->menu_start) {
        xeen_dos_clock now;
        run->g = xeen_game_create_menu(res->files[HOST_XEEN_CC], res->sizes[HOST_XEEN_CC], res->files[HOST_DARK_CC],
                                       res->sizes[HOST_DARK_CC], res->config, res->config_size,
                                       host_save_clock(&res->directory, &now) ? &now : NULL, error, sizeof error);
    } else {
        run->g = xeen_game_create_saved(res->files[HOST_CLOUDS_SAVE], res->sizes[HOST_CLOUDS_SAVE],
                                        res->files[HOST_DARK_SAVE], res->sizes[HOST_DARK_SAVE],
                                        res->files[HOST_XEEN_CC], res->sizes[HOST_XEEN_CC], res->files[HOST_DARK_CC],
                                        res->sizes[HOST_DARK_CC], error, sizeof error);
    }
    if (!run->g) {
        host_report(HOST_ERROR, "%s startup rejected: %s", opt->menu_start ? "menu" : "saved", error);
        report_cc_miss(0);
        return 0;
    }
    if (!start_session(run->g, opt->menu_start, res)) {
        report_cc_miss(0);
        return 0;
    }
    bind_session_addons(run->g, &res->sprite_archives);
    host_print_record(run->g, res->p, opt->menu_start ? "program-start" : "restored", res->random.calls);
    return 1;
}
/* The session's run loop: host_pass until the session ends (HOST_BREAK: normally; HOST_DONE: an error, a stop
 * already recorded or an XEEN.EXE hand-off), which it returns. */
static host_flow run_session(host_run *run)
{
    host_flow pass;
    do {
        pass = host_pass(&run->g, &run->replay, &run->opt, run->platform, &run->res, &run->result);
    } while (pass != HOST_BREAK && pass != HOST_DONE);
    return pass;
}
/* After a normal end: the final evidence, then the --save-output pair (only while the status is still 0). */
static void finish_session(host_run *run)
{
    if (!write_final_evidence(&run->opt, run->g, &run->res)) {
        run->result = 1;
    }
    if (run->result == 0 && run->opt.save_output &&
        !commit_save_output(run->opt.save_output, run->g, run->res.files, run->res.sizes)) {
        run->result = 1;
    }
}
/**
 * Phase 308: XEEN.DAT's exec of xeen.exe (123A1). Copies the hand-off's XEENCOMMAND value into COMMAND (N bytes),
 * destroys the game (XEEN.DAT's image is replaced; nothing of it survives), makes the next start the main menu's
 * (15CDB; a restored developer start included), restarts the session's logging (the command script goes on where
 * it was) and reads XEEN.CFG again (15CDB's 47C1F; the game or the movie may have written it).
 */
static void hand_off(host_run *run, char *command, size_t n)
{
    const char *h = xeen_game_handoff(run->g), *eq = strchr(h, '=');
    explore_replay next = fresh_replay();
    char where[1100];
    snprintf(command, n, "%s", eq ? eq + 1 : h);
    xeen_game_destroy(run->g);
    run->g = NULL;
    run->opt.menu_start = 1;
    next.command_index = run->replay.command_index;
    next.markers = run->replay.markers;
    next.loads = run->replay.loads;
    run->replay = next;
    run->result = 0;
    host_clean_end = 0;
    free(run->res.config);
    run->res.config = read_xeen_cfg(&run->opt, &run->res.config_size, where, sizeof where);
}
/**
 * Phase 308: the original's program flow in one process. XEEN.EXE runs first (XEENCOMMAND absent: the Darkside
 * intro) when a window starts at the menu by default; every XEEN.EXE hand-off of XEEN.DAT (123A1) plays its movie
 * and XEEN.DAT then starts again at 15CDB (the original's overlay of XEEN.DAT, 14e09). A headless run does this only
 * with --movies; otherwise it stops at 123A1 as before. A movie that does not hand off (Ctrl+Q or the window closed:
 * XEEN.EXE's 1ec89 exit 1, a user's choice without an error dialog; a failure: XEEN.EXE's exit 1 after its
 * diagnostic) ends the run with status 1.
 */
static void run_program(host_run *run)
{
    explore_options *opt = &run->opt;
    const int movie_flow = opt->headless ? opt->movies : opt->menu_start;
    int movie_pending = movie_flow && opt->menu_start && (opt->headless || opt->menu_choice < 0);
    unsigned movie_ordinal = 0;
    const char *movie_command = NULL;
    char movie_command_text[160];
    for (;;) {
        if (movie_pending) {
            xeen_movie_outcome mo =
                    play_movie(opt, &run->wox, &run->res, run->platform, &run->g, movie_command, movie_ordinal++);
            if (!xeen_movie_hands_off(mo)) {
                host_clean_end = mo == XEEN_MOVIE_ABORTED;
                run->result = 1;
                return;
            }
        }
        if (!start_program(run)) {
            return;
        }
        if (run_session(run) == HOST_BREAK) {
            finish_session(run);
            return;
        }
        if (!movie_flow || !run->g || !xeen_game_handoff(run->g)) {
            return; /* an error or a recorded stop */
        }
        hand_off(run, movie_command_text, sizeof movie_command_text);
        movie_command = movie_command_text;
        movie_pending = 1;
    }
}
/**
 * The run's end. Phase 272: a window's fatal error first, while everything is as it failed (the window, its last
 * frame, the game, the audio paused): logged, then one modal error until its OK; the teardown only after it. A clean
 * end (status 0, the program's own exit, the hand-off, --help) shows none; headless never does. Then the teardown in
 * its fixed order. Returns the exit status.
 */
static int close_host(host_run *run)
{
    if (run->window && run->result != 0 && !host_clean_end) {
        host_fatal_hooks hooks = {fatal_pause_audio, fatal_dialog, run->platform};
        (void)host_fatal_present(&hooks, run->result);
    }
    report_timing(run->platform);
    stop_host_addons();
    close_audio();
#if defined(XEEN_HAVE_AUDIO)
    xeen_audio_sc55_boot_close(sc55_boot); /* Phase 290: none is left after start_deferred_audio (null is safe) */
    sc55_boot = NULL;
#endif
    free(audio_deferred_cfg); /* Phase 290: kept when the window did not open */
    audio_deferred_cfg = NULL;
    xeen_game_destroy(run->g);
    xeen_platform_close(run->platform);
    free_host_resources(&run->res);
    host_log_close(run->result);
    return run->result;
}
/* The host's entry: the run's options, host and program flow (described above open_pass); its exit status. */
int main(int argc, char **argv)
{
    if (explore_version_requested(argc, argv)) {
        /* Phase 338: the version alone on stdout, before the log, wox.cfg, the data, the audio and the window. */
        return fputs(XEEN_WOX_VERSION "\n", stdout) < 0 || fflush(stdout) ? 1 : 0;
    }
    host_run run = {.res = {.random = {.limit = 7, .replay_time = EXPLORE_DEFAULT_RNG_TIME}},
                    .replay = fresh_replay(),
                    .window = window_session(argc, argv),
                    .result = 1};
    if (run.window) {
        xeen_platform_physical_pixels(); /* Phase 333: before any window or dialog (Windows; elsewhere nothing) */
        /* wox.log beside the executable (the directory of the default wox.cfg) or the per-user fallback. */
        char beside[1024];
        (void)host_log_open(xeen_wox_config_locate(NULL, argv[0], beside, sizeof beside) ? beside : NULL, argc,
                            argv);
    }
    if (open_host(&run, argc, argv) && open_platform(&run)) {
        run_program(&run);
    }
    return close_host(&run);
}
