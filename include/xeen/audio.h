#ifndef XEEN_AUDIO_H
#define XEEN_AUDIO_H
#include "xeen/movement.h"
#include "xeen/wox_config.h"
#include <stddef.h>
/* Native temporary silent backend. Records ordered effect requests without
 * waiting for playback; never modifies game state or simulates DOS hardware.
 * Counters wrap as unsigned values. Not XMI/VOC playback support. */
/* An optional borrowed playback sink (the native audio engine). It
 * only observes: every request reaches it after the silent backend's counters
 * and state are updated exactly as without it, nothing is returned to the game
 * and the voice still reports "not playing" (187E9(1, 0) stays 0), so replays,
 * polls, RNG and DS are unchanged. The sink stands in for the installed DOS
 * drivers (the native DS keeps its F42E/F430 driver words as before):
 * effects follow 1880E's ED9A gate, voices 187E9's ED9A gate, songs ED9B. */
typedef struct {
    void (*effect)(void *, uint16_t number);           /* 1880E: music driver cue */
    void (*voice)(void *, const uint8_t *voc, size_t); /* 18975 + 187E9: a VOC starts */
    void (*voice_name)(void *, const char *name,
                       unsigned cc);                   /* the same, by F5E8 name in CC DS 94 */
    void (*song)(void *, const uint8_t *song, size_t); /* 50AB1: a song loaded and started */
    void (*song_buffer)(void *, const uint8_t *song, size_t); /* 18975 of a song into F42A */
    void (*song_buffer_start)(void *);                        /* 187A5(F42A): F42A's song starts */
    void (*music)(void *, unsigned control); /* 187A5(x): 0 stop, 1 restart, 2..FFh fade */
    void (*volume)(void *, unsigned level);  /* 187CB(x): the song volume */
    /* 483C7's play (483B4 then 482D8): disc 0 = CD1, 1 = CD2; the
     * CD frames (1/75 s) from and to, relative to the track's start (from may
     * be negative, to may pass the track's end: the play is absolute on the
     * disc). 483B4: stop. */
    void (*cd_play)(void *, unsigned disc, unsigned track, int32_t from, int32_t to);
    void (*cd_stop)(void *);
    void *context;
    /* 187E9(0, 0) through the driver's entry 3 (argument 0): the
     * playing voice stops. Optional (NULL: ignored). */
    void (*voice_stop)(void *);
    /* Phase 308: between the movie player and the game (one process): the music driver's service shutdown (entry 3,
     * XEEN.EXE's common cleanup 14cf7: 1ee18(0) then 1ed49 -> 2a651: every note and running effect program stopped)
     * and the fresh driver start (entry 0) of the next program (XEEN.DAT's 15CDB loads and initialises its own driver).
     * The synth and the output device stay. Optional (NULL: ignored). */
    void (*music_restart)(void *);
} xeen_audio_sink;
/* One 47F3D query: the caller's return site (30454, 2FB31, 26D7B,
 * 18ED9), the clock, the play's tick and length, the answer. */
/* Outstanding, a play requested and not stopped since (the drive's
 * request history: a natural end keeps it, a stop or a disc swap clears it).
 * request: 0 a status query; 1 a play (482D8: frames its length), 2 a stop
 * (483B4 or 47F7F's): the same observer sees the drive's requests in order,
 * without counting them as queries. */
typedef struct xeen_cd_status_query {
    unsigned long n;
    uint32_t site;
    uint64_t now, start;
    uint32_t frames;
    int active, busy, outstanding, request;
} xeen_cd_status_query;
/* The kinds of 187E9 call (its arguments): a query (1, 0), a start, a stop (0, 0). */
enum xeen_voice_request_kind { XEEN_VOICE_QUERY = 0, XEEN_VOICE_START = 1, XEEN_VOICE_STOP = 2 };
/* One 187E9 call through the model: its kind (enum xeen_voice_request_kind); the caller's site (its return address),
 * the clock, the voice's start and length, the gate (ED9A) and the answer. */
