/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Worn-equipment bonus table behind the Equipment window's "Bonuses" column.
 *
 * The server knows how much of an item bonus actually reaches a character
 * (its item-modifier cap is a server rule), the client only sees totals. A
 * server mod hands the per-value summary in through equip_bonus_set(): for
 * every value the worn gear touches, the raw gear bonus, the effective part
 * and the cap. The window colours each row from that (red = part of the
 * bonus is wasted, green = exactly at the cap, yellow = room left, white =
 * no cap applies). Without a table the window falls back to what the client
 * can derive alone (display.c).
 *
 * Pure C: no client globals, no SDL - unit-tested in tests/test_equip_bonus.c.
 */

#ifndef EQUIP_BONUS_H
#define EQUIP_BONUS_H

#include "dll.h"

/* value index space: == V_MAX (client/client.h), asserted in display.c so
 * this header stays free of client includes for the unit test */
#define EQUIP_BONUS_MAX_V 200

/* one entry per value index at most */
#define EQUIP_BONUS_MAX EQUIP_BONUS_MAX_V

/* Entry flags - the wire's MOD_EQUIP_F_* (Ugaris_Protocol mod_equip.h).
 * MIRRORED in src/amod/amod_structs.h for mods: keep both in step. */
#define EQUIP_BONUS_F_HAS_CAP   0x01 /* cap is meaningful: raw > cap is wasted */
#define EQUIP_BONUS_F_ATTRIBUTE 0x02 /* v is an attribute or power (V_HP..V_STR) */
#define EQUIP_BONUS_F_NOMAGIC   0x04 /* no-magic tile: the bonus is suppressed */
#define EQUIP_BONUS_F_UNLEARNED 0x08 /* skill base is 0: the bonus has no effect */
#define EQUIP_BONUS_F_BEYOND    0x10 /* raw includes an uncapped artifact share */
#define EQUIP_BONUS_F_NOEFFECT  0x20 /* v ignores item modifiers (V_DEMON) */

/* One summarized value. MIRRORED in src/amod/amod_structs.h (mod ABI). */
struct equip_bonus_entry {
	int v; /* value index, client space (0..V_MAX-1) */
	int raw; /* sum of the worn items' modifiers */
	int eff; /* part of raw that reaches the character */
	int cap; /* most that could (with EQUIP_BONUS_F_HAS_CAP) */
	unsigned int flags; /* EQUIP_BONUS_F_* */
};

enum equip_bonus_verdict {
	EQUIP_VERDICT_NONE = 0, /* no cap applies: white               */
	EQUIP_VERDICT_WASTED, /* raw > cap: red                      */
	EQUIP_VERDICT_PERFECT, /* raw == cap: green                   */
	EQUIP_VERDICT_ROOM, /* raw < cap: yellow                   */
	EQUIP_VERDICT_SUPPRESSED, /* no-magic zone: nothing lands, muted */
};

/* Mod-facing: replace the table (entries are copied and sorted; raw == 0
 * and out-of-range indices are dropped) or drop it altogether. */
DLL_EXPORT void equip_bonus_set(int count, const struct equip_bonus_entry *entries);
DLL_EXPORT void equip_bonus_clear(void);

/* a table has been handed in since the last clear - even an empty one */
int equip_bonus_available(void);
int equip_bonus_count(void);

/* i-th entry in display order: |raw| descending, then value index */
const struct equip_bonus_entry *equip_bonus_get(int i);

/* the colour rule, from the entry alone */
int equip_bonus_verdict(const struct equip_bonus_entry *e);

/* display order for any entry array (the fallback list uses it too) */
void equip_bonus_sort(struct equip_bonus_entry *e, int n);

#endif
