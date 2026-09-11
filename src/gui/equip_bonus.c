/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Worn-equipment bonus table - see equip_bonus.h.
 */

#include <stdlib.h>
#include <string.h>

#include "gui/equip_bonus.h"

static struct equip_bonus_entry tab[EQUIP_BONUS_MAX];
static int tab_n = 0;
static int tab_available = 0;

static int entry_before(const struct equip_bonus_entry *a, const struct equip_bonus_entry *b)
{
	int aa = abs(a->raw), ab = abs(b->raw);

	if (aa != ab) {
		return aa > ab;
	}
	return a->v < b->v;
}

void equip_bonus_sort(struct equip_bonus_entry *e, int n)
{
	/* insertion sort: a summary is a few dozen entries at most */
	for (int i = 1; i < n; i++) {
		struct equip_bonus_entry t = e[i];
		int j = i;

		while (j > 0 && entry_before(&t, &e[j - 1])) {
			e[j] = e[j - 1];
			j--;
		}
		e[j] = t;
	}
}

DLL_EXPORT void equip_bonus_set(int count, const struct equip_bonus_entry *entries)
{
	tab_n = 0;
	for (int i = 0; entries && i < count && tab_n < EQUIP_BONUS_MAX; i++) {
		if (entries[i].v < 0 || entries[i].v >= EQUIP_BONUS_MAX_V || !entries[i].raw) {
			continue;
		}
		tab[tab_n++] = entries[i];
	}
	equip_bonus_sort(tab, tab_n);
	tab_available = 1;
}

DLL_EXPORT void equip_bonus_clear(void)
{
	tab_n = 0;
	tab_available = 0;
}

int equip_bonus_available(void)
{
	return tab_available;
}

int equip_bonus_count(void)
{
	return tab_n;
}

const struct equip_bonus_entry *equip_bonus_get(int i)
{
	if (i < 0 || i >= tab_n) {
		return NULL;
	}
	return &tab[i];
}

int equip_bonus_verdict(const struct equip_bonus_entry *e)
{
	if (!e) {
		return EQUIP_VERDICT_NONE;
	}
	if (e->flags & EQUIP_BONUS_F_NOMAGIC) {
		return EQUIP_VERDICT_SUPPRESSED;
	}
	if (!(e->flags & EQUIP_BONUS_F_HAS_CAP)) {
		return EQUIP_VERDICT_NONE;
	}
	if (e->raw > e->cap) {
		return EQUIP_VERDICT_WASTED;
	}
	if (e->raw == e->cap) {
		return EQUIP_VERDICT_PERFECT;
	}
	return EQUIP_VERDICT_ROOM;
}
