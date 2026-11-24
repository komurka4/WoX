#ifndef XEEN_ADDON_SLOT_FILE_H
#define XEEN_ADDON_SLOT_FILE_H
#include <stddef.h>
#include <stdint.h>
/* Owner-approved add-ons (NOT original behavior): the save slot's add-on file
 * ADDONnn.WOX, shared by the add-ons that keep something per slot (Phase 211;
 * before, identify's alone).
 *
 * Layout: the base block, then zero or more sections.
 *   base     "WOXADDON", version 1 (LE u32), count (LE u32), count keys
 *            (LE u32): identify's known item types (xeen/addon/identify.h),
 *            count 0 when identify never wrote any. This is the file
 *            identify wrote before Phase 211, byte for byte.
 *   section  tag (4 bytes), size (LE u32), size payload bytes.
 * Sections: "SLST" spell_list_sort (xeen/addon/spell_list_sort.h).
 * A reader of the base alone (identify, any build) ignores the sections, so
 * the files of every build read the same; nothing is converted on load.
 *
 * Writing keeps what belongs to an add-on that is off: an off add-on's base
 * or section is copied from the slot's previous file unchanged. */

/* The slot file's name ("ADDON%02d.WOX"; before Phase 211 in identify). */
void xeen_addons_slot_name(int slot,char out[16]);
/* The section `tag` (after the base); 0 when absent or malformed. */
int xeen_addon_file_section(const uint8_t *file,size_t n,const char tag[4],const uint8_t **data,size_t *size);
/* A new file: `base` (NULL: the old file's base, or an empty one), then the
 * old file's sections except `tag`, then `tag` with payload (none when
 * tag is NULL or payload_size is 0). old may be NULL (no previous file or
 * a malformed one: nothing kept). NULL on allocation failure. */
uint8_t *xeen_addon_file_compose(const uint8_t *old,size_t old_size,const uint8_t *base,size_t base_size,
    const char *tag,const uint8_t *payload,size_t payload_size,size_t *size);
#endif
