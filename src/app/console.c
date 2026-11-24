/*
 * xeen_native: the line-mode developer console. It loads one world's CUR/CC archives from a data
 * directory, starts the game at a fixture position (or restores the original party), then reads
 * one command per line from stdin or --script and prints the party state and a map grid after
 * each step. Its printed banners and labels are part of the tests' expected output.
 */
#include "xeen/game.h"
#include "xeen/ds_globals.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/**
 * Reads the whole file NAME from directory DIR into a new buffer; returns NULL on any failure, else
 * the buffer (owned by the caller) and its *SIZE.
 */
static uint8_t *read_archive(const char *dir, const char *name, size_t *size)
{
    char path[4096];
    FILE *f;
    long length;
    uint8_t *bytes;
    int n = snprintf(path, sizeof path, "%s/%s", dir, name);
    if (n < 0 || (size_t)n >= sizeof path) {
        return NULL;
    }
    f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) || (length = ftell(f)) < 0 || length > 64L * 1024L * 1024L ||
        fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return NULL;
    }
    *size = (size_t)length;
    bytes = malloc(*size ? *size : 1u);
    if (!bytes) {
        fclose(f);
        return NULL;
    }
    if (fread(bytes, 1, *size, f) != *size || ferror(f)) {
        free(bytes);
        bytes = NULL;
    }
    fclose(f);
    return bytes;
}
/* Original controls are retained visibly as byte escapes. Never send resource
 * escape/control bytes to the host terminal. This is not DOS text rendering. */
static void text_bytes(const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n && p[i]; ++i) {
        unsigned c = p[i];
        if (c == '\n' || c == '\r') {
            putchar('\n');
        } else if (c >= 32 && c < 127) {
            putchar((int)c);
        } else {
            printf("<%02X>", c);
        }
    }
    putchar('\n');
}
/**
 * Text-terminal presentation callback: prints a presentation request (kind, face, mode and both
 * texts) to stdout; returns 1, or -1 when stdout fails.
 */
static int terminal_present(void *u, const xeen_ui_request *q)
{
    (void)u;
    printf("PRESENT kind=%u face=%u (face%02u.fac; portrait not rendered) mode=%u\n",
           (unsigned)q->kind, q->face, q->face, q->mode);
    text_bytes(q->primary, q->primary_size);
    text_bytes(q->secondary, q->secondary_size);
    return fflush(stdout) == 0 && !ferror(stdout) ? 1 : -1;
}
/** Returns the printable name of an event execution status. */
static const char *status_name(xeen_exec_status s)
{
    switch (s) {
        case XEE_DONE:
            return "done";
        case XEE_NO_MATCH:
            return "no-match";
        case XEE_UNSUPPORTED:
            return "unsupported-operation";
        case XEE_EXTERNAL:
            return "external-boundary";
        case XEE_MALFORMED:
            return "bounded-input-error";
        case XEE_BUDGET:
            return "operation-budget";
        default:
            return "internal-error";
    }
}
/**
 * Prints the game's current state to stdout: position, cell, metadata and, when GRID is set, the
 * surrounding map grid, plus the audio state.
 */
static void print_state(xeen_game *g, int grid)
{
    xeen_game_view v = xeen_game_inspect(g);
    const xeen_native_audio *audio = xeen_game_audio(g);
    printf("STATE world=%u map=%u x=%u y=%u facing=%u phase=%u tile=%04X metadata=%02X mode=%04X "
           "threshold=%u\n",
           v.position.world, v.position.map, v.position.x, v.position.y, v.position.direction,
           (unsigned)v.phase, v.tile, v.metadata, v.mode, v.threshold);
    if (!v.mode) {
        printf("Original facing-edge nibbles 0..3: %X %X %X %X (gate threshold %u; destination "
               "rules also apply)\n",
               (v.tile >> 12) & 15, (v.tile >> 8) & 15, (v.tile >> 4) & 15, v.tile & 15,
               v.threshold);
    }
    if (grid) {
        puts("Debug cell grid (y15 at top; @ player, e metadata bit10, . other cell). Symbols do "
             "not assert walkability.");
        puts("   0123456789ABCDEF");
        for (unsigned row = 16; row > 0; --row) {
            unsigned y = row - 1;
            printf("%02u ", y);
            for (unsigned x = 0; x < 16; ++x) {
                putchar(x == v.position.x && y == v.position.y ? '@'
                        : (xeen_game_metadata(g, x, y) & 0x10) ? 'e' /* the legend's bit10 is bit 10h (bit 4) */
                                                               : '.');
            }
            putchar('\n');
        }
    }
    printf("EVENT status=%s boundary=%05X offset=%u operation=%u sequence=%u steps=%zu "
           "presentations=%zu completions=%zu input=%04X\n",
           status_name(v.event_status), (unsigned)v.boundary, v.offset, v.operation, v.sequence,
           v.steps, v.presentations, v.completions, v.input);
    if (v.lifecycle_boundary) {
        printf("LIFECYCLE status=%u boundary=%05X AX=%04X clock=%u safe-to-continue=%d\n",
               (unsigned)v.command_status, (unsigned)v.lifecycle_boundary, v.dispatcher_ax, v.clock,
               v.safe_to_continue);
    }
    if (audio) {
        printf("AUDIO silent effects=%lu last-effect=%u\n", audio->effects, audio->last_effect);
    }
    if (v.boundary == XEEN_EVENT_SCRIPT_END && v.phase == XG_NAVIGATION) {
        puts("Native test event session ended at 33690; original dispatcher cleanup/return remains "
             "unimplemented. Navigation available.");
    }
    if (v.phase == XG_STOPPED && v.lifecycle_boundary) {
        puts("Original lifecycle cannot resume navigation safely. DS mutations retained; Q quits.");
    } else if (v.phase == XG_STOPPED) {
        puts("Event stopped explicitly. C leaves this native test event session, preserving DS; Q "
             "quits.");
    }
}
/**
 * Answers every pending presentation or text request with an empty response, so a terminal session
 * continues past presentations that need no original key.
 */