typedef struct xeen_voice_request {
    unsigned long n;
    int kind;
    uint32_t site;
    uint64_t now, start;
    uint32_t samples;
    int gate, active, busy;
} xeen_voice_request;
typedef struct {
    unsigned long effects;
    uint16_t last_effect;
    /* Silent music starts complete immediately: no audible or timed playback
     * is claimed. The ordered load/start counters describe accepted commands. */
    unsigned long music_loads, music_starts, music_stops;
    size_t music_size;
    int music_playing;
    /* CD voice requests (483C7 starts an event's PlayCD tuple:
     * the track and its two time words; 483B4 stops). The silent backend
     * plays nothing, so 47F3D's status is never busy. */
    unsigned long cd_starts, cd_stops;
    uint16_t cd_track, cd_from, cd_to;
    const xeen_audio_sink *sink; /* NULL after xeen_native_audio_init */
    /* The sink's model of the drive (not DS): the disc 19CC3 last
     * asked for, and the last play's disc and frames. */
    uint8_t cd_drive, cd_drive_known, cd_disc;
    int32_t cd_start_frame, cd_end_frame;
    /* The drive's status (47F3D, FBF8 & 200h) as a deterministic
     * model, not the playback's progress. cd_model 0: no device (never busy,
     * the historical harness's platform: 50B26 answers nothing); 1: a drive
     * busy from a play (482D8's 84h) until its length (end - start frames,
     * 75 per second) has passed on the game's BIOS clock: cd_now is the last
     * INT 1Ah reading (xeen_timer.clock), set by the game before each query.
     * A stop (483B4) or a disc swap ends it; a new play restarts it. */
    uint8_t cd_model, cd_active;
    uint64_t cd_now, cd_play_tick;
    uint32_t cd_play_frames;
    unsigned long cd_status_queries, cd_status_busy;
    /* A play since the last stop or disc swap (cd_active is
     * cleared at the first idle query; this is not). */
    uint8_t cd_outstanding;
    void (*cd_status_observer)(void *, const xeen_cd_status_query *);
    void *cd_status_context;
    /* The digital driver's voice status (187E9 -> 50AC5 -> the
     * driver's entry 3; phase169/PREFLIGHT.md) as a deterministic model on the
     * game's BIOS clock, never the playback's progress. voice_model 0: no
     * digital driver (the historical harness's F42E = 0: 187E9 returns 0 and
     * starts nothing; the headless default); 1: an SB-family driver (blastsnd,
     * prosnd, specsnd): a start (any argument but 0 and 1) plays
     * xeen_voc_driver_samples bytes, one per 68 us; argument 1 answers busy
     * until they have played on voice_now (the INT 1Ah ticks the game sets
     * before each call); argument 0 and a new start end it. 187E9's ED9A gate
     * applies to each call (with ED9A clear nothing reaches the driver: a
     * playing voice goes on and the answer is 0). */
    uint8_t voice_model, voice_trim, voice_active;
    uint64_t voice_now, voice_start;
    uint32_t voice_samples;
    unsigned long voice_queries, voice_busy, voice_starts, voice_stops;
    void (*voice_observer)(void *, const struct xeen_voice_request *);
    void *voice_context;
    /* The driver samples of a voice named in CC (0 XEEN.CC, 1 DARK.CC), for
     * the F5E8-named combat voices the sink reads itself; NULL or 0: unknown
     * (the model then counts no length: never busy). */
    uint32_t (*voice_lookup)(void *, const char *name, unsigned cc);
    void *voice_lookup_context;
    /* Phase 286: [audio] speech = off (wox_config.h): the voice tests take their no-voice branch and the panel's
     * Speech is held off (xeen_native_audio_speech_hold: SPEECH_ON and TEXT_OFF stored 0, as the panel's Speech off
     * stores TEXT_OFF 0 at 2976D). 0 for every other setting: nothing changes. */
    uint8_t speech_off;
    /* Phase 287: the PC speaker's blocking start (XEEN_VOICE_MODEL_PCSPEAKER): ibmsnd's start returns only after its
     * IRQS interrupts of 81 PIT cycles (the host's service holds the game for them: the window waits, a headless run
     * advances its clock; xeen_game_set_voice_block). NULL: no hold (the start returns at once). */
    void (*voice_block)(void *, uint32_t irqs);
    void *voice_block_context;
    unsigned long voice_blocks;
} xeen_native_audio;
/* The SB digital drivers' rate (blastsnd, prosnd: time constant BCh, 1 000 000 / 68 Hz; the drivers never read the
 * VOC's own time constant). INTRO.CC's specsnd (the Pro Audio Spectrum, XEEN.CFG sound 8) programs the PAS sample
 * timer with divisor 51h (0360h): one sample per 81 cycles of the 1193182 Hz clock, 14731 Hz (Phase 285A). */
