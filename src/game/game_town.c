#include "game_location.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/* Where native play stops when a town service cannot go on; each value is the original address of the step
 * (tools/xeen_replay/stops.json). */
enum {
    TOWN_STOP_REFUSAL_TEXT = 0x28d32,        /* the "cannot" message does not fit */
    TRAINING_STOP_MEMBER = 0x25583           /* the member to train past the six */
};

/* ------------- the training grounds and the temple
 * The 277E5 training grounds (kind 5: 2547B keys T and F1..F6) and the temple (kind 4: 256D9 keys D, H, U and
 * F1..F6) run in the shared location loop, with the location messages and payments below. The tavern is
 * game_tavern.c's; the event forms (2F2FF's yes/no, 2F5B1's choice) and operation 7's teleport (3193B, the host's
 * map_reload for another map, then the script ends) are game_event_forms.c's. The addresses in the comments are
 * the analysis placement. */

/* The level, experience, price and cure rules are xeen_* in town.c. */

/* ------------------------------------------------ the training grounds' and the temple's texts */
/* The training grounds' text for the customer: the town's top level stored (training_top_level) and the experience
 * still needed (1 at the top); the line "%s needs %lu experience for level %u." (some needed, below the top), "%s has
 * learned all we can teach!" (at the top) or "%s is eligible for level %d." with its cost (level x level x 10); then
 * the "Training Options" text with that line and the party's gold. Returns 0 when a text does not fit.
 * Original: 271AE: F64C (Clouds: map 28 10, 30 15, else 20; Darkside: 29 30, 31 50, 37 200, else 100); F646 =
 * 28C2D; A827; AC0F = DS:5361 with A827 and the gold (2689A). */
int train_text(xeen_game *g, char *out, size_t n)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    const uint8_t *ch = ds + g->location_member;
    char gold[24], line[256];
    uint32_t need, level = ch[XEEN_CHARACTER_LEVEL];
    uint8_t top, map = ds[XEEN_DS_PARTY_MAP];
    int k;
    top = xeen_training_top_level(ds[XEEN_DS_WORLD], map);
    ds[XEEN_DS_TRAINING_TOP_LEVEL] = top;
    need = xeen_experience_needed(m, ch);
    xeen_ds_write32(m, XEEN_DS_TRAINING_NEED, need);
    if (need && level < top) {
        k = snprintf(line, sizeof line, ds_text(g, XEEN_TEXT_TRAINING_NEEDS), ds_text(g, g->location_member),
                     (unsigned long)need, (unsigned)(level + 1u));
    } else if (level >= top) {
        k = snprintf(line, sizeof line, ds_text(g, XEEN_TEXT_TRAINING_ALL_LEARNED), ds_text(g, g->location_member));
        xeen_ds_write32(m, XEEN_DS_TRAINING_NEED, 1);
    } else {
        k = snprintf(line, sizeof line, ds_text(g, XEEN_TEXT_TRAINING_ELIGIBLE), ds_text(g, g->location_member),
                     (int)(level + 1u), (unsigned long)xeen_training_cost(ch));
    }
    if (k < 0 || (size_t)k >= sizeof line) {
        return 0;
    }
    memcpy(ds + XEEN_DS_TEXT_BUFFER, line, (size_t)k + 1u);
    location_amount(g, gold, sizeof gold, xeen_ds_read32(m, XEEN_DS_PARTY_GOLD));
    k = snprintf(out, n, ds_text(g, XEEN_TEXT_TRAINING_OPTIONS), ds_text(g, XEEN_DS_TEXT_BUFFER), gold);
    return k >= 0 && (size_t)k < n;
}
/* The temple's options text for the location's member into OUT (N bytes): the member's name, the Heal and Donate
 * prices, the Uncurse price ("%uk" above 999) and the party's gold, the prices computed by xeen_temple_prices_set
 * (which also leaves them in DS for the keys). Returns 0 when the text does not fit. Original: 2730E (273F3's prices;
 * AC0F = the 540E text). */
