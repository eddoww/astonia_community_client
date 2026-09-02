/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Pointer gestures - see gesture.h.
 */

#include "gui/gesture.h"

void gesture_begin(Gesture *g, int kind, int but, int x, int y)
{
	g->kind = kind;
	g->but = but;
	g->press_x = g->last_x = x;
	g->press_y = g->last_y = y;
	g->moved = 0;
}

void gesture_motion(Gesture *g, int x, int y, int *dx, int *dy)
{
	if (g->kind == GESTURE_NONE) {
		*dx = *dy = 0;
		return;
	}
	*dx = x - g->last_x;
	*dy = y - g->last_y;
	g->last_x = x;
	g->last_y = y;
	if (*dx || *dy) {
		g->moved = 1;
	}
}

void gesture_end(Gesture *g)
{
	g->kind = GESTURE_NONE;
	g->but = -1;
}

int gesture_active(const Gesture *g)
{
	return g->kind != GESTURE_NONE;
}

void gesture_clamp_delta(int x1, int y1, int x2, int y2, int cw, int ch, int *dx, int *dy)
{
	if (x2 + *dx > cw) {
		*dx -= x2 + *dx - cw;
	}
	if (y2 + *dy > ch) {
		*dy -= y2 + *dy - ch;
	}
	if (x1 + *dx < 0) {
		*dx -= x1 + *dx;
	}
	if (y1 + *dy < 0) {
		*dy -= y1 + *dy;
	}
}

void gesture_snap_axis(int a1, int a2, int t1, int t2, int dist, int *best, int *adj)
{
	int cand[4] = {t1 - a1, t2 - a1, t1 - a2, t2 - a2};

	for (int i = 0; i < 4; i++) {
		int d = cand[i] < 0 ? -cand[i] : cand[i];

		if (d <= dist && d < *best) {
			*best = d;
			*adj = cand[i];
		}
	}
}