#define XEEN_VOC_DRIVER_RATE_HZ 14706u
#define XEEN_VOC_PAS_RATE_HZ    14731u
#define XEEN_VOC_PAS_SAMPLE_PIT 81u
/* The voice model's driver kinds (xeen_native_audio_voice_model's MODEL): none (nullsnd, or no driver: 187E9
 * answers 0 and nothing plays), an SB driver (blastsnd, prosnd; also the labelled approximation for ibmsnd, adsnd,
 * source and covxsnd), the PAS's specsnd. */
enum { XEEN_VOICE_MODEL_NONE = 0, XEEN_VOICE_MODEL_SB = 1, XEEN_VOICE_MODEL_PAS = 2, XEEN_VOICE_MODEL_ADLIB = 3,
       XEEN_VOICE_MODEL_SOURCE = 4, XEEN_VOICE_MODEL_PCSPEAKER = 5 };
/* The run's voice model from [audio] speech's resolution S (xeen_speech_resolve) and XEEN.CFG's sound driver (S's
 * word; HAVE_CFG 0 when no XEEN.CFG was found): *KIND an XEEN_VOICE_MODEL_*, *TRIM the driver's VOC length trim.
 * Off and nullsnd model no voice; on over nullsnd or the PC speaker models the SB driver (prosnd's); auto keeps the
 * driver's: ibmsnd (1) its blocking start (Phase 287), specsnd (8) the PAS, adsnd (2) and source (5) their own timing
 * (Phase 286), covxsnd (9) and the rest the SB model (covxsnd's rate not established: labelled). blastsnd's trim is 0Eh,
 * every other driver's 0Ah. Without XEEN.CFG the SB model stands unless speech is off, S's audible and modelled follow
 * speech = off alone and *TRIM is left as it is. (The host's model, explore.c; Phase 332 moved it here to be tested.) */
void xeen_speech_voice_model(xeen_speech_resolution *s, int have_cfg, unsigned *kind, unsigned *trim);
/* Phase 287 (owner option (a) of Phase 286's question 1), from ibmsnd's code (xeen_dat_evidence/phase287/a/
 * lin_ibmsnd.asm; Phase 286's oracle, 180 lengths): XEEN.CFG sound 1, the PC speaker. Its start (entry 3) programs
 * PIT channel 0 to 51h, stores the music driver's word 0Fh = 2 and busy-waits on its own flag while each interrupt
 * plays a byte from 20h; the interrupt whose byte position reaches the length word L ends it (0Fh = 0, the PIT back
 * at 4006h). So the start returns after N = (L - 20h) mod 65536 interrupts (L = 20h: 65536) of 81 cycles, and every
 * later query answers 0 (never busy). Its interrupts call the music driver's timer (000Ch) once per carry of a
 * 16-bit accumulator advanced 148h a byte (not by the last): M = floor(148h x (N - 1) / 65536) times during the start. The voice is not
 * heard (the owner's policy: no PC-speaker output); only its time is kept. */
#define XEEN_VOC_PCSPEAKER_SAMPLE_PIT 81u
#define XEEN_VOC_PCSPEAKER_CHAIN_STEP 0x148u
/* Phase 287: the PC speaker's blocking start of IRQS interrupts: the music driver's timer calls during it
 * (floor(148h x (IRQS - 1) / 65536): the last interrupt ends the voice before its add), and the BIOS ticks they chain (floor(calls x 4006h / 65536): every music driver's
 * accumulator from phase 0; ibmmus, CLOCK_STANDS, chains none: xeen_dat_evidence/phase287/a/block_oracle.txt). */
uint32_t xeen_voc_pcspeaker_music_calls(uint32_t irqs);
uint32_t xeen_voc_pcspeaker_bios_ticks(uint32_t irqs, int clock_stands);
/* Phase 287: the voice's length in PIT cycles from its start to the interrupt that ends it, for KIND (the model's
 * driver kinds; UINT64_MAX for source's never-ending voice; 0 for none or a VOC too short to read). */