static void complete_presentations(xeen_game *g)
{
    const xeen_ui_request *q;
    while ((q = xeen_game_pending(g)) != NULL && (q->kind == XUI_PRESENT || q->kind == XUI_TEXT)) {
        puts("PRESENTATION COMPLETE (no original keyboard response)");
        if (!xeen_game_respond(g, 0)) {
            break;
        }
    }
}
/** Parses S as a decimal number no greater than MAX into *VALUE; returns 0 for anything else. */
static int parse_unsigned(const char *s, unsigned max, unsigned *value)
{
    char *end;
    unsigned long v;
    errno = 0;
    v = strtoul(s, &end, 10);
    if (errno || !*s || *end || v > max) {
        return 0;
    }
    *value = (unsigned)v;
    return 1;
}
/* xeen_native's command line (console_parse). */
typedef struct {
    const char *dir;    /* --data-dir: the DOS directory (default "dos") */
    const char *script; /* --script: the command file, or NULL for standard input */
    int world;          /* --world: 0 Clouds, 1 Darkside, -1 asked on the input */
    int grid;           /* 0 with --no-grid: the map grid only on M */
    int lifecycle;      /* --verified-lifecycle (also set by --restore-party) */
    int restore_party;  /* --restore-party: the original CUR party */
    int silent_audio;   /* --silent-audio (only with --restore-party) */
    int custom;         /* --fixture MAP X Y FACING given */
    unsigned map, x, y, facing;
} console_options;
/** Parses the arguments into *O; returns 0 for anything the usage line does not allow. */
static int console_parse(int argc, char **argv, console_options *o)
{
    *o = (console_options){.dir = "dos", .world = -1, .grid = 1};
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--data-dir") && i + 1 < argc) {
            o->dir = argv[++i];
        } else if (!strcmp(argv[i], "--script") && i + 1 < argc) {
            o->script = argv[++i];
        } else if (!strcmp(argv[i], "--world") && i + 1 < argc) {
            const char *w = argv[++i];
            if (!strcmp(w, "clouds") || !strcmp(w, "0")) {
                o->world = 0;
            } else if (!strcmp(w, "darkside") || !strcmp(w, "1")) {
                o->world = 1;
            } else {
                return 0;
            }
        } else if (!strcmp(argv[i], "--verified-lifecycle")) {
            o->lifecycle = 1;
        } else if (!strcmp(argv[i], "--restore-party")) {
            o->restore_party = 1;
            o->lifecycle = 1;
        } else if (!strcmp(argv[i], "--silent-audio")) {
            o->silent_audio = 1;
        } else if (!strcmp(argv[i], "--no-grid")) {
            o->grid = 0;
        } else if (!strcmp(argv[i], "--fixture") && i + 4 < argc) {
            if (!parse_unsigned(argv[i + 1], 255, &o->map) || !o->map ||
                !parse_unsigned(argv[i + 2], 15, &o->x) || !parse_unsigned(argv[i + 3], 15, &o->y) ||
                !parse_unsigned(argv[i + 4], 3, &o->facing)) {
                return 0;
            }
            o->custom = 1;
            i += 4;
        } else {
            return 0;
        }
    }
    return !o->silent_audio || o->restore_party;
}
/** Prints the usage line for program ARGV0 on stderr; returns the exit status 2. */
static int usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [--world clouds|darkside] [--data-dir DIR] [--script FILE] [--no-grid] "
            "[--verified-lifecycle] [--restore-party [--silent-audio]] [--fixture MAP X Y "
            "FACING]\n",
            argv0);
    return 2;
}
/**
 * The world and the start position of the session: an unset world is read from INPUT ("0" or "1"
 * first on the line), and without --fixture the world's test fixture (Clouds map 49 at 6,4, Darkside
 * map 29 at 14,3, facing 0). Returns 0 when the input gives no world.
 */
