/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * GUI panel system
 *
 * The classic bottom window is split into independent panels (skills, chat,
 * inventory, speed, buffs, hotbar) plus the equipment paper doll. Each panel
 * can be hidden and moved; the bigger ones are framed windows with a title
 * bar, a close button, a minimize button and - where a size setting exists -
 * a resize grip. Layout is stored as a per-panel offset from the default
 * position so it survives init_dots() re-runs (resolution / option changes)
 * and persists in the keybind config. A master overlay toggle hides the whole
 * GUI chrome for an unobstructed view of the game world; interaction windows
 * (teleporter, context menu, help, ...) stay usable.
 */

#ifndef PANELS_H
#define PANELS_H

#include "../dll.h"

enum {
	PANEL_SKILLS, /* skill list + scrollbar                      */
	PANEL_CHAT, /* chat text + input line                      */
	PANEL_INVENTORY, /* inventory grid + scrollbar + purse + trash  */
	PANEL_SPEED, /* stealth/normal/fast walk mode               */
	PANEL_BUFFS, /* self-spell timers + rage meter              */
	PANEL_HOTBAR, /* hotbar rows                                 */
	PANEL_EQUIPMENT, /* worn-equipment paper doll + gear lock       */
	PANEL_SPELLBOOK, /* castable spells, drag them onto the hotbar  */
	PANEL_STATUS, /* level + military progress bars              */
	PANEL_SYSMENU, /* Menu / Help / Quests buttons                */
	PANEL_CLOCK, /* classic flip-digit game clock               */
	PANEL_HELP, /* help / quest-log window (summoned)          */
	PANEL_MINIMAP, /* round minimap (frameless, drag anywhere)    */
	PANEL_CONTAINER, /* shop / grave / depot grid (summoned by the  */
	/* server, lives beside the inventory)         */
	PANEL_LOOK, /* "look at" character window (summoned by the   */
	/* server's look reply, closes with its X)     */
	MAX_PANEL
};

/* slim grab strip above a HUD plate - the only draggable part of it, so
 * clicks still reach the controls inside */
#define HUD_GRIP_H 9

/* frame_kind(): how much chrome a panel draws around its content */
#define PANEL_FRAME_NONE   0 /* none - the panel draws its own look entirely */
#define PANEL_FRAME_HUD    1 /* translucent plate with a slim drag strip     */
#define PANEL_FRAME_WINDOW 2 /* title bar + close + minimize (+ resize grip) */

/* master GUI overlay toggle - session only, always starts visible */
DLL_EXPORT extern int gui_overlay_visible;

/* raw per-panel visibility toggle */
DLL_EXPORT int panel_visible(int p);
DLL_EXPORT void panel_set_visible(int p, int on);
DLL_EXPORT void panel_toggle(int p);

/* effective visibility: overlay + toggle + auto-show (an open container
 * forces the skills panel, an active chat line forces the chat panel) */
DLL_EXPORT int panel_shown(int p);

/* minimized to its title bar: the frame is still there, the content is not.
 * panel_set_collapsed() honours the collapse direction below: in "upward"
 * mode the title bar lands where the window's bottom edge was, and restoring
 * grows the window back up. */
DLL_EXPORT int panel_collapsed(int p);
DLL_EXPORT void panel_set_collapsed(int p, int on);

/* Options > UI "Minimize Upward": 0 = a minimized window keeps its title bar
 * where it is (collapses downward), 1 = the title bar drops to the window's
 * bottom edge (collapses upward). Mod windows follow the same setting. */
DLL_EXPORT int panel_collapse_upward(void);
DLL_EXPORT void panels_set_collapse_upward(int on);

/* shown and not collapsed - the guard for drawing a panel's content */
DLL_EXPORT int panel_content_shown(int p);

/* position/size locks: the global Options toggle freezes everything, the
 * per-panel padlock (titlebar glyph) freezes one window */
DLL_EXPORT int panels_layout_locked(void);
DLL_EXPORT void panels_set_layout_locked(int on);
DLL_EXPORT int panel_locked(int p);
DLL_EXPORT void panel_set_locked(int p, int on);
DLL_EXPORT void panel_toggle_locked(int p);

/* persistent drag offset from the default layout position */
DLL_EXPORT int panel_dx(int p);
DLL_EXPORT int panel_dy(int p);

