/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Shared design-token set for the client UI (mirrored in the mod).
 * Colors are RGB555 via IRGB(); include game/game.h before using these.
 */

#ifndef UI_TOKENS_H
#define UI_TOKENS_H

/* palette */
#define UI_BG_BASE       IRGB(3, 3, 3)
#define UI_BG_RAISED     IRGB(7, 7, 7)
#define UI_BG_SUNKEN     IRGB(4, 4, 4)
#define UI_BG_ROW_HOVER  IRGB(12, 12, 12)
#define UI_BG_ROW_ACTIVE IRGB(10, 10, 10)
#define UI_BG_INACTIVE   IRGB(5, 5, 5)
#define UI_BORDER        IRGB(14, 14, 14)
#define UI_BORDER_STRONG IRGB(20, 20, 20)
#define UI_ACCENT        IRGB(28, 22, 10)
#define UI_ACCENT_DIM    IRGB(18, 14, 6)
#define UI_TEXT          IRGB(29, 28, 26)
#define UI_TEXT_TITLE    IRGB(28, 26, 22)
#define UI_TEXT_LABEL    IRGB(22, 21, 19)
#define UI_TEXT_MUTED    IRGB(16, 15, 14)
#define UI_TEXT_DISABLED IRGB(10, 10, 10)
#define UI_TEXT_GOLD     IRGB(28, 24, 8)
#define UI_TEXT_ERROR    IRGB(28, 8, 8)
#define UI_TEXT_SUCCESS  IRGB(8, 24, 8)
#define UI_TEXT_LINK     IRGB(10, 20, 24)
/* alphas */
#define UI_A_PANEL       235
#define UI_A_PANEL_GRAD  200
#define UI_A_OVERLAY     160
#define UI_A_CONTROL     230
#define UI_A_RULE        120
#define UI_A_ROW_HOVER   110
#define UI_A_BORDER_REST 160
#define UI_A_BORDER_HOV  220
#define UI_A_TOOLTIP     250
/* geometry */
#define UI_PAD         8
#define UI_PAD_TIGHT   4
#define UI_ROW_H       18
#define UI_ROW_H_DENSE 14
#define UI_TITLE_H     26
#define UI_TAB_H       22
#define UI_BUTTON_H    24
#define UI_SCROLLBAR_W 12
/* radii */
#define UI_R_PANEL  6
#define UI_R_BUTTON 4
#define UI_R_ROW    3
#define UI_R_CHIP   2
/* typography — never pass bare 0 as text flags (it silently means LARGE) */
#define UI_FONT_BODY   (RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED)
#define UI_FONT_TITLE  (RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED)
#define UI_FONT_CENTER (UI_FONT_BODY | RENDER_ALIGN_CENTER)
#define UI_FONT_RIGHT  (UI_FONT_BODY | RENDER_TEXT_RIGHT)

#endif