static int console_place(console_options *o, FILE *input)
{
    if (o->world < 0) {
        char line[256];
        fputs("Select world: 0 Clouds, 1 Darkside: ", stdout);
        fflush(stdout);
        if (!fgets(line, sizeof line, input) || (line[0] != '0' && line[0] != '1')) {
            return 0;
        }
        o->world = line[0] - '0';
    }
    if (!o->custom) {
        o->map = o->world ? 29u : 49u;
        o->x = o->world ? 14u : 6u;
        o->y = o->world ? 3u : 4u;
        o->facing = 0;
    }
    return 1;
}
/**
 * Creates the session over the world's archives STATE (SN bytes) and ASSETS (AN bytes, both
 * borrowed): the game at the fixture position, then as asked the original party, the silent audio
 * backend and the verified lifecycle, each announced. Returns NULL after the reason on a failure.
 */
static xeen_game *console_start(const console_options *o, const uint8_t *state, size_t sn,
                                const uint8_t *assets, size_t an)
{
    char error[160];
    xeen_game *g = xeen_game_create(state, sn, assets, an,
                                    (xeen_game_start){(uint8_t)o->world, (uint8_t)o->x, (uint8_t)o->y,
                                                      (uint8_t)o->facing, (uint16_t)o->map},
                                    terminal_present, NULL, error, sizeof error);
    if (!g) {
        fprintf(stderr, "Initialization failed: %s\n", error);
        return NULL;
    }
    if (o->restore_party) {
        const uint8_t *ds;
        /* The original party is restored into the original program state: the compiled program data first (the
         * restoration reads its member names from it), as xeen_game_create_saved does. */
        if (!xeen_game_load_program_data(g)) {
            fprintf(stderr, "Party restoration failed: program data disagrees with the native startup state\n");
            xeen_game_destroy(g);
            return NULL;
        }
        if (!xeen_game_restore_party(g, error, sizeof error)) {
            fprintf(stderr, "Party restoration failed: %s\n", error);
            xeen_game_destroy(g);
            return NULL;
        }
        ds = xeen_game_ds(g);
        printf("ORIGINAL PARTY restored count=%u real-count=%u; fixture placement retained; "
               "bounded startup, continuous gameplay unverified.\n",
               ds[XEEN_DS_PARTY_COUNT], ds[XEEN_DS_SAVED_PARTY_COUNT]);
    }
    if (o->silent_audio) {
        if (!xeen_game_enable_silent_audio(g)) {
            xeen_game_destroy(g);
            return NULL;
        }
        puts("AUDIO native silent development backend: nonblocking effects; XMI/VOC playback "
             "unavailable.");
    }
    if (o->lifecycle) {
        (void)xeen_game_enable_lifecycle(g);
        puts("Verified lifecycle mode: partial commands stop at original dependencies. C cannot "
             "unlock them.");
    }
    return g;
}
/**
 * The answer to the pending request Q from the input LINE (COMMAND its first character, upper
 * case): a confirmation takes Y (59h), N (4Eh) or Escape/"esc" (1Bh), any other request Enter or
 * Escape/"esc" (1Bh). Returns 0 after the hint for anything else.
 */
static int console_answer(const xeen_ui_request *q, int command, const char *line, uint16_t *answer)
{
    int escape = command == 27 || !strncmp(line, "esc", 3);
    if (q->kind == XUI_CONFIRM) {
        if (command == 'Y') {
            *answer = 0x59;
        } else if (command == 'N') {
            *answer = 0x4e;
        } else if (escape) {
            *answer = 0x1b;
        } else {
            puts("Use Y, N or esc; no response supplied.");
            return 0;
        }
    } else if (command == '\n' || command == '\r' || escape) {
        *answer = 0x1b;
    } else {
        puts("Use Enter or esc to dismiss; Q quits.");
        return 0;
    }
    return 1;
}
/**
 * Runs one command (COMMAND, the line's first character in upper case) with no request pending: C
 * leaves a stopped event session (refused in lifecycle mode), M prints the map, E interacts and
 * W/S/A/D move or turn. Returns 1 when the step's presentations and state follow, 0 when the
 * command printed all it shows (a refusal, the map, an unknown command).
 */
