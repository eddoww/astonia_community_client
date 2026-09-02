/*
 * Unit tests for the pointer gesture core (src/gui/gesture.c): the grab
 * state machine and the clamp/snap geometry every draggable surface shares.
 * Pure C - no SDL, no client globals.
 */

#include "test.h"
#include "gui/gesture.h"

TEST(begin_records_the_press)
{
	Gesture g = {0};

	gesture_begin(&g, GESTURE_BUTTON, 42, 100, 200);
	ASSERT_TRUE(gesture_active(&g));
	ASSERT_EQ_INT(GESTURE_BUTTON, g.kind);
	ASSERT_EQ_INT(42, g.but);
	ASSERT_EQ_INT(100, g.press_x);
	ASSERT_EQ_INT(200, g.press_y);
	ASSERT_EQ_INT(100, g.last_x);
	ASSERT_EQ_INT(200, g.last_y);
	ASSERT_FALSE(g.moved);
}

TEST(motion_reports_travel_since_the_previous_motion)
{
	Gesture g = {0};
	int dx, dy;

	gesture_begin(&g, GESTURE_BUTTON, 1, 10, 10);
	gesture_motion(&g, 13, 8, &dx, &dy);
	ASSERT_EQ_INT(3, dx);
	ASSERT_EQ_INT(-2, dy);
	ASSERT_TRUE(g.moved);
	gesture_motion(&g, 13, 8, &dx, &dy); /* no travel: zero delta */
	ASSERT_EQ_INT(0, dx);
	ASSERT_EQ_INT(0, dy);
	gesture_motion(&g, 0, 0, &dx, &dy);
	ASSERT_EQ_INT(-13, dx);
	ASSERT_EQ_INT(-8, dy);
	/* the deltas sum to the total travel: nothing is lost or doubled */
	ASSERT_EQ_INT(-10, g.last_x - g.press_x);
	ASSERT_EQ_INT(-10, g.last_y - g.press_y);
}

TEST(motion_outside_a_gesture_is_inert)
{
	Gesture g = {0};
	int dx = 99, dy = 99;

	gesture_motion(&g, 50, 50, &dx, &dy);
	ASSERT_EQ_INT(0, dx);
	ASSERT_EQ_INT(0, dy);
	ASSERT_FALSE(gesture_active(&g));
}

TEST(end_releases_the_button)
{
	Gesture g = {0};

	gesture_begin(&g, GESTURE_MOD, -1, 5, 5);
	ASSERT_TRUE(gesture_active(&g));
	gesture_end(&g);
	ASSERT_FALSE(gesture_active(&g));
	ASSERT_EQ_INT(-1, g.but);
}

TEST(clamp_leaves_an_inside_move_alone)
{
	int dx = 5, dy = -7;

	gesture_clamp_delta(100, 100, 200, 150, 640, 480, &dx, &dy);
	ASSERT_EQ_INT(5, dx);
	ASSERT_EQ_INT(-7, dy);
}

TEST(clamp_stops_at_the_right_and_bottom_edges)
{
	int dx = 500, dy = 400;

	gesture_clamp_delta(100, 100, 200, 150, 640, 480, &dx, &dy);
	ASSERT_EQ_INT(440, dx); /* 200 + 440 = 640 */
	ASSERT_EQ_INT(330, dy); /* 150 + 330 = 480 */
}

TEST(clamp_stops_at_the_left_and_top_edges)
{
	int dx = -500, dy = -400;

	gesture_clamp_delta(100, 100, 200, 150, 640, 480, &dx, &dy);
	ASSERT_EQ_INT(-100, dx);
	ASSERT_EQ_INT(-100, dy);
}

TEST(clamp_heals_a_stranded_rect)
{
	/* a rect already off the bottom-right (a stale layout from a bigger
	 * screen) is pulled back on canvas by a zero move */
	int dx = 0, dy = 0;

	gesture_clamp_delta(700, 500, 800, 560, 640, 480, &dx, &dy);
	ASSERT_EQ_INT(-160, dx);
	ASSERT_EQ_INT(-80, dy);
}

TEST(clamp_prefers_the_left_top_of_an_oversize_rect)
{
	int dx = 0, dy = 0;

	gesture_clamp_delta(-10, -10, 900, 700, 640, 480, &dx, &dy);
	ASSERT_EQ_INT(10, dx);
	ASSERT_EQ_INT(10, dy);
}

