/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Graphical User Interface - Display rendering functions
 *
 */

#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <SDL3/SDL.h>

#include "astonia.h"
#include "gui/gui.h"
#include "gui/loading_ui.h"
#include "gui/gui_private.h"
#include "gui/input_bind.h"
#include "gui/panels.h"
#include "gui/ui_tokens.h"
#include "gui/spellbook_ui.h"
#include "gui/keybind_ui.h"
#include "gui/keybind_settings_ui.h"
#include "gui/escape_menu_ui.h"
#include "gui/options_ui.h"
#include "client/client.h"
#include "game/game.h"
#include "sdl/sdl.h"
#include "modder/modder.h"
#include "lib/cjson/cJSON.h"

/* Item sprites are drawn centred in 40px cells but are often taller than
 * that, so a grid's top row spills over the window's title bar and its edge
 * columns past the frame. Clip a panel's content to the frame it lives in. */
static void panel_clip_begin(int p)
{
	int x1, y1, x2, y2;

	render_push_clip();
	if (panel_frame_rect(p, &x1, &y1, &x2, &y2)) {
		y1 += (panel_frame_kind(p) == PANEL_FRAME_WINDOW) ? UI_WIN_TITLE_H : HUD_GRIP_H;
		render_more_clip(x1 + 1, y1, x2 - 1, y2 - 1);
	}
}

static void panel_clip_end(void)
{
	render_pop_clip();
}

/* ── help / quest-log navigation bar ─────────────────────────────────
 * Prev | page n / N | Index | Next along the bottom of the window. One
 * geometry for the renderer and the hover test (gui_buttons.c). */
#define HELP_NAV_H 13
#define HELP_NAV_W 36
#define MAXQUEST2  10 /* quest-log pages, as in gui_core.c */

int help_nav_rect(int which, int *x1, int *y1, int *x2, int *y2)
{
	int left = dotx(DOT_HLP) + 4, right = dotx(DOT_HL2) - 4;
	int by = doty(DOT_HL2) - HELP_NAV_H - 3;

	if (!display_help && !display_quest) {
		return 0;
	}
	*y1 = by;
	*y2 = by + HELP_NAV_H;
	switch (which) {
	case 0:
		*x1 = left;
		*x2 = left + HELP_NAV_W;
		return 1;
	case 1:
		if (!display_help) {
			return 0; /* the quest log has no index page */
		}
		*x1 = (left + right) / 2 - 22;
		*x2 = (left + right) / 2 + 22;
		return 1;
	case 2:
		*x1 = right - HELP_NAV_W;
		*x2 = right;
		return 1;
	default:
		return 0;
	}
}

static void help_draw_nav(void)
{
	static const char *label[3] = {"< Prev", "Index", "Next >"};
	static const int nav_but[3] = {BUT_HELP_PREV, BUT_HELP_INDEX, BUT_HELP_NEXT};
	int x1, y1, x2, y2, px2 = 0, ix1 = 0;
	char buf[32];

	for (int n = 0; n < 3; n++) {
		int hot;

		if (!help_nav_rect(n, &x1, &y1, &x2, &y2)) {
			continue;
		}
		hot = (butsel == nav_but[n]);
		render_rounded_rect_filled_alpha(x1, y1, x2, y2, UI_R_CHIP, hot ? UI_ACCENT : UI_BG_BASE, hot ? 120 : 200);
		render_rounded_rect_alpha(x1, y1, x2, y2, UI_R_CHIP, UI_BORDER, hot ? UI_A_BORDER_HOV : UI_A_ROW_HOVER);
		render_text(
		    (x1 + x2) / 2, y1 + 2, hot ? UI_TEXT : UI_TEXT_MUTED, RENDER_TEXT_SMALL | RENDER_ALIGN_CENTER, label[n]);
		if (n == 0) {
			px2 = x2;
		}
		if (n == 1) {
			ix1 = x1;
		}
	}

	/* the page counter sits between Prev and Index (or centred for the
	 * quest log, which has no index) */
	if (display_help) {
		snprintf(buf, sizeof(buf), "%d / %d", display_help, help_page_count);
	} else {
		snprintf(buf, sizeof(buf), "%d / %d", display_quest, MAXQUEST2);
	}
	if (help_nav_rect(0, &x1, &y1, &x2, &y2)) {
		int cx = ix1 ? (px2 + ix1) / 2 : (dotx(DOT_HLP) + dotx(DOT_HL2)) / 2;

		render_text(cx, y1 + 2, UI_TEXT_MUTED, RENDER_TEXT_SMALL | RENDER_ALIGN_CENTER, buf);
	}
}

void display_helpandquest(void)
{
	/* the window chrome (frame, title bar, close, minimize) is the help
	 * panel's - drawn by panels_display_frames(); the old paper-scroll
	 * sprites 990/995 are retired */
	if (!panel_content_shown(PANEL_HELP)) {
		return;
	}
	if (display_help) {
		do_display_help(display_help);
	}
	if (display_quest) {
		do_display_questlog(display_quest);
	}
	help_draw_nav();
}

char perf_text[256];

void display_wheel(void)
{
	int i;

	render_push_clip();
	render_more_clip(0, 0, UIXRES, UIYRES);

	if (now - vk_special_time < 2000) {
		int n, panic = 99;

		render_shaded_rect(mousex + 5, mousey - 7 - 20, mousex + 71, mousey + 31, 0x0000, 95);

		for (n = (vk_special + 1) % max_special, i = -1; panic-- && i > -3; n = (n + 1) % max_special) {
			if (!special_tab[n].req || value[0][special_tab[n].req]) {
				render_text(mousex + 9, mousey - 3 + i * 10, graycolor, RENDER_TEXT_LEFT, special_tab[n].name);
				i--;
			}
		}
		render_text(mousex + 9, mousey - 3, whitecolor, RENDER_TEXT_LEFT, special_tab[vk_special].name);

		for (n = (vk_special + max_special - 1) % max_special, i = 1; panic-- && i < 3;
		    n = (n + max_special - 1) % max_special) {
			if (!special_tab[n].req || value[0][special_tab[n].req]) {
				render_text(mousex + 9, mousey - 3 + i * 10, graycolor, RENDER_TEXT_LEFT, special_tab[n].name);
				i++;
			}
		}
	}
	render_pop_clip();
}

void dx_copysprite_emerald(int scrx, int scry, int emx, int emy)
{
	RenderFX ddfx;

	bzero(&ddfx, sizeof(ddfx));
	ddfx.sprite = 37;
	ddfx.align = RENDER_ALIGN_OFFSET;
	ddfx.clipsx = (short)(emx * 10);
	ddfx.clipsy = (short)(emy * 10);
	ddfx.clipex = ddfx.clipsx + 10;
	ddfx.clipey = ddfx.clipsy + 10;
	ddfx.ml = ddfx.ll = ddfx.rl = ddfx.ul = ddfx.dl = RENDERFX_NORMAL_LIGHT;
	ddfx.scale = 100;
	render_sprite_fx(&ddfx, scrx - ddfx.clipsx - 5, scry - ddfx.clipsy - 5);
}

size_t get_memory_usage(void);