int temple_text(xeen_game *g, char *out, size_t n)
{
    xeen_movement *m = &g->movement;
    const uint8_t *ch = m->ds + g->location_member;
    char gold[24], uncurse[16];
    int w;
    xeen_temple_prices price;
    xeen_temple_prices_set(m, ch, g->location_blessed, &price);
    location_amount(g, gold, sizeof gold, xeen_ds_read32(m, XEEN_DS_PARTY_GOLD));
    xeen_temple_amount(uncurse, sizeof uncurse, price.uncurse);
    w = snprintf(out, n, ds_text(g, XEEN_TEXT_TEMPLE_OPTIONS), ds_text(g, g->location_member),
                 (unsigned long)price.heal, (unsigned long)price.donation, uncurse, gold);
    return w >= 0 && (size_t)w < n;
}

/*
 * A location's message: TEXT in the scroll box (message_show: the button set saved, the UI mode replaced), shown
 * while the location's own loop keeps running (its keeper still animates) until a key; OWNER (enum
 * location_message) decides what the close continues (location_message_closed): a member who cannot act or the
 * tavern's text, a service the party cannot pay for (from UNPAID on the payment's own button push is popped too),
 * the same at training (its refusal voice follows), the bank's two messages, or a script's payment refusal under
 * the event face's location loop.
 * Original: 18F2F(text, 2): 190ED/1919B, F5FE = 0, 9A1C saved and FFh, the 4B7A text in window 6, the 26E60 loop;
 * at the key 18765(6), 189BC, 18FFF, 9A1C back; owners 28CFC's refusal (A43F), 291FB (A827; its push popped at 29278),
 * 255D8, the bank's 291FB, 18FC8 under window 11. The loop returns to 25464's caller: 27F20.
 */
xeen_status location_message(xeen_game *g, int owner, uint16_t text)
{
    g->location_message_mode = message_show(g, ds_text(g, text));
    g->location_message = (uint8_t)owner;
    g->location_sampled = 0;
    return panel_set(g, LOCATION_POLL, LOCATION_POLL_BOUNDARY);
}
/** Continues a location after its message closed, according to the message shown. */
xeen_status location_message_closed(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds;
    if (g->location_message == LOCATION_MESSAGE_EVENT_PAYMENT) {
        xeen_presentation_trace_extra("evclose 6");
        xeen_presentation_trace_extra("evrestore 6");
    } /* Evidence */
    message_hide(g);
    ds[XEEN_DS_UI_MODE] = g->location_message_mode;
    if (g->location_message >= LOCATION_MESSAGE_UNPAID) {
        xeen_button_stack(ds, XEEN_BUTTON_POP, XEEN_DS_SEGMENT);
    }
    if (g->location_message == LOCATION_MESSAGE_EVENT_PAYMENT) {            /* 29278, back to 2CCAB/2CCC9 */
        xeen_presentation_trace_extra("evend"); /* Evidence: 2927D */
        g->location_message = LOCATION_MESSAGE_NONE;
        g->location_kind = g->payment_loc_kind;
        g->panel_stage = 0;
        g->event_payment = EVENT_MESSAGE_CLOSED;
        g->phase = XG_STOPPED;
        drive(g);
        return XEEN_SERVICE_REQUIRED;
    }
    if (g->location_message == LOCATION_MESSAGE_TRAINING_UNPAID) { /* 255D8: the training unpaid */
        voice_cancel(g, 0);
        location_voc(g, ds_text(g, ds[XEEN_DS_WORLD] ? XEEN_TEXT_TRAINING_REFUSED_VOICE_DARKSIDE : XEEN_TEXT_TRAINING_REFUSED_VOICE_CLOUDS));
        exit_event(g, "voice");
    }
    if (g->location_message == LOCATION_MESSAGE_BANK_EMPTY || g->location_message == LOCATION_MESSAGE_BANK_SHORT) { /* The bank's 291FB */
        unsigned owner = g->location_message;
        g->location_message = LOCATION_MESSAGE_NONE;
        return bank_message_closed(g, owner);
    }
    g->location_message = LOCATION_MESSAGE_NONE;
    if (g->tavern_food_step == TAVERN_FOOD_MESSAGE) {
        g->tavern_food_step = TAVERN_FOOD_NONE;
        panel_window(g, XEEN_WINDOW_BOTTOM, 0);
        panel_window(g, XEEN_WINDOW_RIGHT_COLUMN, 1);
    }
    return location_redraw(g);
}
/* Pays COST from the party's gold for a town service. Too little shows the location's "not enough" message for OWNER
 * (its routing push, the refusal sound and the text) and returns 0 with *STATUS; else 1.
 * Original: 29281(0, cost, 0, 2); 291FB(0, 0, 0, 2) and its 18F2F mode 2. */
