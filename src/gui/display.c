/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Display Windows
 *
 * Equipment, inventory, text, ... windows.
 */

#include <stdint.h>
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

	render_sprite(opt_sprite(994), dotx(DOT_LOK), doty(DOT_LOK), RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_NORMAL);

	/* the look window keeps the classic 12-wide strip - it is a fixed-size
	 * sprite window, not the paper doll */
	for (b = BUT_WEA_BEG; b <= BUT_WEA_END; b++) {
		int i = b - BUT_WEA_BEG;
		int x = dotx(DOT_LOK) + 30 + i * FDX;
		int y = doty(DOT_LOK) + 20;

		render_sprite(opt_sprite(SPR_ITPAD), x, y, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_CENTER);
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
	render_text(dotx(DOT_LOK) + 70, doty(DOT_LOK) + 50, 0xffff, RENDER_TEXT_LEFT, look_name);
	render_text_break(dotx(DOT_LOK) + 70, doty(DOT_LOK) + 60, dotx(DOT_LOK) + 270, 0xffff, RENDER_TEXT_LEFT, look_desc);

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
		render_sprite_fx(&fx, dotx(DOT_LOK) + 40, doty(DOT_LOK) + 110);
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

	/* the container's name lives in the skills window's title bar
	 * (panel_title()), so no separate header plate is drawn here */
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

	if (capbut == -1) {
		x = mousex;
		y = mousey;
	} else {
		return;
	}

	if (x < 0 || x >= XRES) {
		return;
	}
	if (y < 0 || y >= YRES) {
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
	render_more_clip(0, 0, XRES, YRES);
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
	draw_scroll_rail(BUT_SCL_UP, BUT_SCL_TR, BUT_SCL_DW, con_cnt ? max_conoff : max_skloff);
}

void display_scrollbar_right(void)
{
	draw_scroll_rail(BUT_SCR_UP, BUT_SCR_TR, BUT_SCR_DW, max_invoff);
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

/* Blit art columns [art_x0, art_x0 + (sx1-sx0)) of a chrome bar sprite to
 * screen [sx0, sx1) at row by. */
static void draw_bar_region(unsigned int sprite, int by, int sx0, int sx1, int art_x0)
{
	render_push_clip();
	render_more_clip(sx0, by, sx1, by + 200);
	render_sprite(sprite, sx0 - art_x0, by, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_NORMAL);
	render_pop_clip();
}

/* Top bar (999/309) on a wider canvas. The art is drawn for 800px: left
 * ornament [0,160) carries the experience/military bars, the right section
 * [640,800) the gear lock, menu and clock. The middle used to hold twelve
 * chain plates for the worn-equipment strip; the equipment is its own paper
 * doll window now, so the gap is filled with the plate-free rock between the
 * rails (art columns 212-228) all the way across. */
static void render_top_bar(unsigned int sprite, int bx, int by)
{
	int right_x = bx + XRES - 160;
	int x, e;

	draw_bar_region(sprite, by, bx, bx + 160, 0); /* ornament + exp bars */
	draw_bar_region(sprite, by, right_x, bx + XRES, 640); /* menu + clock  */

	for (x = bx + 160; x < right_x; x += 16) {
		e = (x + 16 < right_x) ? x + 16 : right_x;
		draw_bar_region(sprite, by, x, e, 212);
	}
}

static void trans_date(int t, int *phour, int *pmin)
{
	if (pmin) {
		*pmin = (t / MINLEN) % 60;
	}
	if (phour) {
		*phour = (t / HOURLEN) % 24;
	}
}

void display_screen(void)
{
	int h, m;
	int h1, h2, m1, m2;
	static int rh1 = 0, rh2 = 0, rm1 = 0, rm2 = 0;

	/* use "Menu" sprite variant (309) when in game, original "Exit" (999) otherwise */
	render_top_bar(opt_sprite((sockstate >= 4) ? 309 : 999), dotx(DOT_TOP), doty(DOT_TOP));

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

	render_sprite((unsigned int)(200 + rh1), dotx(DOT_TOP) + XRES - XRES0 + 730 + 0 * 10 - 2, doty(DOT_TOP) + 5 + 3,
	    RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_NORMAL);
	render_sprite((unsigned int)(200 + rh2), dotx(DOT_TOP) + XRES - XRES0 + 730 + 1 * 10 - 2, doty(DOT_TOP) + 5 + 3,
	    RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_NORMAL);
	render_sprite((unsigned int)(200 + rm1), dotx(DOT_TOP) + XRES - XRES0 + 734 + 2 * 10 - 2, doty(DOT_TOP) + 5 + 3,
	    RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_NORMAL);
	render_sprite((unsigned int)(200 + rm2), dotx(DOT_TOP) + XRES - XRES0 + 734 + 3 * 10 - 2, doty(DOT_TOP) + 5 + 3,
	    RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_NORMAL);

	sprintf(hover_time_text, "%02d:%02d Astonia Standard Time", h, m);

	/* The bottom bar art is gone: skills, chat, inventory, speed and the
	 * buff chips each draw their own frame now (panels_display_frames()),
	 * so a single fixed slab of rock across the bottom would only fight
	 * them - and it could not follow a panel that had been dragged away.
	 */
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

	display_cmd();
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
 * One socket per tracked effect (potion, heal/freeze, bless, rage). An
 * active effect gets a colored wash, a ring that unwinds clockwise as the
 * time runs out and the seconds printed underneath; an idle one is a dim
 * empty socket. Nearly-expired effects pulse, like the old sliding bar did. */
static void buff_chip(int idx, const char *tag, unsigned short color, int active, int pct, const char *sub)
{
	int x1 = dotx(DOT_SSP) + idx * (BUFF_CHIP + BUFF_GAP);
	int y1 = doty(DOT_SSP);
	int x2 = x1 + BUFF_CHIP, y2 = y1 + BUFF_CHIP;
	int cx = (x1 + x2) / 2, cy = (y1 + y2) / 2;
	int r = BUFF_CHIP / 2 - 1;
	int urgent;

	if (pct < 0) {
		pct = 0;
	}
	if (pct > 100) {
		pct = 100;
	}
	urgent = active && pct <= 15 && (tick & 8);

	/* socket */
	render_rounded_rect_filled_alpha(x1, y1, x2, y2, UI_R_ROW, UI_BG_SUNKEN, UI_A_SOCKET);
	render_gradient_rect_v(x1 + 1, y1 + 1, x2 - 1, cy, UI_BG_RAISED, UI_BG_SUNKEN, 150);

	if (active) {
		/* colored wash, brighter the fuller the effect still is */
		render_rounded_rect_filled_alpha(
		    x1 + 1, y1 + 1, x2 - 1, y2 - 1, UI_R_ROW, color, (unsigned char)(40 + 70 * pct / 100));
		/* remaining-time ring: a full circle at 100%, unwinding clockwise */
		render_ring_alpha(cx, cy, r - 2, r, -90, -90 + 360 * pct / 100, color, urgent ? 255 : 220);
		render_rounded_rect_alpha(x1, y1, x2, y2, UI_R_ROW, color, urgent ? 255 : UI_A_BORDER_HOV);
	} else {
		render_rounded_rect_alpha(x1, y1, x2, y2, UI_R_ROW, UI_BORDER, UI_A_BORDER_REST);
	}

	render_text(cx, cy - 5, active ? UI_TEXT : UI_TEXT_DISABLED, UI_FONT_CENTER, tag);
	if (active && sub && *sub) {
		render_text(cx, y2 + 1, urgent ? UI_TEXT_ERROR : UI_TEXT_MUTED, UI_FONT_CENTER, sub);
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

static void draw_bar_info(int mode, int y, long long have, long long need, long long togo)
{
	char a[32], b[32], text[64];
	int pct = need > 0 ? (int)(100 * have / need) : 0;

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
		return;
	}
	render_text(dotx(DOT_TOP) + 31 + 50, y, IRGB(31, 31, 31),
	    RENDER_TEXT_SMALL | RENDER_TEXT_FRAMED | RENDER_ALIGN_CENTER, text);
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

		render_push_clip();
		render_more_clip(0, 0, dotx(DOT_TOP) + 31 + 100 - (int)(100ll * step / total), doty(DOT_TOP) + 8 + 7);
		render_sprite(996, dotx(DOT_TOP) + 31, doty(DOT_TOP) + 7, exp_ticker ? RENDERFX_BRIGHT : RENDERFX_NORMAL_LIGHT,
		    RENDER_ALIGN_NORMAL);
		render_pop_clip();

		if (exp_ticker) {
			exp_ticker--;
		}

		/* below the bar (the bar itself spans +7..+15) - players found
		 * text drawn over the bar hard to read */
		draw_bar_info(exp_info_mode, doty(DOT_TOP) + 15, have, total, step);

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

	if (mil_exp && total) {
		if (rank < maxrank) {
			render_push_clip();
			render_more_clip(0, 0, dotx(DOT_TOP) + 31 + 100 * step / total, doty(DOT_TOP) + 8 + 24);
			render_sprite(993, dotx(DOT_TOP) + 31, doty(DOT_TOP) + 24, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_NORMAL);
			render_pop_clip();

			/* below the bar (the bar itself spans +24..+32) */
			draw_bar_info(mil_info_mode, doty(DOT_TOP) + 32, step, total, total - step);

			snprintf(hover_rank_text, 200,
			    "Rank %d '%s' to %d '%s': %s / %s (%d%%)\n%s to go, total %s military points", rank,
			    game_rankname[rank], rank + 1, game_rankname[rank + 1], fmt_thousands(step, n1, sizeof(n1)),
			    fmt_thousands(total, n2, sizeof(n2)), 100 * step / total, fmt_thousands(total - step, n3, sizeof(n3)),
			    fmt_thousands((long long)mil_exp, n4, sizeof(n4)));
		} else {
			/* Highest rank: full bar */
			render_sprite(993, dotx(DOT_TOP) + 31, doty(DOT_TOP) + 24, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_NORMAL);
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
		render_sprite(50475, XRES - 37, 62, 14, 0);
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
		render_sprite(50476, XRES - 100, 456, 14, 0);
		break;
	case 15:
		render_sprite(50476, XRES - 59, 456, 14, 0);
		break;

	case 16:
		render_sprite(50476, 353, 203, 14, 0);
		break;

	case 17:
		render_sprite(50473, XRES - 78, 382, 14, 0);
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

	/* below the top bar - with the fullscreen world view DOT_MTL is the
	 * screen corner, which the top bar art covers */
	x = dotx(DOT_MTL) + 7;
	y = max(doty(DOT_MTL), 40) + 7;

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
