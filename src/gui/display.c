/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Display Windows
 *
 * Equipment, inventory, text, ... windows.
 */

#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "astonia.h"
#include "gui/gui.h"
#include "gui/gui_private.h"
#include "gui/input_bind.h"
#include "gui/panels.h"
#include "gui/ui_draw.h"
#include "game/game.h"
#include "client/client.h"
#include "modder/modder.h"

char tutor_text[1024] = {""};
int show_tutor = 0;

int __textdisplay_sy;

static void dx_drawtext_gold(int x, int y, unsigned short int color, int amount)
{
	if (amount > 99) {
		render_text_fmt(x, y, color, RENDER_ALIGN_CENTER | RENDER_TEXT_FRAMED | RENDER_TEXT_SMALL, "%d.%02dG",
		    amount / 100, amount % 100);
	} else {
		render_text_fmt(x, y, color, RENDER_ALIGN_CENTER | RENDER_TEXT_FRAMED | RENDER_TEXT_SMALL, "%ds", amount);
	}
}

int gear_lock = 0;

void display_wear_lock(void)
{
	gear_lock = 1 - gear_lock;
	save_options();
}

/* Which slots the carried/hovered item could be worn in, so the paper doll
 * can highlight them. Mirrors the IF_WN* flag order of weaname[]. */
static const unsigned int wea_slot_flag[12] = {IF_WNRRING, IF_WNRHAND, IF_WNLHAND, IF_WNLRING, IF_WNNECK, IF_WNHEAD,
    IF_WNCLOAK, IF_WNBODY, IF_WNBELT, IF_WNARMS, IF_WNLEGS, IF_WNFEET};

void display_wear(void)
{
	int b;
	unsigned int sprite;
	unsigned short c1, c2, c3, shine;
	unsigned char scale, cr, cg, cb, light, sat;
	RenderFX fx;
	int cx1, cy1, cx2, cy2;

	for (b = BUT_WEA_BEG; b <= BUT_WEA_END; b++) {
		int i = b - BUT_WEA_BEG;
		int x = butx(b);
		int y = buty(b);
		int yt = y + 13;
		unsigned short namecol = UI_TEXT_MUTED;
		int named = 0;

		if (but[b].flags & BUTF_NOHIT) {
			continue; /* slot has no cell in the doll */
		}

		render_sprite(opt_sprite(SPR_ITPAD), x, y, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_CENTER);
		if (i == weasel) {
			render_sprite(opt_sprite(SPR_ITSEL), x, y, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_CENTER);
		}
		if (item[weatab[i]]) {
			bzero(&fx, sizeof(fx));

			sprite =
			    trans_asprite(0, item[weatab[i]], tick, &scale, &cr, &cg, &cb, &light, &sat, &c1, &c2, &c3, &shine);
			fx.sprite = sprite;
			fx.c1 = c1;
			fx.c2 = c2;
			fx.c3 = c3;
			fx.cr = (char)cr;
			fx.cg = (char)cg;
			fx.cb = (char)cb;
			fx.clight = (char)light;
			fx.sat = (char)sat;
			fx.shine = shine;
			fx.scale = scale;
			fx.sink = 0;
			fx.align = RENDER_ALIGN_CENTER;
			fx.ml = fx.ll = fx.rl = fx.ul = fx.dl = i == weasel ? FX_ITEMBRIGHT : FX_ITEMLIGHT;

			render_sprite_fx(&fx, x, y);
		} else {
			/* an empty cell says what belongs in it - the doll is only
			 * readable as a body once the slots are labelled */
			named = 1;
		}

		/* the carried (or hovered) item fits here: call the slot out */
		if (cflags & wea_slot_flag[i]) {
			named = 1;
			namecol = whitecolor;
			if (i == 2 && (cflags & IF_WNTWOHANDED)) {
				namecol = redcolor;
			}
		}
		if (butsel >= BUT_WEA_BEG && butsel <= BUT_WEA_END && !vk_item && capbut == -1 && i == weasel) {
			named = 1;
			namecol = textcolor;
		}
		if (named) {
			render_text(x, yt, namecol, RENDER_ALIGN_CENTER | RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, weaname[i]);
		}

		if (i == 2 && item[weatab[1]] && (item_flags[weatab[1]] & IF_WNTWOHANDED)) {
			/* left hand blocked by a two-handed weapon in the right hand:
			 * red-tinted pad with a cross so the dead slot reads at a glance */
			render_sprite(5, x, y, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_CENTER);
			render_shaded_rect(x - 16, y - 16, x + 16, y + 16, IRGB(25, 6, 6), 100);
			render_line(x - 11, y - 11, x + 11, y + 11, IRGB(31, 0, 0));
			render_line(x + 11, y - 11, x - 11, y + 11, IRGB(31, 0, 0));
		}

		if (con_cnt && con_type == 2 && itemprice[weatab[i]]) {
			dx_drawtext_gold(x, y + 12, textcolor, (int)itemprice[weatab[i]]);
		}
	}

	/* gear lock in the window's footer */
	if (panel_content_rect(PANEL_EQUIPMENT, &cx1, &cy1, &cx2, &cy2)) {
		render_rect_alpha(cx1, cy2 - WEA_FOOT_H, cx2, cy2 - WEA_FOOT_H + 1, UI_BORDER, UI_A_RULE);
	}
	dx_copysprite_emerald(butx(BUT_WEA_LCK), buty(BUT_WEA_LCK), 2 - gear_lock, 2);
	render_text(butx(BUT_WEA_LCK) + 8, buty(BUT_WEA_LCK) - 4, gear_lock ? UI_TEXT : UI_TEXT_MUTED,
	    RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED, gear_lock ? "Gear locked" : "Gear free");
}

void display_look(void)
{
	int b;
	unsigned int sprite;
	unsigned short c1, c2, c3, shine;
	unsigned char scale, cr, cg, cb, light, sat;
	RenderFX fx;

	/* the shared window chrome instead of the classic stone sprite - the
	 * one window that still looked like 2003 */
	int lx1 = dotx(DOT_LOK), ly1 = doty(DOT_LOK);
	int lw = LOOK_W, lh = LOOK_H;

	ui_panel(lx1, ly1, lx1 + lw, ly1 + lh);
	ui_window_titlebar(lx1, ly1, lx1 + lw, look_name[0] ? look_name : "Look", 0);
	ui_glyph_button(
	    lx1 + lw - UI_WIN_PAD - UI_WIN_GLYPH / 2, ly1 + UI_WIN_TITLE_H / 2, UI_GLYPH_CLOSE, butsel == BUT_NOLOOK);

	/* the classic 12-wide gear strip, in slot cells along the bottom */
	for (b = BUT_WEA_BEG; b <= BUT_WEA_END; b++) {
		int i = b - BUT_WEA_BEG;
		int x = lx1 + LOOK_STRIP_X + FDX / 2 + i * FDX;
		int y = ly1 + lh - UI_WIN_PAD - FDX / 2;

		ui_slot_cell(x, y, FDX / 2 - 1, 0, 0);
		if (lookinv[weatab[i]]) {
			bzero(&fx, sizeof(fx));

			sprite =
			    trans_asprite(0, lookinv[weatab[i]], tick, &scale, &cr, &cg, &cb, &light, &sat, &c1, &c2, &c3, &shine);
			fx.sprite = sprite;
			fx.c1 = c1;
			fx.c2 = c2;
			fx.c3 = c3;
			fx.shine = shine;
			fx.cr = (char)cr;
			fx.cg = (char)cg;
			fx.cb = (char)cb;
			fx.clight = (char)light;
			fx.sat = (char)sat;
			fx.scale = scale;
			fx.sink = 0;
			fx.align = RENDER_ALIGN_CENTER;
			fx.ml = fx.ll = fx.rl = fx.ul = fx.dl = FX_ITEMLIGHT;
			render_sprite_fx(&fx, x, y);
		}
	}
	render_text_break(lx1 + LOOK_PORTRAIT_W, ly1 + UI_WIN_TITLE_H + 8, lx1 + lw - UI_WIN_PAD - 4, UI_TEXT,
	    RENDER_TEXT_LEFT, look_desc);

	{
		static int look_anim = 4, look_step = 0, look_dir = 0;
		int l_csprite, l_scale, l_cr, l_cg, l_cb, l_light, l_sat, l_c1, l_c2, l_c3, l_shine;

		bzero(&fx, sizeof(fx));

		l_csprite = trans_charno(
		    (int)looksprite, &l_scale, &l_cr, &l_cg, &l_cb, &l_light, &l_sat, &l_c1, &l_c2, &l_c3, &l_shine, (int)tick);

		fx.sprite = (unsigned int)get_player_sprite(l_csprite, look_dir, look_anim, look_step, 16, (int)(uint32_t)tick);
		look_step++;
		if (look_step == 16) {
			look_step = 0;
			look_anim++;
			if (look_anim > 6) {
				look_anim = 4;
				look_dir += 2;
				if (look_dir > 7) {
					look_dir = 0;
				}
			}
		}
		fx.scale = (unsigned char)l_scale;
		fx.shine = (unsigned short)l_shine;
		fx.cr = (char)l_cr;
		fx.cg = (char)l_cg;
		fx.cb = (char)l_cb;
		fx.clight = (char)l_light;
		fx.sat = (char)l_sat;

		if ((int)looksprite < 120 || amod_is_playersprite((int)looksprite)) {
			fx.c1 = (unsigned short)lookc1;
			fx.c2 = (unsigned short)lookc2;
			fx.c3 = (unsigned short)lookc3;
		} else {
			fx.c1 = (unsigned short)l_c1;
			fx.c2 = (unsigned short)l_c2;
			fx.c3 = (unsigned short)l_c3;
		}
		fx.sink = 0;
		fx.align = RENDER_ALIGN_OFFSET;
		fx.ml = fx.ll = fx.rl = fx.ul = fx.dl = FX_ITEMLIGHT;
		render_sprite_fx(&fx, lx1 + LOOK_PORTRAIT_W / 2 + 4, ly1 + UI_WIN_TITLE_H + LOOK_PORTRAIT_H);
	}
}