uint64_t xeen_voc_driver_pit(const uint8_t *voc, size_t n, unsigned kind, unsigned trim);
/* Phase 287: 1 for the drivers that store the music driver's word 0Fh = 2 while their voice plays (ibmsnd, adsnd,
 * source); the FM music drivers then leave channel 7 alone (music_voice). */
int xeen_voc_driver_marks_music(unsigned kind);
/* Phase 286, from the INTRO.CC drivers' code (xeen_dat_evidence/phase286/a/lin_adsnd.asm, lin_source.asm):
 *   adsnd (XEEN.CFG sound 2, AdLib): PIT channel 0 at 51h (a sample per 81 cycles; a slow CPU's branch, 162 cycles a
 *     second byte, lasts the same), one byte from 20h per interrupt into OPL register 54h, ended by the interrupt whose
 *     byte reaches the length word L at 1Bh: busy for max(1, L - 20h) interrupts; L - 21h samples heard.
 *   source (5, the Disney Sound Source): PIT channel 0 at 554h (1364 cycles), each interrupt feeds 8 samples (every
 *     other byte) and moves 17 bytes on, until an interrupt finds its position past L: busy for (L - 20h) / 17 + 2
 *     interrupts (1 when L < 20h); 8 samples per interrupt heard, about 6998 Hz.
 * Both chain the 16390-cycle tick (the music driver's, then the BIOS's), so the BIOS clock keeps its rate. */
#define XEEN_VOC_ADLIB_SAMPLE_PIT  81u
#define XEEN_VOC_SOURCE_IRQ_PIT    1364u
#define XEEN_VOC_SOURCE_STEP       17u
#define XEEN_VOC_SOURCE_PER_IRQ    8u
/* The music drivers' effect entry (driver:0009, from 1880E): cues 0..XEEN_MUSIC_CUE_LAST (a higher one is refused),
 * and while an effect plays (its driver's effect-active byte) the cues XEEN_MUSIC_EFFECT_FIRST .. _END - 1 (7..10)
 * are refused too. */
#define XEEN_MUSIC_CUE_LAST        180u
#define XEEN_MUSIC_EFFECT_FIRST    7u
#define XEEN_MUSIC_EFFECT_END      11u
/* The model's count for KIND: the SB drivers' and specsnd's samples (xeen_voc_driver_samples with TRIM), adsnd's
 * and source's interrupts (above). */
uint32_t xeen_voc_driver_count(const uint8_t *voc, size_t n, unsigned kind, unsigned trim);
/* The driver's sample period in microseconds (256 - BCh) and the VOC image's offsets it reads: the first block's
 * length word at 1Bh (after the 1Ah-byte header and the block's type) and its samples from 20h (after the rate
 * and pack bytes), unsigned 8-bit (80h silence). The PIT runs at 1193182 Hz; a BIOS tick is 65536 PIT cycles. */
#define XEEN_VOC_DRIVER_SAMPLE_US 68u
#define XEEN_VOC_FIRST_LENGTH     0x1bu
#define XEEN_VOC_FIRST_SAMPLES    0x20u
#define XEEN_VOC_SILENCE          0x80u
#define XEEN_PIT_HZ               1193182u
#define XEEN_BIOS_TICK_PIT        65536u
/* The bytes the driver plays for a VOC image: the word at 1Bh (the first
 * block's length, low 16 bits) minus TRIM (16-bit wrap), plus one; 0 when the
 * image is shorter than 1Dh bytes. The DMA's 64 KiB page split (one more
 * byte) depends on the DOS segment and is not modelled. */
uint32_t xeen_voc_driver_samples(const uint8_t *voc, size_t n, unsigned trim);
/* busy for SAMPLES started at tick START, at tick NOW:
 * (NOW - START) * 65536 * 1000000 < SAMPLES * 68 * 1193182 (exact). */
int xeen_voc_busy_at(uint64_t start, uint64_t now, uint32_t samples);
/* The same for a driver KIND (XEEN_VOICE_MODEL_*): the PC speaker never busy (ibmsnd's query answers 0 after its start);
 * adlib and source busy while (NOW - START) * 65536 < SAMPLES * their period (XEEN_VOC_ADLIB_SAMPLE_PIT,
 * XEEN_VOC_SOURCE_IRQ_PIT); PAS (specsnd) while (NOW - START) * 65536 < SAMPLES * XEEN_VOC_PAS_SAMPLE_PIT; any other
 * kind as SB (xeen_voc_busy_at). Ticks 2^27 or more after the start are never busy (the products stay in 64 bits). */
