/*
 * Phase 288: the native-stop diagnostics (stop_diagnostics.h). The rows and the category sentences are generated
 * (stop_catalog.inc; tools/xeen_replay/stop_catalog.py --check keeps them current).
 */
#include "stop_diagnostics.h"
#include <stdio.h>
#include <string.h>

static const xeen_stop_entry entries[] = {
#define XEEN_DIAG_CATALOG_ROWS
#include "stop_catalog.inc"
#undef XEEN_DIAG_CATALOG_ROWS
};
static const char *const templates[XEEN_DIAG_CATEGORY_COUNT] = {
#define XEEN_DIAG_CATALOG_TEMPLATES
#include "stop_catalog.inc"
#undef XEEN_DIAG_CATALOG_TEMPLATES
};
static const char *const category_names[XEEN_DIAG_CATEGORY_COUNT] = {
    "unclassified", "resource", "data", "guard", "memory", "unreconstructed", "presentation", "audio", "save", "host",
    "unknown"};
static const char *const basis_names[] = {"none", "reviewed", "rule", "comment", "ambiguous"};

const xeen_stop_entry *xeen_stop_entries(size_t *count)
{
    if (count) {
        *count = sizeof entries / sizeof entries[0];
    }
    return entries;
}
const xeen_stop_entry *xeen_stop_find(uint32_t address)
{
    size_t lo = 0, hi = sizeof entries / sizeof entries[0];
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (entries[mid].address == address) {
            return &entries[mid];
        }
        if (entries[mid].address < address) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return NULL;
}
const char *xeen_stop_category_name(int category)
{
    return category >= 0 && category < XEEN_DIAG_CATEGORY_COUNT ? category_names[category]
                                                                 : category_names[XEEN_DIAG_CATEGORY_UNKNOWN];
}
const char *xeen_stop_category_text(int category)
{
    return category >= 0 && category < XEEN_DIAG_CATEGORY_COUNT ? templates[category]
                                                                 : templates[XEEN_DIAG_CATEGORY_UNKNOWN];
}
const char *xeen_stop_basis_name(int basis)
{
    return basis >= 0 && basis < (int)(sizeof basis_names / sizeof basis_names[0]) ? basis_names[basis] : "none";
}
/* Copies TEXT into OUT with every control character (C0 and DEL) replaced by '?': the report stays one line wherever
 * it is shown. Other bytes (a UTF-8 path) are kept as they are. */
static void sanitize(char *out, size_t size, const char *text)
{
    size_t k = 0;
    if (!size) {
        return;
    }
    for (; text && *text && k + 1u < size; ++text) {
        unsigned char c = (unsigned char)*text;
        out[k++] = c < 0x20u || c == 0x7fu ? '?' : (char)c;
    }
    out[k] = 0;
}
void xeen_stop_diagnose(const xeen_stop_report *r, xeen_stop_diagnostic *out)
{
    const xeen_stop_entry *e;
    char member[96], archive[320];
    xeen_stop_category cat;
    out->message[0] = out->details[0] = 0;
    /* Only a deliberate stop takes its address's meaning: a wait or continuation that failed at an address that is
     * also a stop site did not take that stop. */
    e = r->stopped ? xeen_stop_find(r->address) : NULL;
    cat = e ? (xeen_stop_category)e->category : XEEN_DIAG_CATEGORY_UNKNOWN;
    out->entry = e;
    out->category = cat;
    if (r->member && r->archive) {
        sanitize(member, sizeof member, r->member);
        sanitize(archive, sizeof archive, r->archive);
        snprintf(out->message, sizeof out->message, "Cannot read %s from %s (possible floppy/CD-ROM version mismatch)",
                 member, archive);
    } else {
        snprintf(out->message, sizeof out->message, "%s", e && e->message ? e->message : xeen_stop_category_text(cat));
    }
    if (e) {
        snprintf(out->details, sizeof out->details,
                 "Technical details: native stop at %05x status=%d (next=%05x) stop=%s category=%s basis=%s%s%s",
                 (unsigned)r->address, r->status, (unsigned)r->next, *e->names ? e->names : "(literal)",
                 xeen_stop_category_name(e->category), xeen_stop_basis_name(e->basis), *e->by ? ":" : "", e->by);
    } else {
        const xeen_stop_entry *site = xeen_stop_find(r->address);
        snprintf(out->details, sizeof out->details,
                 "Technical details: native stop at %05x status=%d (next=%05x) stop=%s category=unknown%s%s",
                 (unsigned)r->address, r->status, (unsigned)r->next, r->stopped ? "unregistered" : "none (not stopped)",
                 site ? " registered-site=" : "", site ? (*site->names ? site->names : "(literal)") : "");
    }
}