const char *panel_id(int p); /* config key, e.g. "skills" */
const char *panel_name(int p); /* display name, e.g. "Skills" */
const char *panel_title(int p); /* live window title (container name, ...) */
int panel_frame_kind(int p);
int panel_resizable(int p);

/* content rectangle, published by init_dots() and moved with the panel */
void panel_set_content_rect(int p, int x1, int y1, int x2, int y2);
int panel_content_rect(int p, int *x1, int *y1, int *x2, int *y2);

/* outer frame rectangle (content plus chrome); 0 when the panel is
 * unframed. Honors the collapsed state. */
int panel_frame_rect(int p, int *x1, int *y1, int *x2, int *y2);

/* mod-facing: panel count + the shown footprint (for cross-family snapping) */
DLL_EXPORT int panel_count(void);
DLL_EXPORT int panel_snap_rect(int p, int *x1, int *y1, int *x2, int *y2);

/* Mirror the derived chrome geometry into but[]: init_dots() hands in its
 * set_but() so the shared capture/click machinery can drive the title bars,
 * glyph buttons and resize grips. Call it after the content rects are
 * published and before panels_apply_offsets(). */
void panels_place_chrome_buttons(void (*place)(int bidx, int x, int y, int hitrad, int flags));

/* shift the panel's dots/buttons/content rect by the stored offsets; call at
 * the very end of init_dots() (after every set_dot/set_but) */
void panels_apply_offsets(void);

/* Drag-handle gesture, in absolute UI-space pointer positions: begin at the
 * press, update on every motion, end on the release (persists the moved
 * layout) - or cancel first to put the panel back where it was. The panel's
 * offset is always "offset at the press + pointer travel since", so nothing
 * accumulates and nothing can drift. */
void panels_drag_begin(int p, int mx, int my);
void panels_drag_update(int p, int mx, int my);
void panels_drag_cancel(void);
void panels_drag_end(void);
/* 1 when the live drag actually moved its panel (past the click dead zone) -
 * a press-and-release that never moved is a click on the handle */
int panels_drag_moved(void);

/* Resize-grip gesture: the grip follows the pointer 1:1 and the panel's size
 * setting follows (inventory / container columns and rows, skill list rows);
 * the layout is rebuilt on every change with the corner the grip is NOT
 * dragging pinned in place. update returns 1 when the layout was rebuilt. */
int panels_resize_begin(int p, int mx, int my);
int panels_resize_update(int p, int mx, int my);
void panels_resize_cancel(void);
void panels_resize_end(void);

/* shift a panel so its content rect's top-left returns to (x1,y1) - used to
 * pin the corner a resize is NOT dragging */
void panel_keep_anchor(int p, int x1, int y1);

/* the container window is summoned by an open shop/grave/depot: closing it
 * dismisses the view for this container only - the next one opens it again */
void panel_dismiss_container(void);
void panel_container_opened(void);

/* a mod chat window has taken over chat: hide the classic chat panel (it
 * still appears while the classic input line is active, so nothing is ever
 * typed blind) */
DLL_EXPORT void panel_chat_external(int on);
DLL_EXPORT int panel_chat_is_external(void);

/* which panel owns button b, or -1 (chrome buttons included) */
int panel_owns_button(int b);
/* 0 when b belongs to a hidden panel, or to the content of a collapsed one */
int panel_button_live(int b);

/* window chrome: draw the frames (before the panel contents), hit-test the
 * title bars and glyph buttons, and tell whether the pointer is over a
 * framed panel at all (so the world below is not targeted) */
void panels_display_frames(void);
int panels_frame_button(int x, int y);
int panels_frame_over(int x, int y);

/* subtle drag-handle indicator near the mouse for unframed panels; call
 * late in display() */
void panels_display_handles(void);

/* forget all offsets, visibility and collapse toggles (does not touch the
 * fullscreen-world setting); caller re-runs init_dots() + saves */
void panels_reset_layout(void);

/* fullscreen world view: map spans the whole canvas, GUI overlays it */
DLL_EXPORT int panels_fullscreen_world(void);
void panels_set_fullscreen_world(int on);

/* persistence hooks for the keybind config (cJSON tree) */
struct cJSON;
void panels_save_json(struct cJSON *root);
void panels_load_json(const struct cJSON *root);

#endif