/* "Loading world" phase: right after login the first map tick enqueues hundreds of
 * sprites for the background texture workers. Drawing the map immediately would make
 * the render thread wait for each of them (several seconds of apparent freeze), so we
 * show a progress screen until the initial burst is done (or a timeout elapses). */
static int world_loading;
static Uint64 world_loading_start;

static Uint64 world_loading_ready;

/* Called when the login has been sent (sockstate 3). Everything the texture workers get
 * from here on - including the first map tick, which arrives before login_done - counts
 * towards the progress bar, and the map is not drawn until that burst is processed. */
void world_loading_begin(void)
{
	world_loading = 1;
	world_loading_start = SDL_GetTicks();
	world_loading_ready = 0;
	sdl_tex_jobs_mark();
}

static int world_loading_active(void)
{
	int done = 0, total = 0;
	Uint64 now, since_ready;

	if (!world_loading) {
		return 0;
	}
	if (sockstate < 3) { /* connection dropped while loading */
		world_loading = 0;
		return 0;
	}
	if (sockstate < 4) {
		return 1; /* still logging in */
	}
	now = SDL_GetTicks();
	if (!world_loading_ready) {
		world_loading_ready = now;
	}
	since_ready = now - world_loading_ready;
	sdl_tex_jobs_progress(&done, &total);
	if ((since_ready > 300 && done >= total) || since_ready > 8000) {
		note("world loaded: %d/%d textures prepared, %u ms after login (%u ms after connect)", done, total,
		    (unsigned)since_ready, (unsigned)(now - world_loading_start));
		world_loading = 0;
		return 0;
	}
	return 1;
}

/* 1 while any loading screen (startup or area change) is covering the game.
 * The GUI proper is not usable then: input handling and the mod overlay
 * check this to stay out of the way until the world is really there. */
int gui_is_loading(void)
{
	return loading_active() || world_loading;
}

/* The escape menu (and the windows reachable from it) are the one part of
 * the GUI that must work on the loading screens too, so the player can
 * adjust options or exit while the world is still loading. */
static void display_menu_overlays(void)
{
	keybind_settings_display();
	options_display();
	escape_menu_display();
}

static void display_world_loading(void)
{
	int done = 0, total = 0, pct = 0;
	int cx = UIXRES / 2, cy = (UIYRES - 60) / 2;
	int bw = 220, bh = 8, bx = cx - bw / 2, by = cy + 10;
	Uint64 elapsed = SDL_GetTicks() - world_loading_start;

	sdl_tex_jobs_progress(&done, &total);
	if (total > 0) {
		pct = done * 100 / total;
	}
	render_rect(0, 0, UIXRES, UIYRES - 60, blackcolor);
	render_sprite(60, UIXRES / 2, ((UIYRES - 60) - 240) / 2, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_CENTER);
	render_text(
	    cx, cy - 14, textcolor, RENDER_TEXT_SMALL | RENDER_ALIGN_CENTER | RENDER_TEXT_FRAMED, "Loading world...");
	render_rounded_rect_filled_alpha(bx, by, bx + bw, by + bh, 3, IRGB(4, 4, 4), 230);
	if (total > 0) {
		render_rounded_rect_filled_alpha(bx, by, bx + bw * pct / 100, by + bh, 3, IRGB(28, 22, 10), 240);
	} else {
		/* indeterminate: sweep a short bar */
		int sw = bw / 4, sx = bx + (int)((elapsed / 8) % (Uint64)(bw - sw));
		render_rounded_rect_filled_alpha(sx, by, sx + sw, by + bh, 3, IRGB(28, 22, 10), 240);
	}
	render_rounded_rect_alpha(bx, by, bx + bw, by + bh, 3, IRGB(18, 16, 12), 200);
	if (total > 0) {
		render_text_fmt(cx, by + bh + 6, textcolor, RENDER_TEXT_SMALL | RENDER_ALIGN_CENTER | RENDER_TEXT_FRAMED,
		    "%d%% (%d / %d sprites)", pct, done, total);
	}
}

