#include "xeen/party.h"
#include "xeen/ds_globals.h"
#include "xeen/event_ui.h"
#include "event_ui_internal.h"
#include "xeen/character_record.h"
#include "xeen/input_poll.h"
#include <string.h>
/**
 * Returns the highest condition index (0..15) set in CHARACTER's condition bytes (+143h), or 16
 * (XEEN_CONDITION_GOOD) when the character has none: xeen_character_worst_condition's scan, a copy because this
 * library does not link character_record.c.
 */
static uint8_t action_condition(const uint8_t *character)
{
    uint8_t index = 16u;
    while (index) {
        --index;
        if (character[XEEN_CHARACTER_CONDITIONS + index]) {
            return index;
        }
    }
    return 16u;
}
/**
 * Returns nonzero when two presentation requests are identical (kind, face, mode and both texts).
 */
int xeen_ui_same_request(const xeen_ui_request *a, const xeen_ui_request *b)
{
    return a->kind == b->kind && a->face == b->face && a->mode == b->mode &&
           a->primary_size == b->primary_size && a->secondary_size == b->secondary_size &&
           a->primary_size <= 1000 && a->secondary_size <= 1000 &&
           !memcmp(a->primary, b->primary, a->primary_size) &&
           !memcmp(a->secondary, b->secondary, a->secondary_size);
}
/**
 * Copies the NUL-terminated text IN (at most 1000 bytes) to OUT and stores its length including the
 * NUL in *LENGTH; returns 0 when IN has no terminator in that range.
 */
static int copy_text(uint8_t *out, size_t *length, const uint8_t *in)
{
    for (size_t i = 0; i < 1000; ++i) {
        out[i] = in[i];
        if (!in[i]) {
            *length = i + 1;
            return 1;
        }
    }
    return 0;
}
/** Initializes the event UI with its present and poll callbacks and their user pointer. */
void xeen_event_ui_init(xeen_event_ui *ui, xeen_ui_present_fn present, xeen_ui_poll_fn poll,
                        void *user)
{
    if (!ui) {
        return;
    }
    memset(ui, 0, sizeof(*ui));
    ui->present = present;
    ui->poll = poll;
    ui->user = user;
}
int xeen_event_ui_service(void *user, const xeen_event_request *r, uint8_t *ds, size_t n,
                          uint16_t *ax)
{
    if (!ds || n < 65536) {
        return -1;
    }
    return xeen_event_ui_service_texts(user, r, ds, n, ax, ds + XEEN_DS_TEXT_BUFFER, ds + XEEN_DS_TEXT_BUFFER_0);
}
/* Whether ADDRESS is one of the text services this host presents (text_request's cases). */
static int text_service(uint32_t address)
{
    switch (address) {
        case XEEN_REQUEST_NPC_DIALOGUE:
        case XEEN_REQUEST_TEXT_WINDOW:
        case XEEN_REQUEST_YES_NO:
        case XEEN_REQUEST_WHO_WILL:
        case XEEN_REQUEST_TREASURE_DIALOG:
        case XEEN_REQUEST_PAGED_TEXT_KEY:
        case XEEN_REQUEST_NOTHING_HERE_KEY:
        case XEEN_REQUEST_NUMERIC_CHOICE:
            return 1;
        default:
            return 0;
    }
}
/* Q as a request of KIND in MODE whose secondary text is empty (its size 1: the NUL). */
static void key_request(xeen_ui_request *q, xeen_ui_kind kind, uint16_t mode)
{
    q->kind = kind;
    q->mode = mode;
    q->secondary_size = 1;
}
/*
 * The UI request Q of text service R, one rule per service (Q cleared first for each of them):
 *   - an NPC dialogue (3000D): the face and mode from the arguments; mode 0 asks yes/no (CONFIRM), 1 waits for any
 *     key (DISMISS), 2 only presents; BODY is the secondary text and NAME the primary;
 *   - a paged text's or "Nothing here."'s key loop (316DC, 3380E): any key (DISMISS);
 *   - the yes/no (2F2FF): argument 1 any key (DISMISS), 0 the yes/no (CONFIRM: Y 1; N and Escape 0); the second
 *     argument (the wait's scene; the tavern's food passes 1) and the first above 1 are not this host's;
 *   - the numeric choice (2F5B1(n)): the keys 1..n, n 1..9 (CHOICE, mode n); a second argument is not this host's;
 *   - the Who-will prompt (2F9BA): CHARACTER in mode 1 (the title table's mode 0 as well), for a destination 0 or 1,
 *     no count, an argument up to 31 and a second up to 255;
 *   - the treasure dialog (2EA68): TREASURE;
 *   - a text window (315F4): TEXT, no key.
 * The primary text is the event text buffer (A827) except the NPC dialogue's NAME. Returns 1 when Q is filled, 0
 * for a service or arguments this host does not present (Q left as it is for another service), -1 when a text does
 * not fit.
 */