int town_pay(xeen_game *g, uint32_t cost, int owner, xeen_status *status)
{
    if (party_pay(g, cost)) {
        return 1;
    }
    *status = location_message(g, owner, XEEN_DS_TEXT_BUFFER);
    return 0;
}
/* Plays the town voice NAME (a DS text naming the .voc), any playing voice cancelled first. Original: 187E9(0, 0),
 * the voice through 18975 into F42C, 187E9(1, F42C). */
void town_voice(xeen_game *g, uint16_t name)
{
    voice_cancel(g, 0);
    location_voc(g, ds_text(g, name));
    exit_event(g, "voice");
}
/* Whether the location's member cannot be served (xeen_training_refused: asleep, paralysed, unconscious, dead, stoned
 * or eradicated): then the member-cannot-act text with the member's name is shown as the location's plain message and
 * *STATUS is its result. Returns 1 when refused (or when the text does not fit: *STATUS the stop), 0 when the member
 * may be served.
 * Original: 28CFC(member): 2AB96's condition 8 or 11..15; A43F = the 18CA text of the name, 18F2F(A43F, 2) in a
 * location (9A1C 11h), AX 1. */
int location_refused(xeen_game *g, xeen_status *status)
{
    uint8_t *ds = g->movement.ds;
    char out[0x80];
    int k;
    if (!xeen_training_refused(ds + g->location_member)) {
        return 0;
    }
    k = snprintf(out, sizeof out, far_text(g, XEEN_DS_MEMBER_CANNOT_ACT_TEXT, 0),
                 ds_text(g, g->location_member));
    if (k < 0 || (size_t)k >= sizeof out) {
        *status = location_stop(g, TOWN_STOP_REFUSAL_TEXT);
        return 1;
    }
    memcpy(ds + XEEN_DS_TEXT_BUFFER_0, out, (size_t)k + 1u);
    *status = location_message(g, LOCATION_MESSAGE_PLAIN, XEEN_DS_TEXT_BUFFER_0);
    return 1;
}
/* The training grounds' T for the customer: with experience still needed (or at the top level) only the refusal
 * voice (Clouds: "needs more" below the top, "lost" at it; Darkside its own) and a redraw. A member who cannot act
 * is refused; the price is level x level x 10 (too little gold: the refusal message, then the voice). Paid: the
 * voice, the level trained (xeen_train_level: the experience lowered by the step between the two thresholds, the
 * level up by one); the member's first training this visit adds a day (1,440 minutes); the party's temporary bonuses
 * cleared, HP and SP to their maxima, the bar redrawn; then the location redrawn.
 * Original: 2549B (F646: 25601); 187E9(0, 0), 8D32 = 0, trainin0.voc / trainin1.voc / gtlost.voc; 28CFC; 29281;
 * trainin2.voc (prtygd.voc); 28BCF - (28C76 - XP), +23h + 1; F61E[F62C], 30CF3(1440); 29168; 2AAC2; 46470;
 * 2ABB8(1); 27F20. */