void display(void)
{
	extern long long sdl_time_make, sdl_time_tex, sdl_time_tex_main, sdl_time_text, sdl_time_blit;
	time_t t;
	int tmp;
	uint64_t start = SDL_GetTicks();

#if 0
	// Performance for stuff happening during the actual tick only.
	// So zero them now after preload is done.
	sdl_time_make=0;
	sdl_time_tex=0;
	sdl_time_text=0;
	sdl_time_blit=0;
#endif

	/* a live drag keeps its real pointer position, even outside the window */
	if (!gui_pointer_grabbed() && (tmp = sdl_check_mouse())) {
		mousex = -1;
		if (tmp == -1) {
			mousey = 0;
		} else {
			mousey = UIYRES / 2;
		}
	}

	set_cmd_states();

	/* Startup: one loading screen from window creation until the world is ready */
	if (loading_active()) {
		render_ui_layer_begin();
		if (sockstate >= 4) {
			int ld = 0, lt = 0;
			sdl_tex_jobs_progress(&ld, &lt);
			loading_progress(ld, lt);
		}
		if (sockstate >= 3 && !world_loading_active()) {
			loading_finish();
		} else {
			loading_display();
			display_menu_overlays();
			goto display_graphs;
		}
	}

	/* Later (re)connects / area changes: compact world-loading screen */
	if (sockstate >= 3 && world_loading_active()) {
		render_ui_layer_begin();
		display_world_loading();
		display_text();
		display_menu_overlays();
		goto display_graphs;
	}

	if (sockstate < 4 && ((t = time(NULL) - (time_t)socktimeout) > 10 || !originx)) {
		render_ui_layer_begin();
		render_rect(0, 0, UIXRES, UIYRES - 60, blackcolor);
		display_text();
		if ((now / 1000) & 1) {
			render_text(
			    UIXRES / 2, (UIYRES - 60) / 2 - 60, redcolor, RENDER_ALIGN_CENTER | RENDER_TEXT_LARGE, "not connected");
		}
		render_sprite(60, UIXRES / 2, ((UIYRES - 60) - 240) / 2, RENDERFX_NORMAL_LIGHT, RENDER_ALIGN_CENTER);
		if (!kicked_out) {
			render_text_fmt(UIXRES / 2, (UIYRES - 60) / 2 - 40, textcolor,
			    RENDER_TEXT_SMALL | RENDER_ALIGN_CENTER | RENDER_TEXT_FRAMED,
			    "Trying to establish connection. %ld seconds...", (long)t);
			if (t > 15) {
				render_text_fmt(UIXRES / 2, (UIYRES - 60) / 2 - 0, textcolor,
				    RENDER_TEXT_LARGE | RENDER_ALIGN_CENTER | RENDER_TEXT_FRAMED,
				    "Please check %s for troubleshooting advice.", game_url);
			}
		} else {
			/* the server ended the session (kick, shutdown, idle ...): say why */
			const char *why = loading_last_exit_reason();
			if (why[0]) {
				render_text(UIXRES / 2, (UIYRES - 60) / 2 - 40, redcolor,
				    RENDER_TEXT_SMALL | RENDER_ALIGN_CENTER | RENDER_TEXT_FRAMED, why);
			}
			render_text(UIXRES / 2, (UIYRES - 60) / 2 - 24, textcolor,
			    RENDER_TEXT_SMALL | RENDER_ALIGN_CENTER | RENDER_TEXT_FRAMED,
			    "The connection was closed by the server. Close the game and start it again from the launcher.");
		}
		render_text(UIXRES / 2, (UIYRES - 60) / 2 + 30, textcolor,
		    RENDER_TEXT_SMALL | RENDER_ALIGN_CENTER | RENDER_TEXT_FRAMED, "Press Escape for the menu.");
		/* the menu must stay reachable here - players were stuck on this
		 * screen with no way to quit or change settings from inside */
		display_menu_overlays();
		goto display_graphs; // I know, I know. goto considered harmful and all that.
	}

	render_push_clip();
	render_more_clip(0, 0, XRES, YRES); /* the world draws in native dims */
	display_game();
	render_pop_clip();

	/* from here on everything is GUI - draw it on the UI layer so the UI
	 * Scale option can size it independently of the world */
	render_ui_layer_begin();

	/* Two panels are sized by live game state: the skills window swaps its
	 * skill list for a container grid, and the spellbook grows with the
	 * spells the character can cast. Re-lay the panels out on those
	 * transitions instead of padding both for their largest shape. */
	{
		static int con_open = 0;
		static int spell_count = -1;
		static int mm_foot = -1;
		int now_open = (con_cnt != 0);
		int now_spells = spellbook_slot_count();
		int now_foot = minimap_footprint(); /* the minimap flips small <-> big */

		if (now_open != con_open || now_spells != spell_count || now_foot != mm_foot) {
			if (now_open && !con_open) {
				panel_container_opened(); /* a dismissed shop view comes back */
			}
			con_open = now_open;
			spell_count = now_spells;
			mm_foot = now_foot;
			init_dots();
		}
	}

	/* the mod's background layer (the chat) goes under everything that
	 * follows - every client panel and every mod window paints over it */
	amod_frame_background();

	/* window chrome for every shown panel - outside the overlay gate so the
	 * summoned help window keeps its frame while the overlay is hidden
	 * (panel_shown() returns 0 for the HUD panels in that state) */
	panels_display_frames();

	/* GUI chrome - the master overlay toggle hides all of it for an
	 * unobstructed view of the world */
	if (gui_overlay_visible) {
		display_keys();
		if (game_options & GO_WHEEL) {
			display_wheel();
		}
		display_selfbars();

		if (panel_content_shown(PANEL_STATUS)) {
			display_exp();
			display_military();
		}
		if (panel_content_shown(PANEL_SYSMENU)) {
			display_sysmenu();
		}
		if (panel_content_shown(PANEL_CLOCK)) {
			display_clock();
		}
		if (panel_content_shown(PANEL_EQUIPMENT)) {
			panel_clip_begin(PANEL_EQUIPMENT);
			display_wear();
			panel_clip_end();
		}
		if (panel_content_shown(PANEL_SKILLS)) {
			panel_clip_begin(PANEL_SKILLS);
			display_skill();
			if (max_skloff > 0) {
				display_scrollbar_left(); /* everything fits: no rail */
			}
			panel_clip_end();
		}
		if (panel_content_shown(PANEL_CONTAINER)) {
			panel_clip_begin(PANEL_CONTAINER);
			display_container();
			display_scrollbar_container();
			panel_clip_end();
		}
		if (panel_content_shown(PANEL_INVENTORY)) {
			panel_clip_begin(PANEL_INVENTORY);
			display_inventory();
			display_scrollbar_right();
			display_gold();
			panel_clip_end();
		}
		if (panel_content_shown(PANEL_SPEED)) {
			display_mode();
		}
		if (panel_content_shown(PANEL_BUFFS)) {
			display_selfspells();
			display_rage();
		}
		if (panel_content_shown(PANEL_HOTBAR)) {
			hotbar_display();
		}
		if (panel_content_shown(PANEL_SPELLBOOK)) {
			panel_clip_begin(PANEL_SPELLBOOK);
			spellbook_display();
			panel_clip_end();
		}
		if (panel_content_shown(PANEL_MINIMAP)) {
			display_minimap();
		}
		panels_display_handles();
	}

	/* interaction windows and transient overlays only appear on demand, so
	 * they stay usable even with the GUI overlay hidden */
	if (show_look) {
		panel_clip_begin(PANEL_LOOK); /* the description wraps inside the frame */
		display_look();
		panel_clip_end();
	}
	keybind_panel_display();
	display_teleport();
	display_color();
	display_game_special();
	display_tutor();
	display_citem();
	spellbook_display_carry(); /* the carried spell rides the cursor everywhere */
	context_display(mousex, mousey);
	display_helpandquest();
	display_environment_tag();

	display_menu_overlays();

	// Loud, persistent notice when playing without the Ugaris mod (auction house,
	// weather, achievements, info window all live in the mod). The server keeps
	// serving such clients but the experience is incomplete.
	if (sockstate == 4 && !amod_main_loaded()) {
		int by = 60;
		render_text(UIXRES / 2, by, IRGB(31, 0, 0), RENDER_TEXT_LARGE | RENDER_ALIGN_CENTER | RENDER_TEXT_FRAMED,
		    "UNSUPPORTED CLIENT - Ugaris mod not loaded");
		render_text(UIXRES / 2, by + 22, IRGB(31, 31, 0), RENDER_TEXT_SMALL | RENDER_ALIGN_CENTER | RENDER_TEXT_FRAMED,
		    "Weather, auction house, achievements and the info window are unavailable.");
		render_text(UIXRES / 2, by + 36, IRGB(31, 31, 0), RENDER_TEXT_SMALL | RENDER_ALIGN_CENTER | RENDER_TEXT_FRAMED,
		    "Please install Ugaris from Steam (it includes the mod): store.steampowered.com/app/1044010");
	}

	// Display lag warning when no server data received for > 500ms
	// (can be turned off in Options / UI / "Show Lag Warning")
	if (sockstate == 4 && !(game_options & GO_NOLAG) && last_tick_received_time > 0) {
		uint64_t lag_ms = SDL_GetTicks() - last_tick_received_time;
		if (lag_ms > 500) {
			render_text_fmt(UIXRES / 2, 35, IRGB(31, 0, 0),
			    RENDER_TEXT_LARGE | RENDER_ALIGN_CENTER | RENDER_TEXT_FRAMED | RENDER_TEXT_NOCACHE,
			    "LAG: %" PRIu64 "ms", lag_ms);
		}
	}

display_graphs:;

	int64_t duration = (int64_t)(SDL_GetTicks() - start);

	if (display_vc) {
		extern long long texc_miss, texc_pre; // mem_tex,
		extern uint64_t sdl_backgnd_wait, sdl_backgnd_work, sdl_time_preload, sdl_time_load, gui_time_network;
		extern uint64_t gui_frametime, gui_ticktime;
		extern uint64_t sdl_time_pre1, sdl_time_pre2, sdl_time_pre3, sdl_time_mutex, sdl_time_alloc, sdl_time_make_main;
		extern int x_offset, y_offset; // pre_2,pre_in,pre_3;
		// static int dur=0,make=0,tex=0,text=0,blit=0,stay=0;
		static int size;
		static unsigned char dur_graph[100], size1_graph[100], size2_graph[100],
		    size3_graph[100]; //,size_graph[100];load_graph[100],
		static unsigned char pre1_graph[100], pre2_graph[100], pre3_graph[100];
		// static int frame_min=99,frame_max=0,frame_step=0;
		// static int tick_min=99,tick_max=0,tick_step=0;
		int px = UIXRES - 110, py = 35;

		// render_text_fmt(px,py+=10,0xffff,RENDER_TEXT_SMALL|RENDER_TEXT_LEFT|RENDER_TEXT_FRAMED|RENDER_TEXT_NOCACHE,"skip
		// %3.0f%%",100.0*skip/tota);
		// render_text_fmt(px,py+=10,0xffff,RENDER_TEXT_SMALL|RENDER_TEXT_LEFT|RENDER_TEXT_FRAMED|RENDER_TEXT_NOCACHE,"idle
		// %3.0f%%",100.0*idle/tota);
		// render_text_fmt(px,py+=10,IRGB(8,31,8),RENDER_TEXT_LEFT|RENDER_TEXT_FRAMED|RENDER_TEXT_NOCACHE,"Tex: %5.2f
		// MB",mem_tex/(1024.0*1024.0));
		render_text_fmt(px, py += 10, IRGB(8, 31, 8), RENDER_TEXT_LEFT | RENDER_TEXT_FRAMED | RENDER_TEXT_NOCACHE,
		    "Mem: %5.2f MB", (double)get_memory_usage() / (1024.0 * 1024.0));

#if 0
	    if (pre_in>=pre_3) size=pre_in-pre_3;
	    else size=16384+pre_in-pre_3;

	    render_text_fmt(px,py+=10,0xffff,RENDER_TEXT_SMALL|RENDER_TEXT_LEFT|RENDER_TEXT_FRAMED|RENDER_TEXT_NOCACHE,"PreC %d",size);
#endif
#if 0
	    extern int pre_in,pre_1,pre_2,pre_3;
	    extern int texc_used;
	    py+=10;
	    render_text_fmt(px,py+=10,IRGB(8,31,8),RENDER_TEXT_LEFT|RENDER_TEXT_FRAMED|RENDER_TEXT_NOCACHE,"PreI %d",pre_in);
	    render_text_fmt(px,py+=10,IRGB(8,31,8),RENDER_TEXT_LEFT|RENDER_TEXT_FRAMED|RENDER_TEXT_NOCACHE,"Pre1 %d",pre_1);
	    render_text_fmt(px,py+=10,IRGB(8,31,8),RENDER_TEXT_LEFT|RENDER_TEXT_FRAMED|RENDER_TEXT_NOCACHE,"Pre2 %d",pre_2);
	    render_text_fmt(px,py+=10,IRGB(8,31,8),RENDER_TEXT_LEFT|RENDER_TEXT_FRAMED|RENDER_TEXT_NOCACHE,"Pre3 %d",pre_3);
	    render_text_fmt(px,py+=10,IRGB(8,31,8),RENDER_TEXT_LEFT|RENDER_TEXT_FRAMED|RENDER_TEXT_NOCACHE,"Used %d",texc_used);
	    render_text_fmt(px,py+=10,IRGB(8,31,8),RENDER_TEXT_LEFT|RENDER_TEXT_FRAMED|RENDER_TEXT_NOCACHE,"Cache %d/%d",sdl_cache_size,MAX_TEXCACHE);
#endif
		// render_text_fmt(px,py+=10,0xffff,RENDER_TEXT_SMALL|RENDER_TEXT_LEFT|RENDER_TEXT_FRAMED|RENDER_TEXT_NOCACHE,"Miss
		// %lld",texc_miss);
		// render_text_fmt(px,py+=10,0xffff,RENDER_TEXT_SMALL|RENDER_TEXT_LEFT|RENDER_TEXT_FRAMED|RENDER_TEXT_NOCACHE,"Prel
		// %lld",texc_pre);

		py += 10;

		{
			uint64_t sum = (uint64_t)duration + gui_time_network;
			size = sum > 42 ? 42 : (int)sum;
		}
		render_text(px, py += 10, IRGB(8, 31, 8), RENDER_TEXT_LEFT | RENDER_TEXT_FRAMED, "Render");
		sdl_bargraph_add(sizeof(dur_graph), dur_graph, size);
		sdl_bargraph(px, py += 40, sizeof(dur_graph), dur_graph, x_offset, y_offset);

#if 0
	    if (gui_frametime<frame_min) frame_min=gui_frametime;
	    if (gui_frametime>frame_max) frame_max=gui_frametime;
	    render_text_fmt(px,py+=10,IRGB(8,31,8),RENDER_TEXT_NOCACHE|RENDER_TEXT_LEFT|RENDER_TEXT_FRAMED,"FT %d %d",frame_min,frame_max);

	    if (gui_ticktime<tick_min) tick_min=gui_ticktime;
	    if (gui_ticktime>tick_max) tick_max=gui_ticktime;
	    render_text_fmt(px,py+=10,IRGB(8,31,8),RENDER_TEXT_NOCACHE|RENDER_TEXT_LEFT|RENDER_TEXT_FRAMED,"TT %d %d",tick_min,tick_max);
#endif
		{
			uint64_t val = gui_frametime / 2;
			size = val > 42 ? 42 : (int)val;
		}
		render_text_fmt(px, py += 10, IRGB(8, 31, 8), RENDER_TEXT_NOCACHE | RENDER_TEXT_LEFT | RENDER_TEXT_FRAMED,
		    "Frametime %" PRId64, gui_frametime);
		sdl_bargraph_add(sizeof(pre2_graph), pre2_graph, size);
		sdl_bargraph(px, py += 40, sizeof(pre2_graph), pre2_graph, x_offset, y_offset);

		{
			uint64_t val = gui_ticktime / 2;
			size = val > 42 ? 42 : (int)val;
		}
		render_text_fmt(px, py += 10, IRGB(8, 31, 8), RENDER_TEXT_NOCACHE | RENDER_TEXT_LEFT | RENDER_TEXT_FRAMED,
		    "Ticktime %" PRId64, gui_ticktime);
		sdl_bargraph_add(sizeof(pre3_graph), pre3_graph, size);
		sdl_bargraph(px, py += 40, sizeof(pre3_graph), pre3_graph, x_offset, y_offset);
#if 0
	    size=gui_time_network;
	    render_text_fmt(px,py+=10,IRGB(8,31,8),RENDER_TEXT_LEFT|RENDER_TEXT_FRAMED,"Network");
	    sdl_bargraph_add(sizeof(pre2_graph),pre2_graph,size<42?size:42);
	    sdl_bargraph(px,py+=40,sizeof(pre2_graph),pre2_graph,x_offset,y_offset);

	    size=sdl_time_pre1;
	    render_text(px,py+=10,IRGB(8,31,8),RENDER_TEXT_LEFT|RENDER_TEXT_FRAMED,"Alloc");
	    sdl_bargraph_add(sizeof(size1_graph),size3_graph,size<42?size:42);
	    sdl_bargraph(px,py+=40,sizeof(size1_graph),size3_graph,x_offset,y_offset);
#endif


		size = (lasttick + q_size) * 2;
		render_text_fmt(px, py += 10, IRGB(8, 31, 8), RENDER_TEXT_FRAMED | RENDER_TEXT_LEFT, "Queue %d", size / 2);
		sdl_bargraph_add(sizeof(pre2_graph), size3_graph, size < 42 ? size : 42);
		sdl_bargraph(px, py += 40, sizeof(pre2_graph), size3_graph, x_offset, y_offset);

		// Tick interval indicator - time between server tick batch arrivals
		{
			static unsigned char lag_graph[100];
			static int was_lagging = 0;
			// Normal tick interval is ~40ms, show warning color if consistently high
			int lag_size = tick_receive_interval > 200 ? 42 : (int)(tick_receive_interval * 42 / 200);
			// Hysteresis to prevent color flicker: red at 120ms, green at 80ms
			if (tick_receive_interval > 120) {
				was_lagging = 1;
			} else if (tick_receive_interval < 80) {
				was_lagging = 0;
			}
			unsigned short lag_color = was_lagging ? IRGB(31, 8, 8) : IRGB(8, 31, 8);
			render_text_fmt(px, py += 10, lag_color, RENDER_TEXT_FRAMED | RENDER_TEXT_LEFT | RENDER_TEXT_NOCACHE,
			    "Tick %" PRIu64 "ms", tick_receive_interval);
			sdl_bargraph_add(sizeof(lag_graph), lag_graph, lag_size);
			sdl_bargraph(px, py += 40, sizeof(lag_graph), lag_graph, x_offset, y_offset);
		}

		{
			uint64_t sum = sdl_time_pre1 + sdl_time_pre3;
			size = sum > 42 ? 42 : (int)sum;
		}
		render_text(px, py += 10, IRGB(8, 31, 8), RENDER_TEXT_LEFT | RENDER_TEXT_FRAMED, "Pre-Main");
		sdl_bargraph_add(sizeof(size1_graph), size2_graph, size);
		sdl_bargraph(px, py += 40, sizeof(size1_graph), size2_graph, x_offset, y_offset);
#if 0

#endif
		if (sdl_multi) {
			uint64_t val = sdl_backgnd_work / (uint64_t)sdl_multi;
			size = val > 42 ? 42 : (int)val;
			render_text_fmt(
			    px, py += 10, IRGB(8, 31, 8), RENDER_TEXT_LEFT | RENDER_TEXT_FRAMED, "Pre-Back (%d)", sdl_multi);
		} else {
			uint64_t val = sdl_time_pre2;
			size = val > 42 ? 42 : (int)val;
			render_text_fmt(px, py += 10, IRGB(8, 31, 8), RENDER_TEXT_LEFT | RENDER_TEXT_FRAMED, "Make");
		}
		sdl_bargraph_add(sizeof(pre1_graph), pre1_graph, size);
		sdl_bargraph(px, py += 40, sizeof(pre1_graph), pre1_graph, x_offset, y_offset);
#if 0
	        render_text_fmt(px,py+=10,IRGB(8,31,8),RENDER_TEXT_SMALL|RENDER_TEXT_LEFT|RENDER_TEXT_FRAMED,"Mutex");
	        sdl_bargraph_add(sizeof(pre2_graph),pre2_graph,sdl_time_mutex/sdl_multi<42?sdl_time_mutex/sdl_multi:42);
	        sdl_bargraph(px,py+=40,sizeof(pre2_graph),pre2_graph,x_offset,y_offset);
#endif
#if 0
	    render_text(px,py+=10,IRGB(8,31,8),RENDER_TEXT_SMALL|RENDER_TEXT_LEFT|RENDER_TEXT_FRAMED,"Pre-Queue Tot");
	    sdl_bargraph_add(sizeof(size_graph),size_graph,size/4<42?size/4:42);
	    sdl_bargraph(px,py+=40,sizeof(size_graph),size_graph,x_offset,y_offset);

	    render_text(px,py+=10,IRGB(8,31,8),RENDER_TEXT_SMALL|RENDER_TEXT_LEFT|RENDER_TEXT_FRAMED,"Pre2");
	    sdl_bargraph_add(sizeof(pre2_graph),pre2_graph,sdl_time_pre2<42?sdl_time_pre2:42);
	    sdl_bargraph(px,py+=40,sizeof(pre2_graph),pre2_graph,x_offset,y_offset);

	    render_text(px,py+=10,IRGB(8,31,8),RENDER_TEXT_SMALL|RENDER_TEXT_LEFT|RENDER_TEXT_FRAMED,"Texture");
	    sdl_bargraph_add(sizeof(pre3_graph),pre3_graph,sdl_time_pre3<42?sdl_time_pre3:42);
	    sdl_bargraph(px,py+=40,sizeof(pre3_graph),pre3_graph,x_offset,y_offset);

#endif
#if 0
	    if (pre_2>=pre_3) size=pre_2-pre_3;
	    else size=16384+pre_2-pre_3;

	    render_text(px,py+=10,IRGB(8,31,8),RENDER_TEXT_SMALL|RENDER_TEXT_LEFT|RENDER_TEXT_FRAMED,"Size Tex");
	    sdl_bargraph_add(sizeof(size3_graph),size3_graph,size/4<42?size/4:42);
	    sdl_bargraph(px,py+=40,sizeof(size3_graph),size3_graph,x_offset,y_offset);


	    if (duration>10 && (!stay || duration>dur)) {
	        dur=duration;
	        make=sdl_time_make;
	        tex=sdl_time_tex;
	        text=sdl_time_text;
	        blit=sdl_time_blit;
	        stay=24*6;
	    }

	    if (stay>0) {
	        stay--;
	        render_text_fmt(px,py+=20,0xffff,RENDER_TEXT_SMALL|RENDER_TEXT_LEFT|RENDER_TEXT_FRAMED|RENDER_TEXT_NOCACHE,"Dur %dms (%.0f%%)",dur,100.0*(make+tex+text+blit)/dur);
	        render_text_fmt(px,py+=10,0xffff,RENDER_TEXT_SMALL|RENDER_TEXT_LEFT|RENDER_TEXT_FRAMED|RENDER_TEXT_NOCACHE,"Make %dms (%.0f%%)",make,100.0*make/dur);
	        render_text_fmt(px,py+=10,0xffff,RENDER_TEXT_SMALL|RENDER_TEXT_LEFT|RENDER_TEXT_FRAMED|RENDER_TEXT_NOCACHE,"Tex %dms (%.0f%%)",tex,100.0*tex/dur);
	        render_text_fmt(px,py+=10,0xffff,RENDER_TEXT_SMALL|RENDER_TEXT_LEFT|RENDER_TEXT_FRAMED|RENDER_TEXT_NOCACHE,"Text %dms (%.0f%%)",text,100.0*text/dur);
	        render_text_fmt(px,py+=10,0xffff,RENDER_TEXT_SMALL|RENDER_TEXT_LEFT|RENDER_TEXT_FRAMED|RENDER_TEXT_NOCACHE,"Blit %dms (%.0f%%)",blit,100.0*blit/dur);
	    }
#endif
		sdl_time_preload = 0;
		sdl_time_make = 0;
		sdl_time_tex = 0;
		sdl_time_text = 0;
		sdl_time_blit = 0;
		sdl_backgnd_work = 0;
		sdl_backgnd_wait = 0;
		sdl_time_load = 0;
		sdl_time_pre1 = 0;
		sdl_time_pre2 = 0;
		sdl_time_pre3 = 0;
		sdl_time_mutex = 0;
		sdl_time_tex_main = 0;
		gui_time_misc = 0;
		sdl_time_alloc = 0;
		texc_miss = 0;
		texc_pre = 0;
		sdl_time_make_main = 0;
		gui_time_network = 0;
#if 0
	    if (SDL_GetTicks()-frame_step>1000) {
	        frame_step=SDL_GetTicks();
	        frame_min=99;
	        frame_max=0;
	    }
	    if (SDL_GetTicks()-tick_step>1000) {
	        tick_step=SDL_GetTicks();
	        tick_min=99;
	        tick_max=0;
	    }
#endif
	} // else render_text_fmt(650,15,0xffff,RENDER_TEXT_SMALL|RENDER_TEXT_FRAMED,"Mirror %d",mirror);

	sprintf(perf_text, "mem usage=%zu/%.2fMB, %d/%dKBlocks", memsize[0] / 1024 / 1024,
	    (double)memused / 1024.0 / 1024.0, memptrs[0] / 1024, memptrused / 1024);
}

