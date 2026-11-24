#ifndef XEEN_BUGFIX_SPELL_DESCRIPTION_H
#define XEEN_BUGFIX_SPELL_DESCRIPTION_H
#include <stddef.h>
#include <stdint.h>
/* Bug fix spell_description (Phase202; owner-approved, NOT original
 * behavior, off by default): corrected texts for the spldesc.bin
 * descriptions that contradict or omit what the spell's code does. The
 * guild's 3F46D copies the description into DS:A43F for Spell Info (735C)
 * and for Buy Spells (7382); the hook below then replaces that copy for a
 * corrected spell. Permanent downsides are red (\f32), Moon Ray's healing
 * green (\f16); \fd restores the window's colour. Evidence and the full
 * list: docs/phases/PHASE202_SPELL_DESCRIPTION_BUGFIX.md. */
extern int xeen_bugfix_spell_description_on;
/* Replace the NUL-terminated description in buf (cap bytes) for spell when
 * a correction exists; otherwise buf is left as it is. */
void xeen_bugfix_spell_description_apply(uint8_t *buf,size_t cap,unsigned spell);
#define XEEN_BUGFIX_SPELL_DESCRIPTION(buf,cap,spell) \
    do{ if(xeen_bugfix_spell_description_on)xeen_bugfix_spell_description_apply((buf),(cap),(spell)); }while(0)
#endif