static xeen_status town_train(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds, *ch = ds + g->location_member;
    xeen_status status = XEEN_OK;
    uint32_t level = ch[XEEN_CHARACTER_LEVEL];
    uint16_t member = xeen_read16(m, XEEN_DS_TRAINEE);
    if (xeen_ds_read32(m, XEEN_DS_TRAINING_NEED)) {
        voice_cancel(g, 0);
        xeen_write16(m, XEEN_DS_ANIMATION_FRAME, 0);
        location_voc(g, ds_text(g, ds[XEEN_DS_WORLD] ? XEEN_TEXT_TRAINING_REFUSED_VOICE_DARKSIDE : level < ds[XEEN_DS_TRAINING_TOP_LEVEL] ? XEEN_TEXT_TRAINING_NEED_VOICE_CLOUDS : XEEN_TEXT_TRAINING_REFUSED_VOICE_CLOUDS));
        exit_event(g, "voice");
        return location_redraw(g);
    }
    if (location_refused(g, &status)) {
        return status;
    }
    if (!town_pay(g, xeen_training_cost(ch), LOCATION_MESSAGE_TRAINING_UNPAID, &status)) {
        return status;
    }
    xeen_write16(m, XEEN_DS_ANIMATION_FRAME, 0);
    town_voice(g, ds[XEEN_DS_WORLD] ? XEEN_TEXT_TRAINING_DONE_VOICE_DARKSIDE : XEEN_TEXT_TRAINING_DONE_VOICE_CLOUDS);
    xeen_train_level(m, ch);
    if (member >= 6u) {
        return location_stop(g, TRAINING_STOP_MEMBER);
    }
    if (!ds[XEEN_DS_MEMBER_TRAINED + member]) {
        xeen_gameplay_services services = session_services(g);
        uint32_t stop;
        if ((stop = xeen_gameplay_clock_add(m, 1440u /* one day, 30CF3(1440) */, &services)) != 0) {
            return location_stop(g, stop);
        }
        ch = ds + g->location_member; /* re-read after the clock's services */
        ds[XEEN_DS_MEMBER_TRAINED + member] = 1;
    }
    xeen_gameplay_reset_temps(m);
    XEEN_DAMAGE_NUMBERS(
            hp_restore(m->ds, g->location_member, xeen_combat_max_hp(m, ch))); /* add-on hook */
    xeen_write16(m, (uint16_t)(g->location_member + XEEN_CHARACTER_HP), xeen_combat_max_hp(m, ch));
    xeen_write16(m, (uint16_t)(g->location_member + XEEN_CHARACTER_SP), xeen_combat_max_sp(m, ch));
    bar_redraw(g); /* 2ABB8(1) (drawn) */
    ++g->trainings;
    return location_redraw(g);
}
/* A temple cure is done: a day (1,440 minutes) added to the visit's time charge (the exit charges it), the party bar
 * redrawn, the "ahh" voice, and the location redrawn. Original: 257A8: F628 += 1440 (30CF3 at the exit), 2ABB8(1),
 * ahh.voc. */
static xeen_status temple_done(xeen_game *g)
{
    xeen_movement *m = &g->movement;
    xeen_write16(m, XEEN_DS_LOCATION_TIME_CHARGE,
                 (uint16_t)(xeen_read16(m, XEEN_DS_LOCATION_TIME_CHARGE) + 0x5a0u));
    bar_redraw(g); /* 2ABB8(1) (drawn) */
    town_voice(g, XEEN_TEXT_TEMPLE_AHH_VOICE);
    ++g->healings;
    return location_redraw(g);
}
/* The temple's keys, each for the location's member and only at a nonzero price (else nothing): H heals (the party
 * pays; the temporary resistances and stats cleared, HP to the maximum, every condition but Cursed cleared); D donates
 * (the party pays; the donations count up modulo 10, and when the count equals the day modulo 10 the party is blessed:
 * Clairvoyance, light, and Heroism, Holy Bonus, Power Shield and Bless at the count, 10 for 0, with no more donation
 * that visit); U uncurses (the party pays; the curse condition and every item's cursed bit cleared); F1..F6 choose a
 * present member. Then the location is redrawn.
 * Original: 256D9 by key: H 256F9 heal (F642; 29281): the six temporary resistances
 * (138h..142h, even), +22h, +24h and the seven stat temporaries (15h..21h,
 * odd) cleared, HP = 2AAC2, every condition but 0 (cursed) cleared, then
 * 257A8; D 257CF donate (F63E; 29281): coina.voc, F62A = (F62A + 1) % 10;
 * equal to the day (EFF0) % 10: EDA1 = 1, EFF8 = 1, EDA3..EDA6 = F62A (10
 * for 0), 2ABB8(1), ahh.voc, 57AD:0000 = 1 and F63E = 0; then 189BC; U
 * 258B9 uncurse (F63A; 29281): condition 0 and the cursed bit (40h) of
 * every item row cleared, then 257A8; F1..F6 25960: a present member,
 * 2A1F6, F62A = 0. A zero price does nothing. Then 27F20. */