// cmd

void update_ui_layout(void)
{
	static int last_con_cnt = 0;

	if (update_skltab) {
		set_skltab();
		update_skltab = 0;
	}
	if (last_con_cnt != con_cnt) {
		conoff = 0;
		/* ceil: with a truncating divide the last, partial row of a shop
		 * could not be scrolled to */
		max_conoff = ((con_cnt + CONDX - 1) / CONDX) - CONDY;
		last_con_cnt = con_cnt;
		set_conoff(0, conoff);
		set_skloff(0, skloff);
	}
	/* round up: with non-multiple-of-INVDX item counts the last row is
	 * partial but must still be reachable */
	max_invoff = ((_inventorysize - 30 + INVDX - 1) / INVDX) - INVDY;
	if (max_invoff < 0) {
		max_invoff = 0;
	}
	if (invoff > max_invoff) {
		invoff = max_invoff; /* a denser grid setting can shrink the range */
	}
	set_button_flags();
}

typedef enum HelpBlockType {
	HELP_BLOCK_TITLE = 0,
	HELP_BLOCK_TEXT = 1,
} HelpBlockType;

typedef struct HelpBlock {
	HelpBlockType type;
	char *text;
} HelpBlock;

typedef struct HelpTopic {
	char *title;
	HelpBlock *blocks;
	int block_count;
} HelpTopic;