static int text_request(const xeen_event_request *r, const uint8_t *ds, const uint8_t *name, const uint8_t *body,
                        xeen_ui_request *q)
{
    if (!text_service(r->address)) {
        return 0;
    }
    memset(q, 0, sizeof(*q));
    switch (r->address) {
        case XEEN_REQUEST_NPC_DIALOGUE:
            q->face = r->argument;
            q->mode = r->second_argument;
            if (q->mode > 2) {
                return 0; /* no extrapolation to unreviewed modes */
            }
            q->kind = q->mode == 0 ? XUI_CONFIRM : q->mode == 1 ? XUI_DISMISS : XUI_PRESENT;
            if (!copy_text(q->secondary, &q->secondary_size, body)) {
                return -1;
            }
            break;
        case XEEN_REQUEST_PAGED_TEXT_KEY:   /* 316DC, 31786..317AA */
        case XEEN_REQUEST_NOTHING_HERE_KEY: /* 3380E..33832 */
            key_request(q, XUI_DISMISS, 1);
            break;
        case XEEN_REQUEST_YES_NO:
            if (r->argument > 1u || r->second_argument > 1u) {
                return 0;
            }
            if (r->argument) {
                key_request(q, XUI_DISMISS, 1);
            } else {
                key_request(q, XUI_CONFIRM, 0);
            }
            break;
        case XEEN_REQUEST_NUMERIC_CHOICE:
            if (!r->argument || r->argument > 9u || r->second_argument) {
                return 0;
            }
            key_request(q, XUI_CHOICE, (uint16_t)r->argument);
            break;
        case XEEN_REQUEST_WHO_WILL:
            if (r->destination > 1u || r->count || r->argument > 31u || r->second_argument > 255u) {
                return 0;
            }
            key_request(q, XUI_CHARACTER, 1);
            break;
        case XEEN_REQUEST_TREASURE_DIALOG:
            key_request(q, XUI_TREASURE, 1);
            break;
        default: /* XEEN_REQUEST_TEXT_WINDOW */
            key_request(q, XUI_TEXT, 0);
            break;
    }
    if (!copy_text(q->primary, &q->primary_size, r->address == XEEN_REQUEST_NPC_DIALOGUE ? name : ds + XEEN_DS_TEXT_BUFFER)) {
        return -1;
    }
    return 1;
}
/* Stores the answer's key in DS F5FE (a word), where the event script reads it back. */
static void store_input_code(uint8_t *ds, uint16_t code)
{
    ds[XEEN_DS_INPUT_CODE] = (uint8_t)code;
    ds[XEEN_DS_INPUT_CODE + 1u] = (uint8_t)(code >> 8);
}
/*
 * The member answer of a Who-will request: Esc answers 0; F1..F6 the member + 1 when the party has that member and
 * it can act (the original's form of xeen_condition_cannot_act over 0..16: refused when Asleep, or above Confused
 * and not Good; 28CFC refuses Asleep too). 1 accepted, 0 a member refused (the request stays pending), -1 any other
 * key. F5FE = AX (2F9BA leaves 0 for Esc, 2FBCC; the member + 1: 2FBD9 subtracts C9h, 2FC05 adds 1).
 */