TEST(snap_takes_the_closest_alignment_within_range)
{
	int best = 9, adj = 0;

	/* my span 103..203, target 100..200: the left edges are 3 apart */
	gesture_snap_axis(103, 203, 100, 200, 8, &best, &adj);
	ASSERT_EQ_INT(3, best);
	ASSERT_EQ_INT(-3, adj);
}

TEST(snap_ignores_targets_beyond_the_distance)
{
	int best = 9, adj = 0;

	gesture_snap_axis(120, 220, 100, 200, 8, &best, &adj);
	ASSERT_EQ_INT(9, best);
	ASSERT_EQ_INT(0, adj);
}

TEST(snap_covers_adjacency)
{
	int best = 9, adj = 0;

	/* my left edge 205 sits 5 past the target's right edge 200: sit flush */
	gesture_snap_axis(205, 305, 100, 200, 8, &best, &adj);
	ASSERT_EQ_INT(5, best);
	ASSERT_EQ_INT(-5, adj);

	/* my right edge 96 is 4 short of the target's left edge 100 */
	best = 9;
	adj = 0;
	gesture_snap_axis(0, 96, 100, 200, 8, &best, &adj);
	ASSERT_EQ_INT(4, best);
	ASSERT_EQ_INT(4, adj);
}

TEST(snap_keeps_the_best_match_across_targets)
{
	int best = 9, adj = 0;

	gesture_snap_axis(103, 203, 100, 200, 8, &best, &adj); /* 3 away */
	gesture_snap_axis(103, 203, 0, 640, 8, &best, &adj); /* the screen: far */
	gesture_snap_axis(103, 203, 104, 300, 8, &best, &adj); /* 1 away: wins */
	ASSERT_EQ_INT(1, best);
	ASSERT_EQ_INT(1, adj);
	gesture_snap_axis(103, 203, 102, 300, 8, &best, &adj); /* also 1: the first stays */
	ASSERT_EQ_INT(1, adj);
}

TEST(snap_then_clamp_never_leaves_the_canvas)
{
	/* the drag pipeline is clamp, snap, clamp: a snap toward a target that
	 * hangs off the edge can never push the rect off screen */
	int dx = 0, dy = 0, best = 9, adj = 0;

	gesture_clamp_delta(600, 0, 640, 40, 640, 480, &dx, &dy);
	ASSERT_EQ_INT(0, dx);
	gesture_snap_axis(600, 640, 605, 700, 8, &best, &adj); /* left edges: +5 */
	dx += adj;
	ASSERT_EQ_INT(5, dx);
	gesture_clamp_delta(600, 0, 640, 40, 640, 480, &dx, &dy);
	ASSERT_EQ_INT(0, dx);
}

TEST(absolute_drag_does_not_drift_across_dropped_events)
{
	/* the panel offset is "offset at the press + pointer travel since": the
	 * same final pointer position yields the same offset whether every
	 * motion event arrived or only the last one did */
	Gesture a = {0}, b = {0};
	int dx, dy, start_off = 40;

	gesture_begin(&a, GESTURE_BUTTON, 1, 100, 100);
	gesture_motion(&a, 110, 100, &dx, &dy);
	gesture_motion(&a, 125, 100, &dx, &dy);
	gesture_motion(&a, 160, 100, &dx, &dy);

	gesture_begin(&b, GESTURE_BUTTON, 1, 100, 100);
	gesture_motion(&b, 160, 100, &dx, &dy);

	ASSERT_EQ_INT(start_off + (a.last_x - a.press_x), start_off + (b.last_x - b.press_x));
	ASSERT_EQ_INT(100, start_off + (a.last_x - a.press_x));
}

TEST_MAIN(begin_records_the_press(); motion_reports_travel_since_the_previous_motion(); motion_outside_a_gesture_is_inert();
    end_releases_the_button(); clamp_leaves_an_inside_move_alone(); clamp_stops_at_the_right_and_bottom_edges();
    clamp_stops_at_the_left_and_top_edges(); clamp_heals_a_stranded_rect(); clamp_prefers_the_left_top_of_an_oversize_rect();
    snap_takes_the_closest_alignment_within_range(); snap_ignores_targets_beyond_the_distance(); snap_covers_adjacency();
    snap_keeps_the_best_match_across_targets(); snap_then_clamp_never_leaves_the_canvas();
    absolute_drag_does_not_drift_across_dropped_events();)