static HelpTopic *help_topics = NULL;
static int help_topic_count = 0;
static int *help_topic_pages = NULL;
static char **help_fast_help = NULL;
static int help_fast_help_count = 0;
static char **help_index_titles = NULL;
static int *help_index_pages = NULL;
int help_page_count = 2;
int help_index_count = 0;

static void help_format_text(const char *in, char *out, size_t out_size)
{
	size_t out_len = 0;
	int i;

	struct {
		const char *token;
		const char *value;
	} replacements[] = {
	    {"{game_url}", game_url ? game_url : ""},
	    {"{game_email_cash}", game_email_cash ? game_email_cash : ""},
	    {"{game_email_main}", game_email_main ? game_email_main : ""},
	};

	if (!in || !out || out_size == 0) {
		return;
	}

	for (i = 0; in[i] && out_len + 1 < out_size;) {
		int replaced = 0;
		int r;

		for (r = 0; r < (int)(sizeof(replacements) / sizeof(replacements[0])); r++) {
			size_t token_len = strlen(replacements[r].token);
			if (strncmp(&in[i], replacements[r].token, token_len) == 0) {
				size_t value_len = strlen(replacements[r].value);
				size_t copy_len = min(value_len, out_size - 1 - out_len);
				if (copy_len > 0) {
					memcpy(out + out_len, replacements[r].value, copy_len);
					out_len += copy_len;
				}
				i += (int)token_len;
				replaced = 1;
				break;
			}
		}
		if (!replaced) {
			out[out_len++] = in[i++];
		}
	}

	out[out_len] = '\0';
}