void display_inventory(void)
{
	int b;
	static char *fstr[4] = {"F1", "F2", "F3", "F4"};
	unsigned int sprite;
	unsigned short c1, c2, c3, shine;
	unsigned char scale, cr, cg, cb, light, sat;
	RenderFX fx;

	// fkey[0]=fkey[1]=fkey[2]=fkey[3]=0;

	/* sunken trough behind the scrollbar rail; the window frame itself is
	 * drawn by panels_display_frames() */
	render_rounded_rect_filled_alpha(dotx(DOT_IN1), doty(DOT_IN1), dotx(DOT_IN1) + INV_RAIL_W,
	    doty(DOT_IN1) + __invdy * FDX, UI_R_CHIP, UI_BG_SUNKEN, UI_A_SOCKET);

	for (b = BUT_INV_BEG; b <= BUT_INV_END; b++) {
		int i;
		if (but[b].flags & BUTF_NOHIT) {
			continue; /* beyond the active rows*cols grid */
		}
		i = 30 + invoff * INVDX + b - BUT_INV_BEG;
		if (i >= _inventorysize) {
			continue; /* partial last row: no slot behind this cell */
		}
		int c = (i - 2) % 4;
		int x = butx(b);
		int y = buty(b);
		int yt = y + 12;

		render_sprite(opt_sprite(SPR_ITPAD), x, y, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_CENTER);
		if (i == invsel) {
			render_sprite(opt_sprite(SPR_ITSEL), x, y, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_CENTER);
		}
		if (item[i]) {
			bzero(&fx, sizeof(fx));

			sprite = trans_asprite(0, item[i], tick, &scale, &cr, &cg, &cb, &light, &sat, &c1, &c2, &c3, &shine);
			fx.sprite = sprite;
			fx.shine = shine;
			fx.c1 = c1;
			fx.c2 = c2;
			fx.c3 = c3;
			fx.cr = (char)cr;
			fx.cg = (char)cg;
			fx.cb = (char)cb;
			fx.clight = (char)light;
			fx.sat = (char)sat;
			fx.scale = scale;
			fx.sink = 0;
			fx.align = RENDER_ALIGN_CENTER;
			fx.ml = fx.ll = fx.rl = fx.ul = fx.dl = (i == invsel) ? FX_ITEMBRIGHT : FX_ITEMLIGHT;
			render_sprite_fx(&fx, x, y);
			if ((sprite = (unsigned int)additional_sprite((unsigned int)item[i], (int)tick)) != 0U) {
				fx.sprite = sprite;
				render_sprite_fx(&fx, x, y);
			}
		}
		if (fkeyitem[c] == i) {
			render_text(x, y - 18, textcolor, RENDER_TEXT_SMALL | RENDER_ALIGN_CENTER | RENDER_TEXT_FRAMED, fstr[c]);
		}
		if (con_cnt && con_type == 2 && itemprice[i]) {
			dx_drawtext_gold(x, yt, textcolor, (int)itemprice[i]);
		}
	}
}

void display_container(void)
{
	int b;
	unsigned int sprite;
	unsigned short c1, c2, c3, shine;
	unsigned char scale, cr, cg, cb, light, sat;
	RenderFX fx;

	/* the container's name lives in its window's title bar (panel_title()),
	 * so no separate header plate is drawn here */
	for (b = BUT_CON_BEG; b <= BUT_CON_END; b++) {
		int i = conoff * CONDX + b - BUT_CON_BEG;

		if (but[b].flags & BUTF_NOHIT) {
			continue;
		}
		int x = butx(b);
		int y = buty(b);
		int yt = y + 12;
		unsigned short int color;

		render_sprite(opt_sprite(SPR_ITPAD), x, y, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_CENTER);
		if (i == consel) {
			render_sprite(opt_sprite(SPR_ITSEL), x, y, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_CENTER);
		}
		if (i >= con_cnt) {
			continue;
		}
		if (container[i]) {
			bzero(&fx, sizeof(fx));

			sprite = trans_asprite(0, container[i], tick, &scale, &cr, &cg, &cb, &light, &sat, &c1, &c2, &c3, &shine);
			fx.sprite = sprite;
			fx.shine = shine;
			fx.c1 = c1;
			fx.c2 = c2;
			fx.c3 = c3;
			fx.cr = (char)cr;
			fx.cg = (char)cg;
			fx.cb = (char)cb;
			fx.clight = (char)light;
			fx.sat = (char)sat;
			fx.scale = scale;
			fx.sink = 0;
			fx.align = RENDER_ALIGN_CENTER;
			fx.ml = fx.ll = fx.rl = fx.ul = fx.dl = i == consel ? FX_ITEMBRIGHT : FX_ITEMLIGHT;
			render_sprite_fx(&fx, x, y);
		}

		if (con_type == 2 && price[i]) {
			if (price[i] > gold && i != consel) {
				color = darkredcolor;
			} else if (price[i] > gold && i == consel) {
				color = redcolor;
			} else if (i == consel) {
				color = whitecolor;
			} else {
				color = textcolor;
			}

			dx_drawtext_gold(x, yt, color, (int)price[i]);
		}
	}
}

/* Purse and trashcan sit on the inventory window's footer row: the purse is
 * both the gold readout and the handle you grab to take/drop coins, the
 * trashcan is where a carried item goes to be destroyed. */
void display_gold(void)
{
	int x = butx(BUT_GLD), y = buty(BUT_GLD);
	int cx1, cy1, cx2, cy2;

	if (panel_content_rect(PANEL_INVENTORY, &cx1, &cy1, &cx2, &cy2)) {
		render_rect_alpha(cx1, cy2 - INV_FOOT_H, cx2, cy2 - INV_FOOT_H + 1, UI_BORDER, UI_A_RULE);
	}

	render_sprite(SPR_GOLD_BEG + 7, x, y,
	    lcmd == CMD_TAKE_GOLD || lcmd == CMD_DROP_GOLD ? RENDERFX_BRIGHT : RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_CENTER);

	if (capbut == BUT_GLD) {
		/* splitting a stack: what you are pulling out over what stays */
		dx_drawtext_gold(x + 34, y - 5, whitecolor, (int)takegold);
		dx_drawtext_gold(x + 34, y + 5, UI_TEXT_MUTED, (int)(gold - takegold));
	} else {
		dx_drawtext_gold(x + 34, y, UI_TEXT_GOLD, (int)gold);
	}

	/* trashcan: dimmed until there is something to throw away */
	x = butx(BUT_JNK);
	y = buty(BUT_JNK);
	if (vk_item || csprite) {
		render_sprite(25, x, y, lcmd == CMD_JUNK_ITEM ? RENDERFX_BRIGHT : RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_CENTER);
	} else {
		render_sprite(25, x, y, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_CENTER);
		render_rect_alpha(x - 13, y - 13, x + 13, y + 13, UI_BG_BASE, 120);
	}
}