xeen_status temple_key(xeen_game *g, uint16_t key)
{
    xeen_movement *m = &g->movement;
    uint8_t *ds = m->ds, *ch = ds + g->location_member;
    xeen_status status = XEEN_OK;
    switch (key) {
        case 'H':
            if (!xeen_ds_read32(m, 0xf642)) {
                break;
            }
            if (!town_pay(g, xeen_ds_read32(m, 0xf642), LOCATION_MESSAGE_UNPAID, &status)) {
                return status;
            }
            xeen_temple_heal(m, ch);
            return temple_done(g);
        case 'D':
            if (!xeen_ds_read32(m, XEEN_DS_TEMPLE_DONATION_PRICE)) {
                break;
            }
            if (!town_pay(g, xeen_ds_read32(m, XEEN_DS_TEMPLE_DONATION_PRICE), LOCATION_MESSAGE_UNPAID,
                          &status)) {
                return status;
            }
            town_voice(g, XEEN_TEXT_BANK_COIN_VOICE);
            xeen_write16(m, XEEN_DS_TEMPLE_DONATIONS,
                         (uint16_t)((xeen_read16(m, XEEN_DS_TEMPLE_DONATIONS) + 1u) % 10u));
            if ((int16_t)xeen_read16(m, XEEN_DS_DAY) % 10 ==
                (int16_t)xeen_read16(m, XEEN_DS_TEMPLE_DONATIONS)) {
                uint8_t v = (uint8_t)(xeen_read16(m, XEEN_DS_TEMPLE_DONATIONS)
                                              ? xeen_read16(m, XEEN_DS_TEMPLE_DONATIONS)
                                              : 10u);
                ds[XEEN_DS_EFFECT_CLAIRVOYANCE] = 1;
                xeen_write16(m, XEEN_DS_PARTY_LIGHT, 1);
                ds[XEEN_DS_EFFECT_HEROISM] = ds[XEEN_DS_EFFECT_HOLY_BONUS] =
                        ds[XEEN_DS_EFFECT_POWER_SHIELD] = ds[XEEN_DS_EFFECT_BLESS] = v;
                (void)xeen_presentation_update_window(g->presentation, XEEN_WINDOW_SCREEN);
                town_voice(g, XEEN_TEXT_TEMPLE_AHH_VOICE);
                g->location_blessed = 1;
                xeen_ds_write32(m, XEEN_DS_TEMPLE_DONATION_PRICE, 0);
            }
            xeen_write16(m, XEEN_DS_INPUT_CODE, 0); /* 25F5E 189BC */
            ++g->donations;
            break;
        case 'U':
            if (!xeen_ds_read32(m, XEEN_DS_TEMPLE_UNCURSE_PRICE)) {
                break;
            }
            if (!town_pay(g, xeen_ds_read32(m, XEEN_DS_TEMPLE_UNCURSE_PRICE), LOCATION_MESSAGE_UNPAID, &status)) {
                return status;
            }
            xeen_temple_uncurse(ch);
            return temple_done(g);
        default:
            if (location_member_key(g, key)) {
                xeen_write16(m, XEEN_DS_TEMPLE_DONATIONS, 0);
            }
            break;
    }
    return location_redraw(g);
}
/* The training hall's keys (add-on training_preview's P first): T trains the trainee (town_train); F1..F6 make a
 * present member the trainee (location_member_key); then the location is redrawn. Original: 2547B: T 2549B; F1..F6
 * 2564B (F62C, 2A1F6); 27F20. */
xeen_status train_key(xeen_game *g, uint16_t key)
{
    xeen_movement *m = &g->movement;
    if (training_preview_key(g, key)) {
        return location_redraw(g); /* add-on training_preview: P */
    }
    if (key == 'T') {
        return town_train(g);
    }
    if (location_member_key(g, key)) {
        xeen_write16(m, XEEN_DS_TRAINEE, (uint16_t)(key - XEEN_KEY_F1));
    }
    return location_redraw(g);
}