/* ── hyperlinks inside help pages ─────────────────────────────────────
 * Every mention of another topic's title inside a page is a link to that
 * topic: drawn in the link colour, underlined, brighter under the pointer,
 * and remembered here for the click test. The table is rebuilt every frame
 * the page is drawn. Text is laid out word by word so links can be coloured
 * individually; the same routine measures, so pagination stays consistent. */
#define HELP_MAX_LINKS 128
#define HELP_MAX_WORDS 600

static struct {
	int x1, y1, x2, y2, page;
} help_links[HELP_MAX_LINKS];

static int help_link_cnt;

int help_link_page_at(int x, int y)
{
	for (int i = 0; i < help_link_cnt; i++) {
		if (x >= help_links[i].x1 && x <= help_links[i].x2 && y >= help_links[i].y1 && y <= help_links[i].y2) {
			return help_links[i].page;
		}
	}
	return 0;
}

static void help_link_add(int x1, int y1, int x2, int y2, int page)
{
	if (help_link_cnt < HELP_MAX_LINKS) {
		help_links[help_link_cnt].x1 = x1;
		help_links[help_link_cnt].y1 = y1;
		help_links[help_link_cnt].x2 = x2;
		help_links[help_link_cnt].y2 = y2;
		help_links[help_link_cnt].page = page;
		help_link_cnt++;
	}
}

/* word compare, case-insensitive, trailing punctuation on the text word ignored */
static int help_word_eq(const char *w, int wl, const char *t, int tl)
{
	while (wl > 0 && strchr(".,;:!?)\"'", w[wl - 1])) {
		wl--;
	}
	if (wl != tl) {
		return 0;
	}
	for (int i = 0; i < wl; i++) {
		if (tolower((unsigned char)w[i]) != tolower((unsigned char)t[i])) {
			return 0;
		}
	}
	return 1;
}

/* does topic t's title start at word wi? returns the words it spans, 0 = no */
static int help_title_match(int t, const char **words, const int *wlen, int nwords, int wi)
{
	const char *p = help_topics[t].title;
	int k = wi, n = 0;

	while (*p) {
		const char *e;

		while (*p == ' ') {
			p++;
		}
		if (!*p) {
			break;
		}
		e = p;
		while (*e && *e != ' ') {
			e++;
		}
		if (k >= nwords || !help_word_eq(words[k], wlen[k], p, (int)(e - p))) {
			return 0;
		}
		k++;
		n++;
		p = e;
	}
	return n;
}

/* lay a paragraph out between x and right, linking topic titles (never the
 * page's own topic); draw = 0 only measures. Returns the y below the text. */
