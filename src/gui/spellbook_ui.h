/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Spellbook UI — toggleable panel that shows available spells as
 * draggable icons. Left-click a spell to "pick it up", then click
 * a hotbar slot to assign it.
 */

#ifndef SPELLBOOK_UI_H
#define SPELLBOOK_UI_H

/* rendering — call from gui_display after hotbar_display */
void spellbook_display(void);

/* toggle the spellbook panel open/closed */
void spellbook_toggle(void);
int spellbook_is_open(void);

/* click handlers — return 1 if consumed */
int spellbook_click(int mx, int my);
int spellbook_rclick(int mx, int my);

/* is the player currently dragging a spell from the spellbook? */
int spellbook_is_dragging(void);

/* the action slot index being dragged, or -1 */
int spellbook_dragging_slot(void);

/* cancel any in-progress drag */
void spellbook_cancel_drag(void);

#endif /* SPELLBOOK_UI_H */