void display_citem(void)
{
	int x, y;
	unsigned int sprite;
	unsigned short c1, c2, c3, shine;
	unsigned char scale, cr, cg, cb, light, sat;
	RenderFX fx;

	// citem (the trashcan is part of the inventory window - display_gold())
	if (!csprite) {
		return;
	}

	x = mousex;
	y = mousey;

	if (x < 0 || x >= UIXRES) {
		return;
	}
	if (y < 0 || y >= UIYRES) {
		return;
	}

	bzero(&fx, sizeof(fx));

	sprite = trans_asprite(0, csprite, tick, &scale, &cr, &cg, &cb, &light, &sat, &c1, &c2, &c3, &shine);
	fx.sprite = sprite;
	fx.shine = shine;
	fx.c1 = c1;
	fx.c2 = c2;
	fx.c3 = c3;
	fx.cr = (char)cr;
	fx.cg = (char)cg;
	fx.cb = (char)cb;
	fx.clight = (char)light;
	fx.sat = (char)sat;
	fx.scale = scale;
	fx.sink = 0;
	fx.align = RENDER_ALIGN_CENTER;
	fx.ml = fx.ll = fx.rl = fx.ul = fx.dl = FX_ITEMLIGHT;
	render_push_clip();
	render_more_clip(0, 0, UIXRES, UIYRES);
	render_sprite_fx(&fx, x, y);
	if ((sprite = (unsigned int)additional_sprite((unsigned int)csprite, (int)tick)) != 0U) {
		fx.sprite = sprite;
		render_sprite_fx(&fx, x, y);
	}

	if (cprice) {
		dx_drawtext_gold(x, y + 5 + 12, textcolor, (int)cprice);
	}
	render_pop_clip();
}

/* Slim scrollbar rail matching the window chrome: a sunken trough, two
 * triangle arrows and a rounded thumb. The classic SPR_SCR* sprites are far
 * wider than the 12px rail the panels reserve - they used to spill over the
 * first item column and the skill values. */
static void draw_scroll_rail(int b_up, int b_tr, int b_dw, int maxoff)
{
	int cx = butx(b_up);
	int x1 = cx - UI_SCROLLBAR_W / 2 + 1, x2 = cx + UI_SCROLLBAR_W / 2 - 1;
	int top = buty(b_up), bot = buty(b_dw);
	int ty = buty(b_tr);
	int hot_up = (butsel == b_up), hot_dw = (butsel == b_dw);
	int hot_tr = (butsel == b_tr) || (capbut == b_tr);
	unsigned short thumb = (maxoff > 0) ? (hot_tr ? UI_ACCENT : UI_BORDER_STRONG) : UI_BG_ROW_ACTIVE;

	render_rounded_rect_filled_alpha(x1, top, x2, bot, UI_R_CHIP, UI_BG_SUNKEN, UI_A_CONTROL);

	render_triangle_filled_alpha(cx, top - 4, cx - 4, top + 2, cx + 4, top + 2,
	    hot_up ? UI_ACCENT : (maxoff > 0 ? UI_TEXT_LABEL : UI_TEXT_DISABLED), 255);
	render_triangle_filled_alpha(cx, bot + 4, cx - 4, bot - 2, cx + 4, bot - 2,
	    hot_dw ? UI_ACCENT : (maxoff > 0 ? UI_TEXT_LABEL : UI_TEXT_DISABLED), 255);

	render_rounded_rect_filled_alpha(x1, ty - 5, x2, ty + 5, UI_R_CHIP, thumb, hot_tr ? 255 : UI_A_CONTROL);
}

void display_scrollbar_left(void)
{
	draw_scroll_rail(BUT_SCL_UP, BUT_SCL_TR, BUT_SCL_DW, max_skloff);
}

void display_scrollbar_container(void)
{
	draw_scroll_rail(BUT_CSC_UP, BUT_CSC_TR, BUT_CSC_DW, max_conoff);
}

void display_scrollbar_right(void)
{
	draw_scroll_rail(BUT_SCR_UP, BUT_SCR_TR, BUT_SCR_DW, max_invoff);
}

/* Which world is this? Everything but production wears a small amber tag
 * at the top of the screen so a tester never mistakes pre-production or a
 * local stack for the live game. */
void display_environment_tag(void)
{
	const char *env = client_environment_label();
	char buf[40];

	if (!env || !*env) {
		return;
	}
	snprintf(buf, sizeof(buf), "%s SERVER", env);
	render_text(UIXRES / 2, 3, IRGB(31, 24, 6), RENDER_TEXT_SMALL | RENDER_ALIGN_CENTER | RENDER_TEXT_FRAMED, buf);
}

void display_scrollbars(void)
{
	display_scrollbar_left();
	display_scrollbar_right();
}

void display_skill(void)
{
	int b;
	char buf[256];
	int cn = (int)map[MAPDX * MAPDY / 2].cn;

	for (b = BUT_SKL_BEG; b <= BUT_SKL_END; b++) {
		int i = skloff + b - BUT_SKL_BEG;
		int x = butx(b);
		int y = buty(b);
		int yt = y - 4;
		int bsx = x + 10;
		int bex = x + SKLWIDTH;
		int bsy = y + 4;
		int barsize;

		if (y + 4 > doty(DOT_SK2)) {
			continue;
		}

		if (i >= skltab_cnt) {
			continue;
		}

		if (!(but[b].flags & BUTF_NOHIT)) {
			if (i == sklsel) {
				dx_copysprite_emerald(x, y, 4, 2);
			} else {
				dx_copysprite_emerald(x, y, 4, 1);
			}
		} else if (skltab[i].button) {
			dx_copysprite_emerald(x, y, 1, 0);
		}

		if (skltab[i].v == STV_EMPTYLINE) {
			continue;
		}

		if (skltab[i].v == STV_JUSTAVALUE) {
			render_text(bsx, yt, textcolor, RENDER_TEXT_LARGE | RENDER_TEXT_LEFT, skltab[i].name);
			render_text_fmt(bex, yt, textcolor, RENDER_TEXT_LARGE | RENDER_TEXT_RIGHT, "%d", skltab[i].curr);
			continue;
		}

		if (skltab[i].button) {
			barsize = skltab[i].barsize;
		} else {
			barsize = 0;
		}
		if (barsize > 0) {
			render_rect(bsx, bsy, bsx + barsize, bsy + 1, bluecolor);
		} else if (barsize < 0) {
			render_rect(bsx, bsy, bex + barsize, bsy + 1, redcolor);
		}

		int done = 0;
		if (sv_ver == 35) {
			switch (skltab[i].v) {
			case V35_OFFENSE:
			case V35_DEFENSE:
				sprintf(buf, "%d", skltab[i].curr + rage / 4);
				done = 1;
				break;

			case V35_IMMUNITY:
				sprintf(buf, "%2d/%2d", skltab[i].base, skltab[i].curr + rage / 4);
				done = 1;
				break;
			case V35_WEAPON:
				sprintf(buf, "%.2f", skltab[i].curr / 20.0);
				done = 1;
				break;
			}
		} else {
			switch (skltab[i].v) {
			case V3_WEAPON:
				sprintf(buf, "%d", skltab[i].curr);
				done = 1;
				break;
			}
		}

		if (!done) {
			switch (v_val(skltab[i].v)) {
			case V_SPEED:
			case V_LIGHT:
			case V_COLD:
				sprintf(buf, "%d", skltab[i].curr);
				break;

			case V_ARMOR:
				sprintf(buf, "%.2f", skltab[i].curr / 20.0);
				break;
			case V_MANA:
				sprintf(buf, "%d/%2d/%2d", mana, skltab[i].base, skltab[i].curr);
				break;
			case V_HP:
				if (lifeshield) {
					sprintf(buf, "%d+%d/%2d/%2d", hp, lifeshield, skltab[i].base, skltab[i].curr);
				} else {
					sprintf(buf, "%d/%2d/%2d", hp, skltab[i].base, skltab[i].curr);
				}
				break;
			case V_ENDURANCE:
				sprintf(buf, "%d/%2d/%2d", endurance, skltab[i].base, skltab[i].curr);
				break;
			default:
				if (!amod_display_skill_line(skltab[i].v, skltab[i].base, skltab[i].curr, cn, buf)) {
					if (skltab[i].v >= V_PROFBASE &&
					    skltab[i].v < V_PROFBASE + 20) { // base-only render is a profession thing
						sprintf(buf, "%d", skltab[i].base);
					} else {
						sprintf(buf, "%2d/%2d", skltab[i].base, skltab[i].curr);
					}
				}
				break;
				;
			}
		}

		render_text(bsx, yt, textcolor, RENDER_TEXT_LARGE | RENDER_TEXT_LEFT, skltab[i].name);
		render_text(bex, yt, textcolor, RENDER_TEXT_LARGE | RENDER_TEXT_RIGHT, buf);
	}
}

void display_keys(void)
{
	/* Legacy keytab display disabled - hotbar system replaces this */
	(void)0;
}

