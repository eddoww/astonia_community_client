/*
 * Unit tests for the worn-equipment bonus table (src/gui/equip_bonus.c):
 * the mod-facing setter, display ordering and the colour verdict.
 * Pure C - no SDL, no client globals.
 */

#include "test.h"
#include "gui/equip_bonus.h"

/* the value indices the tests name (client/client.h's V_ table) */
enum { V_HP = 0, V_ENDURANCE = 1, V_MANA = 2, V_WIS = 3, V_INT = 4, V_AGI = 5, V_STR = 6, V_ARMOR = 7, V_LIGHT = 9,
	V_SWORD = 15, V_MAX = EQUIP_BONUS_MAX_V };

static struct equip_bonus_entry mk(int v, int raw, int eff, int cap, unsigned int flags)
{
	struct equip_bonus_entry e;

	e.v = v;
	e.raw = raw;
	e.eff = eff;
	e.cap = cap;
	e.flags = flags;
	return e;
}

TEST(empty_until_set_then_available_even_when_empty)
{
	equip_bonus_clear();
	ASSERT_FALSE(equip_bonus_available());
	ASSERT_EQ_INT(0, equip_bonus_count());
	ASSERT_TRUE(equip_bonus_get(0) == NULL);

	equip_bonus_set(0, NULL);
	ASSERT_TRUE(equip_bonus_available());
	ASSERT_EQ_INT(0, equip_bonus_count());

	equip_bonus_clear();
	ASSERT_FALSE(equip_bonus_available());
}

TEST(set_sorts_by_absolute_raw_then_value_index)
{
	struct equip_bonus_entry in[5];

	in[0] = mk(V_WIS, 5, 5, 10, EQUIP_BONUS_F_HAS_CAP | EQUIP_BONUS_F_ATTRIBUTE);
	in[1] = mk(V_ARMOR, 30, 30, 0, 0);
	in[2] = mk(V_STR, -5, -5, 50, EQUIP_BONUS_F_HAS_CAP | EQUIP_BONUS_F_ATTRIBUTE);
	in[3] = mk(V_SWORD, 7, 7, 10, EQUIP_BONUS_F_HAS_CAP);
	in[4] = mk(V_LIGHT, 1, 1, 0, 0);
	equip_bonus_set(5, in);
	ASSERT_EQ_INT(5, equip_bonus_count());
	ASSERT_EQ_INT(V_ARMOR, equip_bonus_get(0)->v);
	ASSERT_EQ_INT(V_SWORD, equip_bonus_get(1)->v);
	/* |5| ties: the lower value index first */
	ASSERT_EQ_INT(V_WIS, equip_bonus_get(2)->v);
	ASSERT_EQ_INT(V_STR, equip_bonus_get(3)->v);
	ASSERT_EQ_INT(V_LIGHT, equip_bonus_get(4)->v);
	ASSERT_TRUE(equip_bonus_get(5) == NULL);
	/* the entries are copied verbatim */
	ASSERT_EQ_INT(30, equip_bonus_get(0)->raw);
	ASSERT_EQ_INT(10, equip_bonus_get(1)->cap);
	ASSERT_EQ_INT(EQUIP_BONUS_F_HAS_CAP, (int)equip_bonus_get(1)->flags);
}

TEST(set_drops_zero_raw_and_out_of_range_indices)
{
	struct equip_bonus_entry in[4];

	in[0] = mk(V_STR, 0, 0, 50, EQUIP_BONUS_F_HAS_CAP);
	in[1] = mk(-1, 5, 5, 0, 0);
	in[2] = mk(V_MAX, 5, 5, 0, 0);
	in[3] = mk(V_AGI, 3, 3, 50, EQUIP_BONUS_F_HAS_CAP);
	equip_bonus_set(4, in);
	ASSERT_EQ_INT(1, equip_bonus_count());
	ASSERT_EQ_INT(V_AGI, equip_bonus_get(0)->v);
}