int xeen_voc_busy_at_kind(uint64_t start, uint64_t now, uint32_t samples, unsigned kind);
/* Sets the voice model: MODEL XEEN_VOICE_MODEL_PAS, _ADLIB, _SOURCE or _PCSPEAKER as given, any other nonzero value an
 * SB-family driver, 0 no digital driver; TRIM the SB drivers' length trim (0Ah or 0Eh); no voice is active after. */
void xeen_native_audio_voice_model(xeen_native_audio *, int model, unsigned trim);
/* 187E9(1, 0) from SITE: counts, reports, returns the driver's answer. */
int xeen_native_audio_voice_query(xeen_native_audio *, const xeen_movement *, uint32_t site);
/* 187E9(0, 0) from SITE: the driver stops the voice (the sink's voice_stop). */
void xeen_native_audio_voice_stop(xeen_native_audio *, const xeen_movement *, uint32_t site);
/* A start from SITE with the VOC image (the sink plays it; the model counts it). */
void xeen_native_audio_voice_at(xeen_native_audio *, const xeen_movement *, const uint8_t *, size_t,
                                uint32_t site);
/* busy for a play of FRAMES started at tick START, at tick NOW:
 * (NOW - START) * 65536 * 75 < FRAMES * 1193182 (INT 1Ah ticks; exact). */
int xeen_cd_busy_at(uint64_t start, uint64_t now, uint32_t frames);
void xeen_native_audio_init(xeen_native_audio *);
/* Phase 286: with speech_off, stores the panel's Speech off in DS (SPEECH_ON 0, TEXT_OFF 0); else nothing. */
void xeen_native_audio_speech_hold(const xeen_native_audio *, uint8_t *ds);
xeen_status xeen_native_audio_effect(xeen_native_audio *, const xeen_movement *, uint16_t);
/* Sink forwarding (no-ops without a sink; no game state is read back). */
void xeen_native_audio_voice(xeen_native_audio *, const xeen_movement *, const uint8_t *, size_t);
void xeen_native_audio_voice_name(xeen_native_audio *, const xeen_movement *);
void xeen_native_audio_song(xeen_native_audio *, const xeen_movement *, const uint8_t *, size_t);
void xeen_native_audio_song_buffer(xeen_native_audio *, const uint8_t *, size_t);
void xeen_native_audio_music(xeen_native_audio *, const xeen_movement *, unsigned control);
/* The driver's 50AB1(control) called directly, not through 187A5:
 * no F430/ED9B gate (the control panel's music off, 299DE 50AB1(0)). */
void xeen_native_audio_music_driver(xeen_native_audio *, unsigned control);
void xeen_native_audio_start_buffer(xeen_native_audio *, const xeen_movement *);
void xeen_native_audio_volume(xeen_native_audio *, unsigned level);
/* Track is 84E0 after 483FC's -100, over_99 whether it applied,
 * disc the side (xeen_cd_side). Counts as before; the sink gets the segment. */
void xeen_native_audio_cd_start(xeen_native_audio *, uint16_t track, uint16_t from, uint16_t to,
                                int over_99, unsigned disc);
void xeen_native_audio_cd_stop(xeen_native_audio *);
/* 19CC3 with speech on found this side. A different disc than the
 * drive's would be swapped by 47F7F, whose two 483B4 stop the audio: the sink
 * gets a stop (no counter, no DS). */
void xeen_native_audio_cd_side(xeen_native_audio *, unsigned side);
/* Gameplay's status at cd_now, without counting: 0 with no device (the
 * default) or no active play. Real playback stays an observational sink; the
 * status never reads the engine. */
int xeen_native_audio_cd_busy(const xeen_native_audio *);
/* 47F3D called from SITE (its caller's return address): counts the
 * query, reports it to the observer and returns 1 when busy. */