/* Close button in the top-right corner of the tutorial popup. */
static void tutor_close_rect(int *x1, int *y1, int *x2, int *y2)
{
	*x1 = dotx(DOT_TUT) + 410 - 13;
	*y1 = doty(DOT_TUT) + 3;
	*x2 = dotx(DOT_TUT) + 410 - 3;
	*y2 = doty(DOT_TUT) + 13;
}

int tutor_click(int x, int y)
{
	int x1, y1, x2, y2;

	if (!show_tutor) {
		return 0;
	}
	tutor_close_rect(&x1, &y1, &x2, &y2);
	if (x >= x1 && x <= x2 && y >= y1 && y <= y2) {
		show_tutor = 0;
		return 1;
	}
	return 0;
}

void display_tutor(void)
{
	int mx = dotx(DOT_TUT) + 406, my = doty(DOT_TUT) + 80;
	char buf[80];
	char hint1[128];
	int bh;

	if (!show_tutor) {
		return;
	}

	/* Escape dismisses open windows before it opens the menu, so it always
	 * works here; name the dedicated Cancel All key instead when one is
	 * bound. */
	InputBinding *cancel = input_find_by_id("ui.cancel");
	if (cancel && cancel->key != SDLK_UNKNOWN) {
		snprintf(hint1, sizeof(hint1), "Press %s or click the X to dismiss this window.",
		    input_key_to_string(cancel->key, cancel->modifiers));
	} else {
		snprintf(hint1, sizeof(hint1), "Press ESCAPE or click the X to dismiss this window.");
	}
	bh = 102;

	ui_panel(dotx(DOT_TUT), doty(DOT_TUT), dotx(DOT_TUT) + 410, doty(DOT_TUT) + bh);

	/* close button */
	{
		int x1, y1, x2, y2;
		tutor_close_rect(&x1, &y1, &x2, &y2);
		int hov = (mousex >= x1 && mousex <= x2 && mousey >= y1 && mousey <= y2);
		int state = UI_BTN_REST;
		if (hov) {
			state = vk_lbut ? UI_BTN_PRESSED : UI_BTN_HOVER;
		}
		ui_button(x1, y1, x2 - x1, y2 - y1, "X", state);
	}

	/* dismissal hint below the server text */
	render_rect_alpha(
	    dotx(DOT_TUT) + 4, doty(DOT_TUT) + 84, dotx(DOT_TUT) + 406, doty(DOT_TUT) + 85, UI_BORDER, UI_A_RULE);
	render_text(dotx(DOT_TUT) + 6, doty(DOT_TUT) + 88, UI_TEXT_MUTED, RENDER_TEXT_SMALL | RENDER_TEXT_LEFT, hint1);

	int x = dotx(DOT_TUT) + 6;
	int y = doty(DOT_TUT) + 4;
	const char *ptr = tutor_text;
	while (*ptr) {
		while (*ptr == ' ') {
			ptr++;
		}
		while (*ptr == '$') {
			ptr++;
			x = dotx(DOT_TUT) + 6;
			y += 10;
			if (y >= my) {
				break;
			}
		}
		while (*ptr == ' ') {
			ptr++;
		}
		int n = 0;
		while (*ptr && *ptr != ' ' && *ptr != '$' && n < 79) {
			buf[n++] = *ptr++;
		}
		buf[n] = 0;
		if (x + render_text_length(RENDER_TEXT_LEFT | RENDER_TEXT_LARGE, buf) >= mx) {
			x = dotx(DOT_TUT) + 6;
			y += 10;
			if (y >= my) {
				break;
			}
		}
		x = render_text(x, y, UI_TEXT, RENDER_TEXT_LEFT | RENDER_TEXT_LARGE, buf) + 3;
	}
}

// date stuff
#define DAYLEN  (60 * 60 * 2)
#define HOURLEN (DAYLEN / 24)
#define MINLEN  (HOURLEN / 60)

static void trans_date(int t, int *phour, int *pmin)
{
	if (pmin) {
		*pmin = (t / MINLEN) % 60;
	}
	if (phour) {
		*phour = (t / HOURLEN) % 24;
	}
}

/* System menu strip: the Menu / Help / Quests buttons that lived in the
 * top bar's right corner. Plain ui_buttons on a HUD plate; the commands
 * are the classic BUT_EXIT / BUT_HELP / BUT_QUEST ones. */
void display_sysmenu(void)
{
	static const struct {
		int but;
		const char *label;
	} seg[3] = {
	    {BUT_EXIT, "Menu"},
	    {BUT_HELP, "Help"},
	    {BUT_QUEST, "Quests"},
	};

	for (int i = 0; i < 3; i++) {
		int x = dotx(DOT_MENU) + i * (SYSM_BTN_W + SYSM_GAP);
		int y = doty(DOT_MENU);
		int active = (seg[i].but == BUT_HELP && display_help) || (seg[i].but == BUT_QUEST && display_quest);
		int hot = (butsel == seg[i].but);
		int state = active ? UI_BTN_ACTIVE : (hot ? (vk_lbut ? UI_BTN_PRESSED : UI_BTN_HOVER) : UI_BTN_REST);

		ui_button(x, y, SYSM_BTN_W, SYSM_BTN_H, seg[i].label, state);
	}
}

/* Classic flip-digit game clock, on its own little HUD plate (hidden by
 * default - the mod ships a modern clock widget; this one is for the
 * players who liked the old one). Sprites 200.. are the flip animation
 * frames the top bar used to show. */
void display_clock(void)
{
	int h, m;
	int h1, h2, m1, m2;
	static int rh1 = 0, rh2 = 0, rm1 = 0, rm2 = 0;
	int x = dotx(DOT_CLK) + 1, y = doty(DOT_CLK) + 2;

	trans_date((int)realtime, &h, &m);

	h1 = h / 10 * 3;
	h2 = h % 10 * 3;
	m1 = m / 10 * 3;
	m2 = m % 10 * 3;

	if (h1 != rh1) {
		rh1++;
	}
	if (rh1 == 30) {
		rh1 = 0;
	}
	if (h2 != rh2) {
		rh2++;
	}
	if (rh2 == 30) {
		rh2 = 0;
	}
	if (m1 != rm1) {
		rm1++;
	}
	if (rm1 == 18) {
		rm1 = 0;
	}
	if (m2 != rm2) {
		rm2++;
	}
	if (rm2 == 30) {
		rm2 = 0;
	}

	render_sprite((unsigned int)(200 + rh1), x + 0 * 10, y, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_NORMAL);
	render_sprite((unsigned int)(200 + rh2), x + 1 * 10, y, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_NORMAL);
	render_text(x + 2 * 10 + 3, y, UI_TEXT_MUTED, UI_FONT_BODY, ":");
	render_sprite((unsigned int)(200 + rm1), x + 2 * 10 + 8, y, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_NORMAL);
	render_sprite((unsigned int)(200 + rm2), x + 3 * 10 + 8, y, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_NORMAL);

	sprintf(hover_time_text, "%02d:%02d Astonia Standard Time", h, m);
}

void display_text(void)
{
	int link;

	render_display_text();

	if ((link = render_scantext(mousex, mousey, hitsel))) {
		hittype = link;
	} else {
		hitsel[0] = 0;
	}
}

/* Speed selector.
 *
 * A three-segment control, fastest first (dots.c owns the order). Each segment
 * carries chevrons for how quick it is (>>> fast, >> normal, > stealth), its
 * name, and the key bound to it - the F-key captions used to be hardcoded. */
static void speed_chevrons(int cx, int cy, int n, unsigned short col, unsigned char alpha)
{
	int w = 3, h = 4, gap = 1;
	int total = n * (w + gap) - gap;
	int x = cx - total / 2;

	for (int i = 0; i < n; i++) {
		int lx = x + i * (w + gap);

		render_line_alpha(lx, cy - h, lx + w, cy, col, alpha);
		render_line_alpha(lx + w, cy, lx, cy + h, col, alpha);
	}
}