static int help_rich_text(int x, int y, int right, unsigned short color, const char *text, int self_topic, int draw)
{
	const char *words[HELP_MAX_WORDS];
	int wlen[HELP_MAX_WORDS], nl[HELP_MAX_WORDS];
	int nwords = 0, cx = x, space, link_left = 0, link_page = 0, lines = 0;
	const char *p = text;

	/* tokenize; a newline in the text forces a line break before the word */
	while (*p && nwords < HELP_MAX_WORDS) {
		const char *e;
		int brk = 0;

		while (*p == ' ' || *p == '\n') {
			if (*p == '\n') {
				brk = 1;
			}
			p++;
		}
		if (!*p) {
			break;
		}
		e = p;
		while (*e && *e != ' ' && *e != '\n') {
			e++;
		}
		words[nwords] = p;
		wlen[nwords] = (int)(e - p);
		nl[nwords] = brk;
		nwords++;
		p = e;
	}
	if (!nwords) {
		return y;
	}
	space = render_text_length(0, "n n") - render_text_length(0, "nn");
	if (space < 2) {
		space = 3;
	}
	lines = 1;

	for (int i = 0; i < nwords; i++) {
		char wbuf[128];
		int l = wlen[i] < 127 ? wlen[i] : 127;
		int ww;
		unsigned short c = color;

		memcpy(wbuf, words[i], (size_t)l);
		wbuf[l] = 0;
		ww = render_text_length(0, wbuf);
		if (cx > x && (nl[i] || cx + ww > right)) {
			cx = x;
			y += LINEHEIGHT;
			lines++;
		}
		if (!link_left && draw) {
			for (int t = 0; t < help_topic_count; t++) {
				int n;

				if (t == self_topic || !help_topic_pages) {
					continue;
				}
				n = help_title_match(t, words, wlen, nwords, i);
				if (n) {
					link_left = n;
					link_page = 3 + help_topic_pages[t];
					break;
				}
			}
		}
		if (draw) {
			if (link_left > 0) {
				int hot = mousex >= cx && mousex <= cx + ww && mousey >= y && mousey < y + LINEHEIGHT;

				c = hot ? whitecolor : lightbluecolor;
				help_link_add(cx, y, cx + ww, y + LINEHEIGHT - 1, link_page);
				render_line(cx, y + LINEHEIGHT - 2, cx + ww, y + LINEHEIGHT - 2, c);
				link_left--;
			}
			render_text(cx, y, c, 0, wbuf);
		}
		cx += ww + space;
	}
	(void)lines;
	return y + LINEHEIGHT;
}

static int help_text_height(const char *text, unsigned short color)
{
	char buf[4096];

	help_format_text(text, buf, sizeof(buf));
	return help_rich_text(0, 0, HELP_TEXT_WIDTH, color, buf, -1, 0);
}

static void help_truncate_index_title(const char *text, char *out, size_t out_size, int max_width)
{
	size_t n = 0;
	int ellipsis_width;
	int full_width;
	int truncated = 0;

	if (!text || !out || out_size == 0) {
		return;
	}

	full_width = render_text_length(0, text);
	if (full_width > max_width) {
		truncated = 1;
		ellipsis_width = render_text_length(0, "...");
		if (max_width > ellipsis_width) {
			max_width -= ellipsis_width;
		}
	}

	while (text[n] && n + 1 < out_size) {
		int width = render_text_len(0, text, (int)(n + 1));
		if (width > max_width) {
			break;
		}
		n++;
	}

	memcpy(out, text, n);
	out[n] = '\0';

	if (truncated && n + 3 < out_size) {
		strncat(out, "...", out_size - n - 1);
	}
}

static int help_topic_height(const HelpTopic *topic)
{
	int i;
	int height = 0;

	if (!topic || !topic->title) {
		return 0;
	}

	height += help_rich_text(0, 0, HELP_TEXT_WIDTH, whitecolor, topic->title, -1, 0) + HELP_TITLE_SPACING;

	for (i = 0; i < topic->block_count; i++) {
		unsigned short color = topic->blocks[i].type == HELP_BLOCK_TITLE ? whitecolor : graycolor;
		int spacing = topic->blocks[i].type == HELP_BLOCK_TITLE ? HELP_TITLE_SPACING : HELP_PARAGRAPH_SPACING;

		height += help_text_height(topic->blocks[i].text, color) + spacing;
	}

	return height;
}

static void help_build_pages(void)
{
	int i;
	int start_y = doty(DOT_HLP) + HELP_PAGE_MARGIN_TOP;
	int content_bottom = doty(DOT_HL2) - HELP_PAGE_MARGIN_BOTTOM;
	int y = start_y;
	int page = 0;
	int pages_for_topics = 0;

	if (help_topic_count > 0) {
		help_topic_pages = xmalloc(sizeof(*help_topic_pages) * (size_t)help_topic_count, MEM_GUI);
	}

	for (i = 0; i < help_topic_count; i++) {
		int height = help_topic_height(&help_topics[i]);

		if (y != start_y && y + height > content_bottom) {
			page++;
			y = start_y;
		}

		help_topic_pages[i] = page;
		y += height;
	}

	if (help_topic_count > 0) {
		pages_for_topics = page + 1;
	}

	help_page_count = 2 + pages_for_topics;
	if (help_page_count < 2) {
		help_page_count = 2;
	}

	help_index_count = help_topic_count;
	if (help_index_count > 0) {
		help_index_titles = xmalloc(sizeof(*help_index_titles) * (size_t)help_index_count, MEM_GUI);
		help_index_pages = xmalloc(sizeof(*help_index_pages) * (size_t)help_index_count, MEM_GUI);
		for (i = 0; i < help_topic_count; i++) {
			help_index_titles[i] = help_topics[i].title;
			help_index_pages[i] = 3 + help_topic_pages[i];
		}
	}
}

