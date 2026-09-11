/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Pointer gestures
 *
 * One pointer gesture at a time. Whoever takes the left-button press owns
 * every motion event and the matching release, wherever the pointer goes in
 * between - a client button (drag handle, resize grip, scroll thumb, purse)
 * or a mod surface that consumed the press. Positions are absolute UI-space
 * coordinates: no pointer warping, no relative motion, no hidden cursor, so
 * a gesture can never drift or run away, and it always ends on the release.
 *
 * This file has no dependencies on the rest of the client so the state
 * machine and the geometry helpers can be unit-tested on their own
 * (tests/test_gesture.c).
 */

#ifndef GESTURE_H
#define GESTURE_H

#define GESTURE_NONE   0
#define GESTURE_BUTTON 1 /* a but[] entry flagged BUTF_CAPTURE */
#define GESTURE_MOD    2 /* a mod consumed the press           */

typedef struct gesture {
	int kind; /* GESTURE_*                             */
	int but; /* GESTURE_BUTTON: the captured button id */
	int press_x, press_y; /* pointer at the press                  */
	int last_x, last_y; /* pointer at the latest motion          */
	int moved; /* the pointer moved since the press     */
} Gesture;

void gesture_begin(Gesture *g, int kind, int but, int x, int y);
/* record a motion; dx and dy receive the travel since the previous motion */
void gesture_motion(Gesture *g, int x, int y, int *dx, int *dy);
void gesture_end(Gesture *g);
int gesture_active(const Gesture *g);

/* ── geometry shared by every draggable surface ─────────────────────────── */

/* Keep the rectangle (x1,y1)-(x2,y2), about to move by (*dx,*dy), inside the
 * canvas (0,0)-(cw,ch) by trimming the delta. The left/top edges win for a
 * rectangle larger than the canvas - that is where its handles live. */
void gesture_clamp_delta(int x1, int y1, int x2, int y2, int cw, int ch, int *dx, int *dy);

/* Magnetize one axis of the span a1..a2 to the span t1..t2: aligned edges
 * (a1=t1, a2=t2) and adjacency (a1=t2, a2=t1). Keeps the closest candidate
 * within dist in *best (its distance) and *adj (the shift that reaches it).
 * Initialize *best to dist+1 and *adj to 0 before the first call, then feed
 * every target span; the closest match across all of them survives. */
void gesture_snap_axis(int a1, int a2, int t1, int t2, int dist, int *best, int *adj);

#endif