TEST(set_replaces_the_previous_table)
{
	struct equip_bonus_entry a = mk(V_STR, 10, 10, 50, EQUIP_BONUS_F_HAS_CAP);
	struct equip_bonus_entry b = mk(V_INT, 4, 4, 50, EQUIP_BONUS_F_HAS_CAP);

	equip_bonus_set(1, &a);
	equip_bonus_set(1, &b);
	ASSERT_EQ_INT(1, equip_bonus_count());
	ASSERT_EQ_INT(V_INT, equip_bonus_get(0)->v);
}

TEST(set_caps_at_the_table_size)
{
	struct equip_bonus_entry in[EQUIP_BONUS_MAX + 8];

	for (int i = 0; i < EQUIP_BONUS_MAX + 8; i++) {
		in[i] = mk(i % V_MAX, i + 1, i + 1, 0, 0);
	}
	equip_bonus_set(EQUIP_BONUS_MAX + 8, in);
	ASSERT_EQ_INT(EQUIP_BONUS_MAX, equip_bonus_count());
	equip_bonus_clear();
}

TEST(verdict_follows_raw_against_cap)
{
	struct equip_bonus_entry e;

	e = mk(V_STR, 40, 40, 50, EQUIP_BONUS_F_HAS_CAP);
	ASSERT_EQ_INT(EQUIP_VERDICT_ROOM, equip_bonus_verdict(&e));
	e = mk(V_STR, 50, 50, 50, EQUIP_BONUS_F_HAS_CAP);
	ASSERT_EQ_INT(EQUIP_VERDICT_PERFECT, equip_bonus_verdict(&e));
	e = mk(V_STR, 60, 50, 50, EQUIP_BONUS_F_HAS_CAP);
	ASSERT_EQ_INT(EQUIP_VERDICT_WASTED, equip_bonus_verdict(&e));
	/* unlearned skill: cap 0, anything is wasted */
	e = mk(V_SWORD, 7, 0, 0, EQUIP_BONUS_F_HAS_CAP | EQUIP_BONUS_F_UNLEARNED);
	ASSERT_EQ_INT(EQUIP_VERDICT_WASTED, equip_bonus_verdict(&e));
	/* no cap: no verdict */
	e = mk(V_ARMOR, 30, 30, 0, 0);
	ASSERT_EQ_INT(EQUIP_VERDICT_NONE, equip_bonus_verdict(&e));
	/* a no-magic zone overrides the cap verdict */
	e = mk(V_STR, 60, 0, 50, EQUIP_BONUS_F_HAS_CAP | EQUIP_BONUS_F_NOMAGIC);
	ASSERT_EQ_INT(EQUIP_VERDICT_SUPPRESSED, equip_bonus_verdict(&e));
	ASSERT_EQ_INT(EQUIP_VERDICT_NONE, equip_bonus_verdict(NULL));
}

TEST(sort_helper_orders_any_array)
{
	struct equip_bonus_entry arr[3];

	arr[0] = mk(V_HP, 2, 2, 0, 0);
	arr[1] = mk(V_MANA, -9, -9, 0, 0);
	arr[2] = mk(V_ENDURANCE, 2, 2, 0, 0);
	equip_bonus_sort(arr, 3);
	ASSERT_EQ_INT(V_MANA, arr[0].v);
	ASSERT_EQ_INT(V_HP, arr[1].v);
	ASSERT_EQ_INT(V_ENDURANCE, arr[2].v);
	equip_bonus_sort(arr, 0);
	equip_bonus_sort(arr, 1);
	ASSERT_EQ_INT(V_MANA, arr[0].v);
}

TEST_MAIN(
	empty_until_set_then_available_even_when_empty();
	set_sorts_by_absolute_raw_then_value_index();
	set_drops_zero_raw_and_out_of_range_indices();
	set_replaces_the_previous_table();
	set_caps_at_the_table_size();
	verdict_follows_raw_against_cap();
	sort_helper_orders_any_array();
)