static int help_load_from_json(const char *json_str, const char *source_name)
{
	cJSON *root = cJSON_Parse(json_str);
	if (!root) {
		warn("help: Failed to parse %s: %s", source_name, cJSON_GetErrorPtr());
		return -1;
	}

	cJSON *fast_help = cJSON_GetObjectItem(root, "fast_help");
	if (fast_help && cJSON_IsArray(fast_help)) {
		int count = cJSON_GetArraySize(fast_help);
		int i;

		if (count > 0) {
			help_fast_help = xmalloc(sizeof(*help_fast_help) * (size_t)count, MEM_GUI);
			help_fast_help_count = 0;
			for (i = 0; i < count; i++) {
				cJSON *item = cJSON_GetArrayItem(fast_help, i);
				if (item && cJSON_IsString(item)) {
					help_fast_help[help_fast_help_count++] = xstrdup(item->valuestring, MEM_GUI);
				}
			}
		}
	}

	cJSON *topics = cJSON_GetObjectItem(root, "topics");
	if (topics && cJSON_IsArray(topics)) {
		int count = cJSON_GetArraySize(topics);
		int i;
		int valid = 0;

		for (i = 0; i < count; i++) {
			cJSON *item = cJSON_GetArrayItem(topics, i);
			cJSON *title = item ? cJSON_GetObjectItem(item, "title") : NULL;
			if (item && cJSON_IsObject(item) && title && cJSON_IsString(title)) {
				valid++;
			}
		}

		if (valid > 0) {
			help_topics = xmalloc(sizeof(*help_topics) * (size_t)valid, MEM_GUI);
			memset(help_topics, 0, sizeof(*help_topics) * (size_t)valid);
			help_topic_count = 0;
			for (i = 0; i < count; i++) {
				cJSON *item = cJSON_GetArrayItem(topics, i);
				cJSON *title = item ? cJSON_GetObjectItem(item, "title") : NULL;
				cJSON *blocks = item ? cJSON_GetObjectItem(item, "blocks") : NULL;
				int block_count = 0;
				int b;

				if (!item || !cJSON_IsObject(item) || !title || !cJSON_IsString(title)) {
					continue;
				}

				help_topics[help_topic_count].title = xstrdup(title->valuestring, MEM_GUI);

				if (blocks && cJSON_IsArray(blocks)) {
					int total = cJSON_GetArraySize(blocks);
					for (b = 0; b < total; b++) {
						cJSON *block = cJSON_GetArrayItem(blocks, b);
						cJSON *text = NULL;
						if (block && cJSON_IsString(block)) {
							text = block;
						} else if (block && cJSON_IsObject(block)) {
							text = cJSON_GetObjectItem(block, "text");
						}
						if (text && cJSON_IsString(text)) {
							block_count++;
						}
					}

					if (block_count > 0) {
						help_topics[help_topic_count].blocks =
						    xmalloc(sizeof(*help_topics[help_topic_count].blocks) * (size_t)block_count, MEM_GUI);
						help_topics[help_topic_count].block_count = 0;
						for (b = 0; b < total; b++) {
							cJSON *block = cJSON_GetArrayItem(blocks, b);
							cJSON *type = NULL;
							cJSON *text = NULL;
							HelpBlockType block_type = HELP_BLOCK_TEXT;

							if (block && cJSON_IsString(block)) {
								text = block;
							} else if (block && cJSON_IsObject(block)) {
								type = cJSON_GetObjectItem(block, "type");
								text = cJSON_GetObjectItem(block, "text");
							}

							if (!text || !cJSON_IsString(text)) {
								continue;
							}

							if (type && cJSON_IsString(type) && strcmp(type->valuestring, "title") == 0) {
								block_type = HELP_BLOCK_TITLE;
							}

							help_topics[help_topic_count].blocks[help_topics[help_topic_count].block_count].type =
							    block_type;
							help_topics[help_topic_count].blocks[help_topics[help_topic_count].block_count].text =
							    xstrdup(text->valuestring, MEM_GUI);
							help_topics[help_topic_count].block_count++;
						}
					}
				}

				help_topic_count++;
			}
		}
	}

	cJSON_Delete(root);

	help_build_pages();
	return 0;
}

static void help_set_fallback(const char *path)
{
	HelpTopic *topic;
	HelpBlock *block;
	char buf[256];

	snprintf(buf, sizeof(buf), "Help data missing: %s", path ? path : "unknown");

	help_topics = xmalloc(sizeof(*help_topics), MEM_GUI);
	memset(help_topics, 0, sizeof(*help_topics));
	help_topic_count = 1;
	topic = &help_topics[0];
	topic->title = xstrdup("Help", MEM_GUI);
	topic->blocks = xmalloc(sizeof(*topic->blocks), MEM_GUI);
	topic->block_count = 1;
	block = &topic->blocks[0];
	block->type = HELP_BLOCK_TEXT;
	block->text = xstrdup(buf, MEM_GUI);

	help_build_pages();
}

void help_init(void)
{
	char path[64];
	char *json;

	snprintf(path, sizeof(path), "res/config/help_v%d.json", sv_ver);
	json = load_ascii_file(path, MEM_TEMP);
	if (!json) {
		warn("help: Failed to read %s", path);
		help_set_fallback(path);
		return;
	}

	if (help_load_from_json(json, path) < 0) {
		help_set_fallback(path);
	}

	xfree(json);
}

int help_index_page_for_entry(int entry)
{
	if (entry < 0 || entry >= help_index_count || !help_index_pages) {
		return 0;
	}
	return help_index_pages[entry];
}

DLL_EXPORT int _do_display_help(int nr)
{
	int x = dotx(DOT_HLP) + 10;
	int y = doty(DOT_HLP) + HELP_PAGE_MARGIN_TOP;
	int content_right = x + HELP_TEXT_WIDTH;
	int content_bottom = doty(DOT_HL2) - HELP_PAGE_MARGIN_BOTTOM;
	int i, b;

	if (nr < 1 || nr > help_page_count) {
		nr = 1;
	}
	help_link_cnt = 0;

	if (nr == 1) {
		y = render_text_break(x, y, content_right, whitecolor, 0, "Fast Help");
		y += HELP_FAST_HELP_TITLE_SPACING;
		for (i = 0; i < help_fast_help_count; i++) {
			char buf[4096];

			help_format_text(help_fast_help[i], buf, sizeof(buf));
			y = help_rich_text(x, y, content_right, graycolor, buf, -1, 1);
		}
		return y;
	}

	if (nr == 2) {
		int start_y;
		int rows;
		int columns = 2;
		int max_entries;
		int visible;

		y = render_text_break(x, y, content_right, whitecolor, 0, "Help Index");
		y += HELP_INDEX_TITLE_SPACING;
		start_y = y;
		rows = (content_bottom - start_y) / HELP_INDEX_ROW_HEIGHT;
		if (rows < 1) {
			rows = 1;
		}
		max_entries = rows * columns;
		visible = min(help_index_count, max_entries);

		for (i = 0; i < visible; i++) {
			int col = i / rows;
			int row = i % rows;
			int tx = x + col * HELP_INDEX_COL_WIDTH;
			int ty = start_y + row * HELP_INDEX_ROW_HEIGHT;
			int hot = mousex >= tx && mousex < tx + HELP_INDEX_COL_WIDTH && mousey >= ty &&
			          mousey < ty + HELP_INDEX_ROW_HEIGHT;
			char label[128];

			help_truncate_index_title(help_index_titles[i], label, sizeof(label), HELP_INDEX_COL_WIDTH - 16);
			render_text(tx, ty, hot ? whitecolor : lightbluecolor, 0, label);
		}
		return y;
	}

	for (i = 0; i < help_topic_count; i++) {
		if (!help_topic_pages || help_topic_pages[i] != nr - 3) {
			continue;
		}

		y = help_rich_text(x, y, content_right, whitecolor, help_topics[i].title, i, 1);
		y += HELP_TITLE_SPACING;

		for (b = 0; b < help_topics[i].block_count; b++) {
			char buf[4096];
			HelpBlock *block = &help_topics[i].blocks[b];
			unsigned short color = block->type == HELP_BLOCK_TITLE ? whitecolor : graycolor;
			int spacing = block->type == HELP_BLOCK_TITLE ? HELP_TITLE_SPACING : HELP_PARAGRAPH_SPACING;

			help_format_text(block->text, buf, sizeof(buf));
			y = help_rich_text(x, y, content_right, color, buf, i, 1);
			y += spacing;
		}
	}

	return y;
}