static int console_command(xeen_game *g, int command, int lifecycle)
{
    if (command == 'C') {
        if (lifecycle) {
            puts("Host abandonment cannot make this incomplete original lifecycle safe. DS "
                 "retained; Q quits.");
            return 0;
        }
        xeen_game_leave_event(g);
        puts("Native event session left explicitly; DS retained, original cleanup not "
             "executed.");
    } else if (command == 'M') {
        print_state(g, 1);
        return 0;
    } else if (command == 'E') {
        if (!xeen_game_interact(g)) {
            puts("No eligible interaction, or an event is still pending.");
        }
    } else if (command == 'W' || command == 'S' || command == 'A' || command == 'D') {
        uint16_t code = command == 'W'   ? XEEN_MOVE_FORWARD
                        : command == 'S' ? XEEN_MOVE_BACK
                        : command == 'A' ? XEEN_MOVE_TURN_LEFT
                                         : XEEN_MOVE_TURN_RIGHT;
        xeen_slice_navigation_result moved = xeen_game_navigate(g, code);
        printf("NAV %s\n", xeen_slice_navigation_name(moved));
    } else {
        puts("Use W/S/A/D/E/M/C/Q.");
        return 0;
    }
    return 1;
}
/**
 * The command loop: a prompt (the pending request's, else "> "), one line of INPUT (a longer line is
 * discarded with a note), Q or the input's end quits; a pending request takes the line as its answer
 * (console_answer), otherwise it is a command (console_command); after an answer or a command that
 * ran, the presentations that need no key are completed and the state printed.
 */
static void console_loop(xeen_game *g, FILE *input, int lifecycle)
{
    char line[256];
    for (;;) {
        const xeen_ui_request *q = xeen_game_pending(g);
        int command;
        if (q) {
            fputs(q->kind == XUI_CONFIRM ? "Confirm [Y/N/Escape]: " : "Dismiss [Enter or Escape]: ",
                  stdout);
        } else {
            fputs("> ", stdout);
        }
        fflush(stdout);
        if (!fgets(line, sizeof line, input)) {
            break;
        }
        if (!strchr(line, '\n') && !feof(input)) {
            int c;
            while ((c = fgetc(input)) != '\n' && c != EOF) {
            }
            puts("Input line too long.");
            continue;
        }
        command = toupper((unsigned char)line[0]);
        if (command == 'Q') {
            break;
        }
        if (q) {
            uint16_t answer = 0;
            if (!console_answer(q, command, line, &answer)) {
                continue;
            }
            printf("RESPONSE %04X\n", answer);
            (void)xeen_game_respond(g, answer);
        } else if (!console_command(g, command, lifecycle)) {
            continue;
        }
        complete_presentations(g);
        print_state(g, 0);
    }
}
/**
 * The xeen_native terminal driver: loads the game from a DOS directory, optionally places the
 * party, and runs the commands read from standard input or a script, printing the state after each.
 * Exit status 0 after a clean shutdown, 2 for a usage error, an unreadable script, no world on the
 * input, unreadable archives or a refused start.
 */
int main(int argc, char **argv)
{
    console_options o;
    FILE *input = stdin;
    uint8_t *state = NULL, *assets = NULL;
    size_t sn = 0, an = 0;
    xeen_game *g = NULL;
    int result = 2;
    if (!console_parse(argc, argv, &o)) {
        return usage(argv[0]);
    }
    if (o.script && !(input = fopen(o.script, "r"))) {
        perror(o.script);
        return 2;
    }
    puts(o.restore_party ? "WoX — original party restoration (Phase 11)"
                         : "WoX — native bounded exploration slice (Phase 9)");
    puts(o.restore_party ? "Original CUR party and bounded lifecycle; commands stop at "
                           "unreconstructed original services."
                         : "Original maps, collision gates and events. No restored party, time, "
                           "combat or full scene/dispatcher lifecycle.");
    if (console_place(&o, input)) {
        printf("TEST FIXTURE world=%d map=%u start=(%u,%u) facing=%u; not original game start.\n",
               o.world, o.map, o.x, o.y, o.facing);
        state = read_archive(o.dir, o.world ? "DARK.CUR" : "XEEN.CUR", &sn);
        assets = read_archive(o.dir, o.world ? "DARK.CC" : "XEEN.CC", &an);
        if (!state || !assets) {
            fputs("Cannot read selected world's original CUR/CC archives. Use --data-dir.\n", stderr);
        } else if ((g = console_start(&o, state, sn, assets, an)) != NULL) {
            puts("Controls (line input): W forward, S backward, A/D turn, E interact, M map, C leave "
                 "stopped event, Q quit.");
            puts("Facing0=+y,1=+x,2=-y,3=-x. These are native bindings; compass names are unverified.");
            print_state(g, o.grid);
            console_loop(g, input, o.lifecycle);
            puts("Clean shutdown.");
            result = 0;
        }
    }
    xeen_game_destroy(g);
    free(state);
    free(assets);
    if (input != stdin) {
        fclose(input);
    }
    return result;
}