int xeen_native_audio_cd_status(xeen_native_audio *, uint32_t site);
/* The two status queries the libraries make themselves (the game's own are its *_CD_STATUS_CALL names): */
enum xeen_cd_status_call {
    XEEN_CD_STATUS_PRELOAD_CALL = 0x47b96, /* the disc preload: a busy drive is stopped before the next side */
    XEEN_CD_STATUS_MUSIC_CALL = 0x18ed9    /* the input poll's song change: a busy drive defers the song */
};
/* 0 no device, 1 the modeled drive (an active play is ended). */
void xeen_native_audio_cd_model(xeen_native_audio *, int model);
/* 483C7 over DS (the PlayCD tuple 84E0..84E5, F385, FB15 + 7n,
 * FB06 = 0 after the play); the caller sets cd_now. */
void xeen_native_audio_cd_voice(xeen_native_audio *, uint8_t *ds);
/* 47B7D before a resource load: a status query at 47B96; busy and
 * F614[96E2] = 0 stop the play and set FB06 (returns 1). */
int xeen_native_audio_cd_preload(xeen_native_audio *, uint8_t *ds);
/* The wrappers' FB06 test after the load: FB06 set -> 483C7. */
void xeen_native_audio_cd_postload(xeen_native_audio *, uint8_t *ds);
/* 19CC3's side: 96E2, but 0 on the Darkside maps it treats as the
 * first disc's (35h..38h, 49h..4Dh, 53h..58h, 7Dh..7Fh, 81h). 0 = CD1, 1 = CD2
 * (47C1F/47F7F: F61A = the volume label's digit - '1'). */
unsigned xeen_cd_side(uint8_t world, uint8_t map);
/* The audio tracks of each disc (track 1 is data): CD1 2..31, CD2 2..30. */
#define XEEN_CD_FIRST_TRACK 2u
unsigned xeen_cd_last_track(unsigned disc);
/* The owner's rips: CD1 track t = xeenTT.ogg, CD2 track t = xeen(t+30).ogg.
 * 1 and the name, or 0 for a disc or track that has no audio. */
int xeen_cd_track_file(unsigned disc, unsigned track, char *name, size_t n);
/* 483C7's segment in CD frames from the track's start: the decimal MMSSFF
 * words (over_99: +65535 to a word below 60000), 48167's (m*60+s)*75+f with
 * its 16-bit intermediate words; from - 25 (0AFh - 96h) and to. */
void xeen_cd_segment(uint16_t from, uint16_t to, int over_99, int32_t *start, int32_t *end);
/* Selected original blastmus driver image, with its mutable software state.
 * Loading the resource is not execution of DOS driver initialization. The host
 * must supply an initialized image or explicitly label controlled init state. */
typedef struct {
    uint8_t segment[65536];
    uint16_t dos_segment;
    int loaded;
} xeen_audio;
typedef struct {
    uint16_t port;
    uint8_t value;
} xeen_audio_output;
/* The sound effect call (1881B, the step sound's and every effect's): the boundary of an effect that no loaded driver
 * can play. */
enum {
    XEEN_AUDIO_EFFECT_CALL = 0x1881b
};
typedef struct {
    xeen_audio_output writes[12];
    size_t write_count;
    unsigned pit_reads;
    uint16_t ax, bx, cx, dx;
    uint32_t boundary; /* XEEN_AUDIO_EFFECT_CALL, or 0 */
} xeen_audio_result;
/* Test support (the xeen_test_support library, src/test_support/; not in the game): */
xeen_status xeen_audio_load_blastmus(xeen_audio *, const void *, size_t, uint16_t);
/* Original 1880E wrapper ->4853:858B -> selected driver:0009. PIT port40
 * reads occur only in fixed hardware delays, and the supplied value is retained
 * in returned AL. OUT operations are returned in order for a headless adapter.
 * No interrupt/timer advancement is synthesized. No renderer is installed. */
/* Test support (the xeen_test_support library, src/test_support/; not in the game): */
xeen_status xeen_audio_play_fx(xeen_audio *, const xeen_movement *, uint16_t, uint16_t incoming_cx,
                               uint8_t pit_value, xeen_audio_result *);
/* Startup 47C1F reads exactly29 bytes at DS8D38. This describes selection and
 * hardware dependencies only; it does not initialize or authorize a driver. */
typedef struct {
    const char *sound_name, *music_name;
    uint16_t sound_port, music_port;
} xeen_audio_selection;
/* Test support (the xeen_test_support library, src/test_support/; not in the game): */
xeen_status xeen_audio_select_configuration(const void *, size_t, xeen_audio_selection *);
#endif