static int member_answer(uint16_t input, uint8_t *ds, uint16_t *ax)
{
    unsigned selected;
    uint8_t condition;
    if (input == XEEN_KEY_ESCAPE) {
        *ax = 0;
        store_input_code(ds, 0);
        return 1;
    }
    if (input < XEEN_KEY_F1 || input > XEEN_KEY_F6) {
        return -1;
    }
    selected = (unsigned)input - XEEN_KEY_F1;
    if (selected >= ds[XEEN_DS_PARTY_COUNT]) {
        return 0;
    }
    condition = action_condition(ds + XEEN_PARTY_RECORDS + selected * XEEN_CHARACTER_BYTES);
    if (condition == XEEN_CONDITION_ASLEEP ||
        (condition > XEEN_CONDITION_CONFUSED && condition != XEEN_CONDITION_GOOD)) {
        return 0;
    }
    *ax = (uint16_t)(selected + 1u);
    store_input_code(ds, *ax);
    return 1;
}
/*
 * Checks the host's ANSWER to request Q (of service R) and stores its results: AX and the key word DS F5FE.
 * 1 when accepted, 0 for a member the service refuses (the request stays pending), -1 for an input the request
 * cannot take (nothing stored). By kind:
 * - a yes/no (CONFIRM): an NPC's (3000D mode 0) pages with any key, so any key but none ends it (its last page
 *   takes Y, N and Esc, 3051A, filtered by the host); any other takes Y, N or Esc. AX 1 for Y, else 0; F5FE the key;
 * - a message to dismiss or the treasure dialog: any key but none; AX 0; F5FE the key;
 * - a presentation or a text (303E3..30404's mode 2 and the text-only operation 1 skip the input): no key;
 *   AX 0, F5FE unchanged;
 * - a choice of MODE (2F7C1): the digits 1..MODE; AX the digit below MODE, 0 for MODE itself; F5FE the digit;
 * - a Who-will (CHARACTER): member_answer.
 */
static int text_answer(const xeen_ui_request *q, const xeen_event_request *r,
                       const xeen_ui_response *answer, uint8_t *ds, uint16_t *ax)
{
    uint16_t input = answer->input;
    int npc = r->address == XEEN_REQUEST_NPC_DIALOGUE;
    switch (q->kind) {
        case XUI_CONFIRM:
            if (npc ? !input : input != 'Y' && input != 'N' && input != XEEN_KEY_ESCAPE) {
                return -1;
            }
            *ax = input == 'Y';
            store_input_code(ds, input);
            return 1;
        case XUI_DISMISS:
        case XUI_TREASURE:
            if (!input) {
                return -1;
            }
            *ax = 0;
            store_input_code(ds, input);
            return 1;
        case XUI_PRESENT:
        case XUI_TEXT:
            if (input) {
                return -1;
            }
            *ax = 0;
            return 1;
        case XUI_CHOICE:
            if (input < (unsigned)'1' || input > (unsigned)'0' + (unsigned)q->mode) {
                return -1;
            }
            *ax = (uint16_t)(input - (unsigned)'0' < q->mode ? input - (unsigned)'0' : 0u);
            store_input_code(ds, (uint8_t)(input - (unsigned)'0'));
            return 1;
        case XUI_CHARACTER:
            return member_answer(input, ds, ax);
    }
    *ax = 0; /* not built (text_request makes only the kinds above) */
    return 1;
}
int xeen_event_ui_service_texts(void *user, const xeen_event_request *r, uint8_t *ds, size_t n,
                                uint16_t *ax, const uint8_t *name, const uint8_t *body)
{
    xeen_event_ui *ui = user;
    xeen_ui_request q;
    xeen_ui_response answer = {0};
    int result;
    if (!ui || !r || !ds || n < 65536 || !ax || !name || !body) {
        return -1;
    }
    if ((result = text_request(r, ds, name, body, &q)) != 1) {
        return result;
    }
    if (!ui->present || !ui->poll) {
        return 0;
    }
    if (ui->pending) {
        if (!xeen_ui_same_request(&q, &ui->active)) {
            return -1;
        }
    } else {
        ui->active = q;
        result = ui->present(ui->user, &ui->active);
        if (result != 1) {
            return result == 0 ? 0 : -1;
        }
        ui->pending = 1;
        ++ui->presentations;
    }
    result = ui->poll(ui->user, &answer);
    if (result != 1) {
        return result == 0 ? 0 : -1;
    }
    if ((result = text_answer(&q, r, &answer, ds, ax)) != 1) {
        return result;
    }
    ui->last_ax = *ax;
    ui->last_input = answer.input;
    ui->pending = 0;
    ++ui->completions;
    return 1;
}