void display_mode(void)
{
	static const struct {
		int but;
		int mode;
		int speed;
		const char *label;
		const char *bind;
	} seg[3] = {
	    {BUT_MOD_WALK1, 1, 3, "Fast", "move.speed_fast"},
	    {BUT_MOD_WALK0, 0, 2, "Normal", "move.speed_normal"},
	    {BUT_MOD_WALK2, 2, 1, "Stealth", "move.speed_stealth"},
	};

	int i;

	for (i = 0; i < 3; i++) {
		int x = butx(seg[i].but) - SPEED_SEG_W / 2;
		int y = buty(seg[i].but) - SPEED_SEG_H / 2;
		int active = (pspeed == seg[i].mode);
		int hot = (butsel == seg[i].but);
		unsigned short text = active ? UI_TEXT : (hot ? UI_TEXT : UI_TEXT_LABEL);
		InputBinding *b = input_find_by_id(seg[i].bind);
		const char *key = (b && b->key != SDLK_UNKNOWN) ? input_key_to_string(b->key, b->modifiers) : "";

		if (active) {
			render_rounded_rect_filled_alpha(
			    x, y, x + SPEED_SEG_W, y + SPEED_SEG_H, UI_R_BUTTON, UI_BG_ROW_ACTIVE, UI_A_CONTROL);
			render_gradient_rect_v(
			    x + 1, y + 1, x + SPEED_SEG_W - 1, y + SPEED_SEG_H / 2, UI_ACCENT_DIM, UI_BG_BASE, 120);
			render_rounded_rect_alpha(x, y, x + SPEED_SEG_W, y + SPEED_SEG_H, UI_R_BUTTON, UI_ACCENT, 255);
			render_rect_alpha(x + 3, y + SPEED_SEG_H - 3, x + SPEED_SEG_W - 3, y + SPEED_SEG_H - 2, UI_ACCENT, 255);
			if (render_glow_available()) {
				/* faint amber underglow on the selected mode - the same
				 * family as the lit effect orbs next door */
				render_glow_line(
				    x + 6, y + SPEED_SEG_H - 2, x + SPEED_SEG_W - 6, y + SPEED_SEG_H - 2, UI_ACCENT, 5.0f, 0.8f, 0.22f);
			}
		} else if (hot) {
			render_rounded_rect_filled_alpha(
			    x, y, x + SPEED_SEG_W, y + SPEED_SEG_H, UI_R_BUTTON, UI_BG_ROW_HOVER, UI_A_ROW_HOVER);
			render_rounded_rect_alpha(x, y, x + SPEED_SEG_W, y + SPEED_SEG_H, UI_R_BUTTON, UI_ACCENT, UI_A_BORDER_HOV);
		}

		speed_chevrons(
		    x + SPEED_SEG_W / 2, y + 5, seg[i].speed, active ? UI_ACCENT : UI_TEXT_MUTED, active ? 255 : 170);
		render_text(x + SPEED_SEG_W / 2, y + 8, text, UI_FONT_CENTER, seg[i].label);
		if (*key) {
			render_text(x + SPEED_SEG_W / 2, y + 17, active ? UI_ACCENT : UI_TEXT_DISABLED, UI_FONT_CENTER, key);
		}
	}
}

/* ── Buff chips ─────────────────────────────────────────────────────────
 *
 * One orb per tracked effect (potion, heal/freeze, bless, rage). An idle
 * effect is a dark socket; an active one is a colored orb lit from within
 * (an additive GPU glow when the fancy-effects pipeline is on, a gradient
 * fill either way), with the remaining time as a rim ring that unwinds
 * clockwise - a small hot spark rides its leading edge - and the seconds
 * printed underneath. Nearly-expired effects pulse hard. */
static void buff_chip(int idx, const char *tag, unsigned short color, int active, int pct, const char *sub)
{
	int x1 = dotx(DOT_SSP) + idx * (BUFF_CHIP + BUFF_GAP);
	int y1 = doty(DOT_SSP);
	int cx = x1 + BUFF_CHIP / 2, cy = y1 + BUFF_CHIP / 2;
	int r = BUFF_CHIP / 2 - 1;
	int glow = render_glow_available();
	int urgent;
	float pulse;

	if (pct < 0) {
		pct = 0;
	}
	if (pct > 100) {
		pct = 100;
	}
	urgent = active && pct <= 15;
	/* ~1Hz breath, doubled while the effect is about to run out */
	pulse = 0.5f + 0.5f * sinf((float)tick * (urgent ? 0.55f : 0.26f));

	/* socket: a dark well with a faint raised rim - always there, so the
	 * row reads as four fixed slots whatever is active */
	render_circle_filled_alpha(cx, cy, r, UI_BG_SUNKEN, UI_A_SOCKET);
	render_gradient_circle(cx, cy, r, UI_BG_BASE, 0, 160);
	render_circle_alpha(cx, cy, r, active ? color : UI_BORDER, active ? 200 : UI_A_BORDER_REST);

	if (active) {
		float f = (float)pct / 100.0f;
		float ang = -(float)M_PI / 2.0f + 2.0f * (float)M_PI * f;
		int tipx = cx + (int)lroundf(cosf(ang) * (float)(r - 1));
		int tipy = cy + (int)lroundf(sinf(ang) * (float)(r - 1));

		/* the orb: colored, brighter at the centre - lit from within */
		render_gradient_circle(cx, cy, r - 2, color, (unsigned char)(150 + 60 * f), 30);

		if (glow) {
			/* soft halo bleeding past the rim, breathing with the pulse */
			render_glow(cx, cy, color, (float)BUFF_CHIP * 0.85f, 0.4f,
			    (0.16f + 0.24f * f) * (0.7f + 0.3f * pulse) * (urgent ? 1.5f : 1.0f));
			/* hot core so the middle reads as the light source */
			render_glow(cx, cy, color, (float)r * 0.8f, 2.2f, 0.30f + 0.15f * pulse);
		}

		/* remaining time as a rim ring, unwinding clockwise from 12
		 * o'clock, with a spark riding the leading edge */
		render_ring_alpha(cx, cy, r - 2, r, -90, -90 + (int)(360.0f * f), whitecolor, urgent ? 255 : 210);
		if (glow) {
			render_glow(tipx, tipy, color, 5.0f, 2.5f, 0.55f + 0.35f * pulse);
		} else {
			render_circle_filled_alpha(tipx, tipy, 1, whitecolor, 255);
		}
	}

	render_text(cx, cy - 5, active ? UI_TEXT : UI_TEXT_DISABLED, UI_FONT_CENTER, tag);
	if (active && sub && *sub) {
		render_text(
		    cx, y1 + BUFF_CHIP + 1, urgent && ((tick & 8) != 0U) ? UI_TEXT_ERROR : UI_TEXT_MUTED, UI_FONT_CENTER, sub);
	}
}

void display_selfspells(void)
{
	int cn = (int)map[mapmn(MAPDX / 2, MAPDY / 2)].cn;
	int pot_on = 0, mid_on = 0, bls_on = 0;
	int pot_pct = 0, mid_pct = 0, bls_pct = 0;
	char pot_txt[16] = "", mid_txt[16] = "", bls_txt[16] = "";
	const char *mid_tag = (sv_ver == 35) ? "HEA" : "FRZ";

	sprintf(hover_bless_text, "Bless: Not active");
	sprintf(hover_freeze_text, "Freeze: Not active");
	sprintf(hover_heal_text, "Heal: Not active");
	sprintf(hover_potion_text, "Potion: Not active");

	for (int n = 0; cn && n < 4; n++) {
		int nr = find_cn_ceffect(cn, n);
		if (nr == -1) {
			continue;
		}

		switch (ceffect[nr].generic.type) {
		case 9: {
			unsigned int left = ceffect[nr].bless.stop - tick;

			bls_on = 1;
			bls_pct = 100 * (int)left / (int)(ceffect[nr].bless.stop - ceffect[nr].bless.start);
			snprintf(bls_txt, sizeof(bls_txt), "%us", left / 24);
			sprintf(hover_bless_text, "Bless: %us to go", left / 24);
			break;
		}
		case 10:
#define HEALDURATION (TICKS * 8)
			if (sv_ver == 35) {
				unsigned int done = tick - ceffect[nr].heal.start;

				mid_on = 1;
				mid_pct = 100 - 100 * (int)done / HEALDURATION;
				snprintf(mid_txt, sizeof(mid_txt), "%.0fs", (ceffect[nr].heal.start + HEALDURATION - tick) / 24.0);
				sprintf(hover_heal_text, "Heal: %.1fs to go", (ceffect[nr].heal.start + HEALDURATION - tick) / 24.0);
			}
			break;

		case 11:
			if (sv_ver == 30) {
				unsigned int left = ceffect[nr].freeze.stop - tick;

				mid_on = 1;
				mid_pct = 100 * (int)left / (int)(ceffect[nr].freeze.stop - ceffect[nr].freeze.start);
				snprintf(mid_txt, sizeof(mid_txt), "%us", left / 24);
				sprintf(hover_freeze_text, "Freeze: %us to go", left / 24);
			}
			break;
		case 14: {
			unsigned int left = ceffect[nr].potion.stop - tick;

			pot_on = 1;
			pot_pct = 100 * (int)left / (int)(ceffect[nr].potion.stop - ceffect[nr].potion.start);
			snprintf(pot_txt, sizeof(pot_txt), "%us", left / 24);
			sprintf(hover_potion_text, "Potion: %us to go", left / 24);
			break;
		}
		}
	}

	buff_chip(0, "POT", IRGB(8, 26, 10), pot_on, pot_pct, pot_txt);
	buff_chip(1, mid_tag, IRGB(10, 18, 28), mid_on, mid_pct, mid_txt);
	buff_chip(2, "BLS", IRGB(28, 22, 8), bls_on, bls_pct, bls_txt);
}

