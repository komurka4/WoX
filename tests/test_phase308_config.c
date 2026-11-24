/* Phase 308: the merged wox.cfg (the movie player's keys in the game's parser: xeen_wox_config.movie) and the
 * movie's shared speech and song selection (xeen_movie_subtitles, xeen_movie_music_selector): game configs, the movie
 * keys, a merged config, an absent file, invalid values, sections and path handling. Phase 336: the keys are spelled
 * with '_' only and the former player's retired keys are refused. */
#include "xeen/wox_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            ++failures;                                             \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
        }                                                           \
    } while (0)

static char dir[512];
static int load(const char *text, xeen_wox_config *c, char *error)
{
    char path[600];
    FILE *f;
    snprintf(path, sizeof path, "%s/wox.cfg", dir);
    f = fopen(path, "wb");
    if (!f) {
        return 0;
    }
    fputs(text, f);
    fclose(f);
    return xeen_wox_config_load(path, c, error, 256);
}

int main(int argc, char **argv)
{
    xeen_wox_config c;
    char error[256], expect[1200];
    static const uint8_t genuine[29] = {0x0a, 0, 0x20, 2, 4, 0, 0x30, 3, 7, 0, 'e', 'r', 'e', 'i', 'a', 'm', 'j', 'h',
                                        0,    0, 1,    0, 0, 0, 1,    0, 1, 0, 3};
    uint8_t cfg[29];
    snprintf(dir, sizeof dir, "%s", argc > 1 ? argv[1] : ".");

    /* A game config (no movie keys): the movie defaults. */
    CHECK(load("[audio]\nmusic_override = mt32\n", &c, error));
    CHECK(!c.movie.cue[0] && !c.movie.cd_num_tracks && !c.movie.command[0] && !c.movie.sequence[0] &&
          !c.movie.dump_kind && !c.movie.score_given && !c.movie.badge_given);
    /* An absent file: the defaults (as before; nothing created by the loader). */
    xeen_wox_config_defaults(&c, dir);
    CHECK(!c.movie.cd_num_tracks);

    /* The movie keys: the user keys read, the developer keys read. */
    CHECK(load("[files]\ndata_dir = auto\ncue = disc/xeen.cue\ncd_num_tracks = 30\n[playback]\nsequence = darkside\n"
               "[endings]\nscore = 1234\nbadge = 2\n"
               "[timing]\ntiming = legacy\noriginal_timer_quirks = on\nclock = virtual\ndos_date = 1995-01-01\n"
               "dos_time = 00:00:00.00\n[diagnostics]\ndump_hashes = out\n",
               &c, error));
    snprintf(expect, sizeof expect, "%s/disc/xeen.cue", dir);
    CHECK(!strcmp(c.movie.cue, expect)); /* relative to wox.cfg's directory, as [files] data_dir */
    CHECK(c.movie.cd_num_tracks == 30u);
    CHECK(!strcmp(c.movie.sequence, "darkside") && !c.movie.command[0]);
    CHECK(c.movie.score_given && c.movie.score == 1234u && c.movie.badge_given && c.movie.badge == 2u);
    CHECK(c.movie.legacy_timing && c.movie.original_timer_quirks && c.movie.virtual_clock);
    CHECK(!strcmp(c.movie.dos_date, "1995-01-01") && !strcmp(c.movie.dos_time, "00:00:00.00"));
    CHECK(c.movie.dump_kind == 2);
    CHECK(c.speech == XEEN_SPEECH_AUTO);
    /* Phase 336: the former player's retired keys, its '-' spellings and true/false are refused. */
    {
        static const char *const refused[] = {
                "[playback]\nspeech-mode = subtitles\n", "[playback]\nsubtitles = forced\n",
                "[playback]\nhandoff-exec = auto\n",     "[diagnostics]\ndump-midi = auto\n",
                "[diagnostics]\ndump-opl = auto\n",      "[utility]\nversion = off\n",
                "[audio]\nexperimental-music = off\n",   "[files]\ncd-num-tracks = 30\n",
                "[timing]\noriginal-timer-quirks = on\n", "[timing]\noriginal_timer_quirks = true\n",
                "[timing]\ndos-date = 1995-01-01\n",      "[diagnostics]\ndump-hashes = out\n"};
        for (size_t i = 0; i < sizeof refused / sizeof *refused; ++i) {
            CHECK(!load(refused[i], &c, error));
        }
        CHECK(!load("[playback]\nspeech-mode = subtitles\n", &c, error) && strstr(error, "unknown setting: speech-mode"));
        CHECK(!load("[utility]\n", &c, error) && strstr(error, "unknown section: utility"));
        CHECK(!load("[timing]\noriginal_timer_quirks = true\n", &c, error) &&
              strstr(error, "must be on or off: original_timer_quirks"));
        CHECK(load("[timing]\noriginal_timer_quirks = off\n", &c, error) && !c.movie.original_timer_quirks);
    }

    /* A merged config: game and movie keys side by side. */
    CHECK(load("[audio]\nmusic_override = sc55\nspeech = off\n[files]\ncd_num_tracks = auto\ncue = auto\n[playback]\n"
               "command = endgame,0001234567\n[endings]\npayload = 0001234567\n",
               &c, error));
    CHECK(c.system == XEEN_MUSIC_SC55 && c.speech == XEEN_SPEECH_OFF && !c.movie.cd_num_tracks && !c.movie.cue[0]);
    CHECK(!strcmp(c.movie.command, "endgame,0001234567") && !strcmp(c.movie.payload, "0001234567"));

    /* Invalid values, sections and duplicates are refused, as the game's own keys are. */
    CHECK(!load("[files]\ncd_num_tracks = 0\n", &c, error));
    CHECK(!load("[files]\ncd_num_tracks = 100\n", &c, error));
    CHECK(!load("[endings]\npayload = 12345\n", &c, error));
    CHECK(!load("[endings]\nbadge = 3\n", &c, error));
    CHECK(!load("[endings]\nscore = -1\n", &c, error));
    /* Phase 328: the whole-number grammar (digits only, representable, in range), the same on every unsigned long
     * width: on a 32-bit long strtoul answers "-1" and every overflow as 4294967295, which the old test accepted. */
    {
        static const char *const refused[] = {"", "-1", "-0", "+7", " 7", "7 ", "0x10", "1e3", "4294967296",
                                              "18446744073709551615", "18446744073709551616", "99999999999999999999",
                                              "-4294967295", "-18446744073709551615", "\t1"};
        static const struct {
            const char *text;
            unsigned long value;
        } accepted[] = {{"0", 0ul}, {"1", 1ul}, {"007", 7ul}, {"1234", 1234ul}, {"4294967295", 4294967295ul},
                        {"0004294967295", 4294967295ul}};
        unsigned long n;
        for (size_t i = 0; i < sizeof refused / sizeof *refused; ++i) {
            n = 12345ul;
            CHECK(!xeen_wox_whole_number(refused[i], 0ul, 4294967295ul, &n) && n == 12345ul);
        }
        for (size_t i = 0; i < sizeof accepted / sizeof *accepted; ++i) {
            CHECK(xeen_wox_whole_number(accepted[i].text, 0ul, 4294967295ul, &n) && n == accepted[i].value);
        }
        CHECK(!xeen_wox_whole_number("0", 1ul, 99ul, &n) && !xeen_wox_whole_number("100", 1ul, 99ul, &n));
        CHECK(xeen_wox_whole_number("1", 1ul, 99ul, &n) && n == 1ul && xeen_wox_whole_number("99", 1ul, 99ul, &n));
        CHECK(!load("[endings]\nscore = 4294967296\n", &c, error) && strstr(error, "score must be auto"));
        CHECK(!load("[endings]\nscore = \"+5\"\n", &c, error));
        CHECK(!load("[endings]\nscore = -0\n", &c, error));
        CHECK(!load("[files]\ncd_num_tracks = -1\n", &c, error));
        CHECK(!load("[files]\ncd_num_tracks = 4294967297\n", &c, error)); /* 1 after a 32-bit wrap */
        CHECK(load("[endings]\nscore = 4294967295\n", &c, error) && c.movie.score == 4294967295u);
        CHECK(load("[endings]\nscore = 0\n", &c, error) && c.movie.score_given && c.movie.score == 0u);
        CHECK(load("[files]\ncd_num_tracks = 099\n", &c, error) && c.movie.cd_num_tracks == 99u);
    }
    CHECK(!load("[timing]\ntiming = exact\n", &c, error));
    CHECK(!load("[timing]\nclock = wall\n", &c, error));
    CHECK(!load("[audio]\ncue = auto\n", &c, error) && strstr(error, "cue belongs in [files]"));
    CHECK(!load("[playback]\nsequence = darkside\nsequence = clouds\n", &c, error) && strstr(error, "duplicate"));
    CHECK(!load("[diagnostics]\ndump_hashes = a\ndump_timings = b\n", &c, error));

    /* The movie's speech: [audio] speech, auto by 1eaee's gate (XEEN.CFG word 0 below 2: captions). */
    memcpy(cfg, genuine, sizeof cfg);
    xeen_wox_config_defaults(&c, dir);
    CHECK(xeen_movie_subtitles(&c, cfg, sizeof cfg) == 0); /* word 0 = 10 (prosnd): speech */
    cfg[0] = 1;
    CHECK(xeen_movie_subtitles(&c, cfg, sizeof cfg) == 1);
    cfg[0] = 2;
    CHECK(xeen_movie_subtitles(&c, cfg, sizeof cfg) == 0);
    CHECK(xeen_movie_subtitles(&c, NULL, 0) == 1); /* no XEEN.CFG: 1ef7f's zeroed image, word 0 = 0 */
    c.speech = XEEN_SPEECH_ON;
    CHECK(xeen_movie_subtitles(&c, NULL, 0) == 0);
    c.speech = XEEN_SPEECH_OFF;
    CHECK(xeen_movie_subtitles(&c, genuine, sizeof genuine) == 1);

    /* The song selector (14eb8): auto from XEEN.CFG's word 4, else the resolved driver's index. */
    xeen_wox_config_defaults(&c, dir);
    c.music_auto = 1;
    CHECK(xeen_movie_music_selector(&c, genuine, sizeof genuine) == 4u);
    CHECK(xeen_movie_music_selector(&c, NULL, 0) == 0u);
    c.music_auto = 0;
    c.system = XEEN_MUSIC_SC55;
    CHECK(xeen_movie_music_selector(&c, genuine, sizeof genuine) == 5u); /* canmus: cantheme.m */
    c.system = XEEN_MUSIC_MT32;
    CHECK(xeen_movie_music_selector(&c, genuine, sizeof genuine) == 4u); /* rolmus: bigtheme.m */
    c.system = XEEN_MUSIC_EXTERNAL;
    CHECK(xeen_movie_music_selector(&c, genuine, sizeof genuine) == 4u); /* system MIDI plays rolmus */
    c.system = XEEN_MUSIC_SBPRO;
    CHECK(xeen_movie_music_selector(&c, genuine, sizeof genuine) == 3u); /* promus: mm4theme.m */
    c.system = XEEN_MUSIC_OFF;
    CHECK(xeen_movie_music_selector(&c, genuine, sizeof genuine) == 0u);

    printf(failures ? "phase308 config: %d failures\n" : "phase308 config: all checks passed%.0d\n", failures);
    return failures != 0;
}