/* What the experience / military bars print right below themselves.
 * Cycled by clicking the bar: nothing -> percent -> have/need -> to go. */
#define BAR_INFO_NONE    0
#define BAR_INFO_PERCENT 1
#define BAR_INFO_VALUES  2
#define BAR_INFO_TOGO    3
#define BAR_INFO_MODES   4

static int exp_info_mode = BAR_INFO_PERCENT;
static int mil_info_mode = BAR_INFO_PERCENT;

void exp_bar_toggle(void)
{
	exp_info_mode = (exp_info_mode + 1) % BAR_INFO_MODES;
}

void mil_bar_toggle(void)
{
	mil_info_mode = (mil_info_mode + 1) % BAR_INFO_MODES;
}

/* 1234567 -> "1.23M", 45678 -> "45.7k", 999 -> "999" */
static const char *fmt_compact(long long v, char *buf, size_t sz)
{
	if (v >= 1000000000LL) {
		snprintf(buf, sz, "%.2fG", (double)v / 1e9);
	} else if (v >= 1000000LL) {
		snprintf(buf, sz, "%.2fM", (double)v / 1e6);
	} else if (v >= 10000LL) {
		snprintf(buf, sz, "%.1fk", (double)v / 1e3);
	} else {
		snprintf(buf, sz, "%lld", v);
	}
	return buf;
}

/* 1234567 -> "1,234,567" */
static const char *fmt_thousands(long long v, char *buf, size_t sz)
{
	char tmp[32];
	int len, i, o = 0;

	snprintf(tmp, sizeof(tmp), "%lld", v);
	len = (int)strlen(tmp);
	for (i = 0; i < len && o < (int)sz - 1; i++) {
		if (i && (len - i) % 3 == 0 && tmp[i] != '-') {
			buf[o++] = ',';
		}
		buf[o++] = tmp[i];
	}
	buf[o] = 0;
	return buf;
}

/* One row of the status panel: a long WoW-style bar with its info text
 * printed ON the bar (click cycles what it says). The width comes from the
 * panel's content rect so it follows the layout and the UI scale. */
static void draw_status_row(
    int y, int pct, unsigned short color, int flash, int mode, long long have, long long need, long long togo)
{
	int x1, y1, x2, y2;
	char a[32], b[32], text[80] = "";

	if (!panel_content_rect(PANEL_STATUS, &x1, &y1, &x2, &y2)) {
		return;
	}

	ui_meter_h(x1, y, x2, y + STAT_BAR_H, pct, flash ? whitecolor : color);
	/* WoW-style segment ticks every 10% */
	for (int i = 1; i < 10; i++) {
		int tx = x1 + (x2 - x1) * i / 10;

		render_rect_alpha(tx, y + 1, tx + 1, y + STAT_BAR_H - 1, UI_BG_BASE, 120);
	}

	switch (mode) {
	case BAR_INFO_PERCENT:
		snprintf(text, sizeof(text), "%d%%", pct);
		break;
	case BAR_INFO_VALUES:
		snprintf(text, sizeof(text), "%s / %s", fmt_compact(have, a, sizeof(a)), fmt_compact(need, b, sizeof(b)));
		break;
	case BAR_INFO_TOGO:
		snprintf(text, sizeof(text), "%s to go", fmt_compact(togo, a, sizeof(a)));
		break;
	default:
		break;
	}
	if (text[0]) {
		render_text((x1 + x2) / 2, y + 2, UI_TEXT, UI_FONT_CENTER, text);
	}
}

void display_exp(void)
{
	static int last_exp = 0, exp_ticker = 0;
	char n1[32], n2[32], n3[32], n4[32];

	sprintf(hover_level_text, "Level: unknown");

	int cn = (int)map[MAPDX * MAPDY / 2].cn;
	int level = player[cn].level;

	int expe = (int)experience;
	int clevel = exp2level(expe);
	int nlevel = level + 1;

	int step = level2exp(nlevel) - expe;
	int total = level2exp(nlevel) - level2exp(clevel);
	if (step > total) {
		step = total; // ugh. fix for level 1 with 0 exp
	}

	if (total) {
		long long have = total - step; /* exp gathered in this level */
		if (last_exp != expe) {
			exp_ticker = 3;
			last_exp = expe;
		}
		if (exp_ticker) {
			exp_ticker--;
		}

		char lead[96];

		draw_status_row(doty(DOT_STAT), (int)(100ll * have / total), IRGB(14, 8, 28), exp_ticker != 0, exp_info_mode,
		    have, total, step);
		/* the level tag sits at the bar's left end, whatever the mode */
		snprintf(lead, sizeof(lead), "Lv %d", clevel);
		render_text(dotx(DOT_STAT) + 4, doty(DOT_STAT) + 2, UI_TEXT_MUTED, UI_FONT_BODY, lead);

		snprintf(hover_level_text, 200,
		    "Level %d to %d: %s / %s (%lld%%)\n%s to go, total %s exp\n(click the bar to change the numbers shown)",
		    clevel, nlevel, fmt_thousands(have, n1, sizeof(n1)), fmt_thousands(total, n2, sizeof(n2)),
		    100LL * have / total, fmt_thousands(step, n3, sizeof(n3)), fmt_thousands(expe, n4, sizeof(n4)));
	}
}

char *_game_rankname[] = {
    "nobody", // 0
    "Private", // 1
    "Private First Class", // 2
    "Lance Corporal", // 3
    "Corporal", // 4
    "Sergeant", // 5     lvl 30
    "Staff Sergeant", // 6
    "Master Sergeant", // 7
    "First Sergeant", // 8     lvl 45
    "Sergeant Major", // 9
    "Second Lieutenant", // 10    lvl 55
    "First Lieutenant", // 11
    "Captain", // 12
    "Major", // 13
    "Lieutenant Colonel", // 14
    "Colonel", // 15
    "Brigadier General", // 16
    "Major General", // 17
    "Lieutenant General", // 18
    "General", // 19
    "Field Marshal", // 20    lvl 105
    "Knight of Astonia", // 21
    "Baron of Astonia", // 22
    "Earl of Astonia", // 23
    "Warlord of Astonia", // 24    lvl 125
    "Duke of Astonia", // 25    lvl 130
    "Archduke of Astonia", // 26    lvl 135
    "Prince of Astonia", // 27    lvl 140
    "High Prince of Astonia", // 28    lvl 145
    "Royal Guardian", // 29    lvl 150
    "Slayer of Demons", // 30    lvl 155
    "Astonian Champion", // 31    lvl 161
    "Defender of the Realm", // 32    lvl 167
    "Sword of Astonia", // 33    lvl 173
    "Shield of the Kingdom", // 34    lvl 179
    "Legendary Warrior", // 35    lvl 185
    "Immortal Guardian", // 36    lvl 188
    "Hero of Ages", // 37    lvl 191
    "Mythic Protector", // 38    lvl 194
    "Eternal Champion", // 39    lvl 197
    "Avatar of Astonia" // 40    lvl 200
};
char **game_rankname = _game_rankname;

int _game_rankcount = ARRAYSIZE(_game_rankname);
_Static_assert(ARRAYSIZE(_game_rankname) == 41, "rank table must cover ranks 0..40 (server mil_rank table)");
int *game_rankcount = &_game_rankcount;

DLL_EXPORT int mil_rank(int exp);

DLL_EXPORT int mil_rank(int exp)
{
	int n;

	for (n = 1; n < 50; n++) {
		if (exp < n * n * n) {
			return n - 1;
		}
	}
	return 99;
}

void display_military(void)
{
	int step, total, rank, cost1, cost2, maxrank;
	char n1[32], n2[32], n3[32], n4[32];

	sprintf(hover_rank_text, "Rank: none or unknown");

	/* Ranks follow the server: rank = cbrt(military points), capped at the last name
	 * in the table (40 = Avatar of Astonia). */
	maxrank = *game_rankcount - 1;
	rank = mil_rank((int)mil_exp);
	if (rank > maxrank) {
		rank = maxrank;
	}
	cost1 = rank * rank * rank;
	cost2 = (rank + 1) * (rank + 1) * (rank + 1);

	total = cost2 - cost1;
	step = (int)mil_exp - cost1;
	if (step > total) {
		step = total;
	}
	if (step < 0) {
		step = 0;
	}

	if (!mil_exp || !total) {
		/* no military points yet: the bar still exists, empty - players
		 * kept reporting it "missing" when it only appeared with honor */
		draw_status_row(doty(DOT_STAT) + STAT_ROW_H, 0, IRGB(28, 12, 4), 0, BAR_INFO_NONE, 0, 0, 0);
		render_text(dotx(DOT_STAT) + 4, doty(DOT_STAT) + STAT_ROW_H + 2, UI_TEXT_MUTED, UI_FONT_BODY,
		    "Military Standing - no rank yet");
		snprintf(hover_rank_text, 200, "No rank yet - military points come from fighting for your realm");
		return;
	}
	{
		unsigned short mil_color = IRGB(28, 12, 4);
		char lead[96];

		/* the rank table can have unnamed gaps - say so instead of %s-ing null */
		const char *rname = game_rankname[rank] && game_rankname[rank][0] ? game_rankname[rank] : NULL;

		if (rank < maxrank) {
			draw_status_row(doty(DOT_STAT) + STAT_ROW_H, 100 * step / total, mil_color, 0, mil_info_mode, step, total,
			    total - step);
			if (rname) {
				snprintf(lead, sizeof(lead), "Military Standing - %s", rname);
			} else {
				snprintf(lead, sizeof(lead), "Military Standing - unnamed rank %d", rank);
			}
			render_text(dotx(DOT_STAT) + 4, doty(DOT_STAT) + STAT_ROW_H + 2, UI_TEXT_MUTED, UI_FONT_BODY, lead);

			snprintf(hover_rank_text, 200,
			    "Rank %d '%s' to %d '%s': %s / %s (%d%%)\n%s to go, total %s military points", rank,
			    game_rankname[rank], rank + 1, game_rankname[rank + 1], fmt_thousands(step, n1, sizeof(n1)),
			    fmt_thousands(total, n2, sizeof(n2)), 100 * step / total, fmt_thousands(total - step, n3, sizeof(n3)),
			    fmt_thousands((long long)mil_exp, n4, sizeof(n4)));
		} else {
			/* Highest rank: full bar */
			draw_status_row(doty(DOT_STAT) + STAT_ROW_H, 100, mil_color, 0, BAR_INFO_NONE, 0, 0, 0);
			if (rname) {
				snprintf(lead, sizeof(lead), "Military Standing - %s (max)", rname);
			} else {
				snprintf(lead, sizeof(lead), "Military Standing - unnamed rank %d (max)", rank);
			}
			render_text(dotx(DOT_STAT) + 4, doty(DOT_STAT) + STAT_ROW_H + 2, UI_TEXT_MUTED, UI_FONT_BODY, lead);
			snprintf(hover_rank_text, 200, "Rank %d '%s' (highest rank)\ntotal %s military points", rank,
			    game_rankname[maxrank], fmt_thousands((long long)mil_exp, n4, sizeof(n4)));
		}
	}
}

/* The rage chip is the fourth socket of the buffs panel; unlike the timed
 * effects it fills up as rage builds rather than draining. */
void display_rage(void)
{
	int pct, cap;
	char txt[16] = "";

	sprintf(hover_rage_text, "Rage: Not active");

	if (!value[0][sv_val(V_RAGE)] || !rage) {
		buff_chip(3, "RGE", IRGB(28, 8, 8), 0, 0, "");
		return;
	}

	if (sv_ver == 35) {
		cap = value[0][V35_RAGE] + (int)(value[0][V35_TACTICS] * 0.15 + 0.1);
		sprintf(hover_rage_text, "Rage: +%d", rage / 4);
		snprintf(txt, sizeof(txt), "+%d", rage / 4);
	} else {
		cap = (int)value[0][V3_RAGE];
		sprintf(hover_rage_text, "Rage: %d%%", 100 * rage / max(1, cap));
		snprintf(txt, sizeof(txt), "%d%%", 100 * rage / max(1, cap));
	}
	pct = 100 * rage / max(1, cap);
	buff_chip(3, "RGE", IRGB(28, 8, 8), 1, pct, txt);
}

void display_game_special(void)
{
	int dx;

	if (!display_gfx) {
		return;
	}

	switch (display_gfx) {
	// TODO: these are the ugly tutorial arrows
	// since we want to re-write the input parts of the
	// GUI there's no point in updating them now
	// so: Make a new tutorial. Eventually.
	case 1:
		render_sprite(50473, 343, 540, 14, 0);
		break;
	case 2:
		render_sprite(50473, 423, 167, 14, 0);
		break;
	case 3:
		dx = (tick - display_time) * 450 / 120;
		if (dx < 450) {
			render_sprite(50475, 175 + dx, 60, 14, 0);
		}
		break;
	case 4:
		render_sprite(50475, 218, 60, 14, 0);
		break;
	case 5:
		render_sprite(50475, 257, 60, 14, 0);
		break;
	case 6:
		render_sprite(50475, 23, 45, 14, 0);
		break;
	case 7:
		render_sprite(50475, 75, 47, 14, 0);
		break;
	case 8:
		render_sprite(50475, UIXRES - 37, 62, 14, 0);
		break;

	case 9:
		dx = (tick - display_time) * 150 / 120;
		if (dx < 150) {
			render_sprite(50474, 188, 447 + dx, 14, 0);
		}
		break;

	case 10:
		render_sprite(50474, 205, 459, 14, 0);
		break;

	case 11:
		dx = (tick - display_time) * 150 / 120;
		if (dx < 150) {
			render_sprite(50476, 200, 440 + dx, 14, 0);
		}
		break;

	case 12:
		dx = (tick - display_time) * 150 / 120;
		if (dx < 150) {
			render_sprite(50476, 618, 445 + dx, 14, 0);
		}
		break;

	case 13:
		render_sprite(50476, 625, 456, 14, 0);
		break;
	case 14:
		render_sprite(50476, UIXRES - 100, 456, 14, 0);
		break;
	case 15:
		render_sprite(50476, UIXRES - 59, 456, 14, 0);
		break;

	case 16:
		render_sprite(50476, 353, 203, 14, 0);
		break;

	case 17:
		render_sprite(50473, UIXRES - 78, 382, 14, 0);
		render_sprite(50475, 257, 60, 14, 0);
		break;

	// TODO: this is used to display the maps in earth underground
	// needs testing.
	default:
		render_sprite(display_gfx, 550, 210, 14, 0);
		break;
	}
}

int action_enabled = 1;

char v3_action_row[2][MAXACTIONSLOT] = {
    //  0   1   2   3   4   5   6   7   8   9   0   1   2   3
    {'a', 's', 'd', ' ', ' ', ' ', 'f', 'g', ' ', ' ', ' ', 'h', ' ', 'l'},
    {' ', 'q', 'w', 'e', 'r', 't', 'z', 'u', 'i', 'o', 'p', ' ', 'm', ' '}};

static char *v3_action_text[MAXACTIONSLOT] = {"Attack", "Fireball", "Lightning Ball", "Flash", "Freeze", "Magic Shield",
    "Bless", "Heal", "Warcry", "Pulse", "Firering", "Take/Use/Give/Drop", "Map", "Look"};

static char *v3_action_desc[MAXACTIONSLOT] = {"Attacks another character using your equipped weapon, or your hands.",
    "Throws a fireball. Explodes for huge splash damage when it hits.",
    "Throws a slow moving ball of lightning. It will deal medium damage over time to enemies it passes.",
    "Summons a small ball of lightning to your side. It will deal medium damage over time to enemies near you.",
    "Slows down enemies close to you.",
    "Summons a magic shield that will protect you from damage. Collapses when used up.",
    "Increases the basic attributes (WIS/INT/AGI/STR) of the target.", "Restores some of the target's hitpoints.",
    "Gives you a temporary Life Shield, blocking some damage. Slows enemies and might interrupt spellcasting in a "
    "fairly wide radius around you.",
    "A finishing move: instantly kills adjacent enemies that are weakened enough, converting their remaining life "
    "force into mana for you. Has no effect on healthy enemies.",
    "Deals high damage to adjacent enemies.",
    "Interact with items. Can be used to take or use an item on the ground, or to drop or give an item on your mouse "
    "cursor.",
    "Cycles between the minimap, the big map and no map.", "Look at characters or items in the world."};

/* Skill gate per action: -1 = always available (look/map), -2 = never
 * (reserved/disabled). The tails MUST be explicit -2: an implicit 0 would
 * read as "requires skill index 0" (Hitpoints), making every reserved slot
 * look like an owned spell with a NULL name. */
static int v3_action_skill[MAXACTIONSLOT] = {V3_PERCEPT, V3_FIREBALL, V3_FLASH, V3_FLASH, V3_FREEZE, V3_MAGICSHIELD,
    V3_BLESS, V3_HEAL, V3_WARCRY, V3_PULSE, V3_FIREBALL, V3_PERCEPT, -1, -1,
    /* 14-23: reserved for new class actions */
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2};

char v35_action_row[2][MAXACTIONSLOT] = {{'a', 's', 'd', ' ', ' ', ' ', ' ', 'b', ' ', ' ', ' ', 'g', ' ', 'l'},
    {' ', 'q', 'w', 'e', 'r', 't', 'z', 'u', 'i', 'o', 'p', ' ', 'm', ' '}};

static char *v35_action_text[MAXACTIONSLOT] = {"Attack", "Fireball", "Lightning Ball", "Flash", "Freeze",
    "Magic Shield", "Bless", "Heal", "Warcry", "NOOP", "Firering", "Take/Use/Give/Drop", "Map", "Look"};

static char *v35_action_desc[MAXACTIONSLOT] = {"Attacks another character using your equipped weapon, or your hands.",
    "Throws a fireball. Explodes for huge splash damage when it hits.",
    "Throws a slow moving ball of lightning. It will deal medium damage over time to enemies it passes.",
    "Summons a small ball of lightning to your side. It will deal medium damage over time to enemies near you.",
    "Slows down enemies close to you.",
    "Summons a magic shield that will protect you from damage. Collapses when used up.",
    "Increases the basic attributes (WIS/INT/AGI/STR) of yourself.", "Restores some of the target's hitpoints.",
    "Gives you a temporary Life Shield, blocking some damage. Slows enemies and might interrupt spellcasting in a "
    "fairly wide radius around you.",
    "NOOP", "Deals high damage to adjacent enemies.",
    "Interact with items. Can be used to take or use an item on the ground, or to drop or give an item on your mouse "
    "cursor.",
    "Cycles between the minimap, the big map and no map.", "Look at characters or items in the world."};

static int v35_action_skill[MAXACTIONSLOT] = {V35_PERCEPT, V35_FIRE, V35_FLASH, V35_FLASH, V35_FREEZE, V35_MAGICSHIELD,
    V35_BLESS, V35_HEAL, V35_WARCRY, -2, V35_FIRE, V35_PERCEPT, -1, -1,
    /* 14-23: reserved for new class actions */
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2};

/* CAST_ID_* wire id per action for the generic CL_CAST path (protocol v4+),
 * -1 = none: the original actions keep their dedicated legacy opcodes via
 * the context.c dispatch, so a new castable action only needs its cast_id
 * here (plus text/desc/skill/spell_caps rows) to become fully usable. */
static int v3_action_cast_id[MAXACTIONSLOT] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    /* 14-23: reserved for new class actions */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

static int v35_action_cast_id[MAXACTIONSLOT] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    /* 14-23: reserved for new class actions */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

char (*action_row)[MAXACTIONSLOT] = v3_action_row;
static char **action_text = v3_action_text;
static char **action_desc = v3_action_desc;
int *action_skill = v3_action_skill;
static int *action_cast_id = v3_action_cast_id;

void set_v35_actions(void)
{
	action_row = v35_action_row;
	action_text = v35_action_text;
	action_desc = v35_action_desc;
	action_skill = v35_action_skill;
	action_cast_id = v35_action_cast_id;
}

int get_action_cast_id(int slot)
{
	if (slot < 0 || slot >= MAXACTIONSLOT) {
		return -1;
	}
	return action_cast_id[slot];
}

void actions_loaded(void)
{
	int i;

	for (i = 0; i < MAXACTIONSLOT; i++) {
		if (action_row[0][i] < 'a' || action_row[0][i] > 'z') {
			action_row[0][i] = '-';
		}
		if (action_row[1][i] < 'a' || action_row[1][i] > 'z') {
			action_row[1][i] = '-';
		}
	}

	action_row[0][ACTION_FLASH] = ' ';
	action_row[0][ACTION_FREEZE] = ' ';
	action_row[0][ACTION_SHIELD] = ' ';
	if (sv_ver == 35) {
		action_row[0][ACTION_BLESS] = ' ';
	}
	action_row[0][ACTION_WARCRY] = ' ';
	action_row[0][ACTION_PULSE] = ' ';
	action_row[0][ACTION_FIRERING] = ' ';
	action_row[0][ACTION_MAP] = ' ';

	action_row[1][ACTION_ATTACK] = ' ';
	action_row[1][ACTION_TAKEGIVE] = ' ';
	action_row[1][ACTION_LOOK] = ' ';
}

int16_t has_action_skill(int i)
{
	return (int16_t)input_action_slot_available(i);
}

int action_key2slot(SDL_Keycode key)
{
	return input_key_to_action_slot(key);
}

void display_action(void) {}

int act_lck = 1;

/* ── Accessors for spellbook ─────────────────────────────────────────── */

const char *get_action_text(int slot)
{
	if (slot < 0 || slot >= MAXACTIONSLOT || !action_text) {
		return NULL;
	}
	return action_text[slot];
}

const char *get_action_desc(int slot)
{
	if (slot < 0 || slot >= MAXACTIONSLOT || !action_desc) {
		return NULL;
	}

	/* Warcry only grants a Life Shield to characters WITHOUT the Magic
	 * Shield skill (warriors); for Seyan'du the shield part of the
	 * description would be a lie */
	if (slot == ACTION_WARCRY && action_skill && action_skill[ACTION_SHIELD] >= 0 &&
	    value[0][action_skill[ACTION_SHIELD]]) {
		return "Slows enemies and might interrupt spellcasting in a fairly wide radius around you.";
	}

	return action_desc[slot];
}

void display_action_lock(void)
{
	act_lck ^= 1;
}

void display_action_open(void)
{
	action_enabled ^= 1;
	save_options();
}

static void display_bar(int sx, int sy, int perc, unsigned short color, int xs, int ys)
{
	perc = perc * ys / 100;
	render_shaded_rect(sx - 1, sy - 1, sx + xs + 1, sy + ys + 1, 0, 120);
	if (perc < 100) {
		render_shaded_rect(sx, sy, sx + xs, sy + ys - perc, IRGB(0, 0, 0), 95);
	}
	if (perc > 0) {
		render_shaded_rect(sx, sy + ys - perc, sx + xs, sy + ys, color, 95);
	}
}

#define WARCRYCOST (12)

static int warcryperccost(void)
{
	if (sv_ver == 35) {
		return 100 * WARCRYCOST / value[0][sv_val(V_ENDURANCE)];
	}

	if (value[0][sv_val(V_ENDURANCE)]) {
		return 100 * value[0][sv_val(V_WARCRY)] / value[0][sv_val(V_ENDURANCE)] / 3 + 1;
	} else {
		return 911;
	}
}

void display_selfbars(void)
{
	int lifep, shieldp, endup, manap;
	if (plrmn == -1) {
		return;
	}
	int x, y;
	int xs = 7, ys = 67, xd = 3;

	if (!(game_options & GO_BIGBAR)) {
		return;
	}

	x = 7;
	y = 47;

	lifep = map[plrmn].health;
	shieldp = map[plrmn].shield;
	manap = map[plrmn].mana;
	if (value[0][sv_val(V_ENDURANCE)]) {
		endup = (int)(100 * endurance / value[0][sv_val(V_ENDURANCE)]);
	} else {
		endup = 100;
	}

	lifep = min(110, lifep);
	shieldp = min(110, shieldp);
	manap = min(110, manap);
	endup = min(110, endup);

	display_bar(x, y, lifep, healthcolor, xs, ys);
	display_bar(x + xs + xd, y, shieldp, shieldcolor, xs, ys);
	if (!value[0][sv_val(V_MANA)]) {
		display_bar(x + xs * 2 + xd * 2, y, endup, endurancecolor, xs, ys);
		if (value[0][sv_val(V_WARCRY)]) {
			int wpc = warcryperccost();
			for (int i = wpc; i < 100; i += wpc) {
				int j;
				j = i * ys / 100;
				if (i < endup) {
					render_line(x + xs * 2 + xd * 2, y + ys - j, x + xs * 3 + xd * 2, y + ys - j, 0x0000);
				} else {
					render_line(x + xs * 2 + xd * 2, y + ys - j, x + xs * 3 + xd * 2, y + ys - j, 0xffff);
				}
			}
		}
	} else {
		display_bar(x + xs * 2 + xd * 2, y, manap, manacolor, xs, ys);
	}
}
