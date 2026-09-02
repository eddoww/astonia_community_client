/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 */
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_stdinc.h>

#include "../dll.h"
#include "../astonia.h"

/* inventory grid density is a runtime setting (items per row / visible
 * rows); the container grid stays fixed at the classic 4-wide layout */
#define INV_GRID_MIN_COLS  4
#define INV_GRID_MAX_COLS  8
#define INV_GRID_MIN_ROWS  3
#define INV_GRID_MAX_ROWS  6
#define INV_GRID_MAX_SLOTS (INV_GRID_MAX_COLS * INV_GRID_MAX_ROWS)

DLL_EXPORT int inv_grid_cols(void); /* items per row (4..8) */
DLL_EXPORT int inv_grid_rows(void); /* visible rows setting; 0 = auto (classic 4/3) */
DLL_EXPORT void inv_grid_set_cols(int n);
DLL_EXPORT void inv_grid_set_rows(int n);

/* visible rows of the skill list; the button bank holds MAX rows */
#define SKL_GRID_MIN_ROWS 6
#define SKL_GRID_MAX_ROWS 40 /* resize ceiling: room for the whole list */
#define SKL_GRID_DEF_ROWS 16 /* the auto/default height */
#define MINIMAP_D         80 /* minimap circle diameter (minimap.c draws MINIMAP*2 = this) */

/* look-at window geometry: title bar + portrait/description body + the
 * classic 12-slot gear strip along the bottom */
#define LOOK_W          500
#define LOOK_H          168
#define LOOK_STRIP_X    ((LOOK_W - 12 * FDX) / 2)
#define LOOK_PORTRAIT_W 92
#define LOOK_PORTRAIT_H 94
DLL_EXPORT int skl_grid_rows(void); /* setting; 0 = auto (16, or 12 small bottom) */
DLL_EXPORT int skl_grid_rows_effective(void); /* the row count actually drawn */
DLL_EXPORT void skl_grid_set_rows(int n);

/* the shop / grave grid is sized like the inventory one, independently of it */
#define CON_GRID_MIN_COLS  4
#define CON_GRID_MAX_COLS  8
#define CON_GRID_MIN_ROWS  3
#define CON_GRID_MAX_ROWS  6
#define CON_GRID_MAX_SLOTS (CON_GRID_MAX_COLS * CON_GRID_MAX_ROWS)
DLL_EXPORT int con_grid_cols(void);
DLL_EXPORT int con_grid_rows(void); /* setting; 0 = auto (4, or 3 small bottom) */
DLL_EXPORT void con_grid_set_cols(int n);
DLL_EXPORT void con_grid_set_rows(int n);

/* ── Panel content geometry ─────────────────────────────────────────────
 * Shared by dots.c (which lays the panels out), display.c (which draws
 * them) and hover.c (which puts tooltips over them). */
#define INV_RAIL_W         12 /* inventory scrollbar rail column         */
#define INV_RAIL_GAP       3
#define INV_FOOT_H         32 /* purse + trashcan row under the grid     */
#define SKL_RAIL_W         12 /* skill list scrollbar rail column        */
#define WEA_COLS           3 /* equipment paper doll                    */
#define WEA_ROWS           5
#define WEA_FOOT_H         18 /* gear-lock row under the doll            */
#define WEA_BONUS_W        168 /* "Bonuses" column right of the doll     */
#define WEA_BONUS_GAP      6 /* rule + padding between doll and column   */
#define WEA_BONUS_HEAD_H   14 /* column header row                    */
#define WEA_BONUS_ROW_H    LINEHEIGHT
#define WEA_BONUS_LEGEND_H (2 * LINEHEIGHT + 2) /* legend / mod-less note  */
#define WEA_BONUS_MAX_ROWS 24
#define SPEED_SEG_W        46 /* one segment of the speed selector       */
#define SPEED_SEG_H        28
#define SPEED_SEG_GAP      2
#define BUFF_CHIP          28 /* one buff chip                          */
#define BUFF_GAP           3
#define BUFF_LABEL_H       10
#define BUFF_COUNT         4
#define SPB_COLS           7 /* spellbook window columns              */
/* status panel (level + military progress) */
/* status panel: two WoW-style full-width bars at the screen bottom */
#define STAT_BAR_H 11
#define STAT_ROW_H (STAT_BAR_H + 2)
/* the bar strip spans this fraction of the UI width, centered */
#define STAT_W_NUM 3
#define STAT_W_DEN 5
#define STAT_MIN_W 420
/* system menu strip (Menu / Help / Quests)  */
#define SYSM_BTN_W 48
#define SYSM_BTN_H 18
#define SYSM_GAP   3
/* classic flip clock                        */
#define CLK_W 58
#define CLK_H 16

/* number of castable spells the spellbook has cells for (spellbook_ui.c) */
int spellbook_slot_count(void);

/* centre of worn-equipment slot `slot` in the paper doll; 0 when the slot
 * has no cell (dots.c) */
int wea_slot_pos(int slot, int *x, int *y);

#define INVDX      (inv_grid_cols())
#define INVDY      (__invdy)
#define CONDX      (con_grid_cols())
#define CONDY      (__condy)
#define SKLDY      (__skldy)
#define SKLWIDTH   145
#define LINEHEIGHT 10

#define FX_ITEMLIGHT  RENDERFX_NORMAL_LIGHT
#define FX_ITEMBRIGHT RENDERFX_BRIGHT
#define DOTF_TOPOFF   (1 << 0) // dot moves with top bar

#define BUT_MAP     0
#define BUT_WEA_BEG 1
#define BUT_WEA_END 12
#define BUT_INV_BEG 13
#define BUT_INV_END 60 /* INV_GRID_MAX_SLOTS (8×6) button ids: 13..60 */
/* BUT_CON_* moved to the end of the id space - the container grid is a
 * runtime size now and needs more than the classic 16 slots */
#define BUT_SCL_UP 77
#define BUT_SCL_TR 78
#define BUT_SCL_DW 79
#define BUT_SCR_UP 80
#define BUT_SCR_TR 81
#define BUT_SCR_DW 82
/* skill rows moved past the container bank: the classic 83..98 slot gave
 * only 16 rows, the resize ceiling of the skills window (SKL_GRID_MAX_ROWS)
 * needs one button per possible row */
#define BUT_SKL_BEG   (BUT_CON_END + 1)
#define BUT_SKL_END   (BUT_SKL_BEG + SKL_GRID_MAX_ROWS - 1)
#define BUT_GLD       99
#define BUT_JNK       100
#define BUT_MOD_WALK0 101
#define BUT_MOD_WALK1 102
#define BUT_MOD_WALK2 103

#define BUT_TEL        104
#define BUT_HELP_NEXT  105
#define BUT_HELP_PREV  106
#define BUT_HELP_MISC  107
#define BUT_HELP_CLOSE 108
#define BUT_HELP_INDEX 134
#define BUT_EXPBAR     182 /* above BUT_HOTBAR_END (179) */
#define BUT_MILBAR     183
#define BUT_EXIT       109
#define BUT_HELP       110
#define BUT_NOLOOK     111
#define BUT_COLOR      112
#define BUT_SKL_LOOK   113
#define BUT_QUEST      114
#define BUT_HELP_DRAG  115

#define BUT_TEL_MISC 116

#define BUT_ACT_LCK 117
#define BUT_ACT_OPN 118
#define BUT_ACT_BEG 119
#define BUT_ACT_END 132

#define BUT_WEA_LCK 133
// BUT_HELP_INDEX is 134

#define BUT_HOTBAR_BEG 135
#define BUT_HOTBAR_END 179 /* 45 slots (3×15): 135..179 */

/* Per-panel button banks. Each bank has PANEL_BUT_SLOTS consecutive ids in
 * PANEL_* enum order (panels.h): BANK_BEG + PANEL_x is that panel's button.
 * Slots past MAX_PANEL are unused and set BUTF_NOHIT by init_dots(). */
#define PANEL_BUT_SLOTS 13

/* window drag handle / titlebar */
#define BUT_DRAG_BEG    184
#define BUT_DRAG_HOTBAR (BUT_DRAG_BEG + PANEL_HOTBAR)
#define BUT_DRAG_END    (BUT_DRAG_BEG + PANEL_BUT_SLOTS - 1)

/* titlebar close / minimize buttons and the bottom-right resize grip */
#define BUT_PCLOSE_BEG (BUT_DRAG_END + 1)
#define BUT_PCLOSE_END (BUT_PCLOSE_BEG + PANEL_BUT_SLOTS - 1)
#define BUT_PMIN_BEG   (BUT_PCLOSE_END + 1)
#define BUT_PMIN_END   (BUT_PMIN_BEG + PANEL_BUT_SLOTS - 1)
#define BUT_PSIZE_BEG  (BUT_PMIN_END + 1)
#define BUT_PSIZE_END  (BUT_PSIZE_BEG + PANEL_BUT_SLOTS - 1)
#define BUT_PLOCK_BEG  (BUT_PSIZE_END + 1)
#define BUT_PLOCK_END  (BUT_PLOCK_BEG + PANEL_BUT_SLOTS - 1)

/* not a real button: parks butsel while the pointer is over a framed
 * panel's body so the full-screen world underneath is not targeted */
#define BUT_PANEL_BODY (BUT_PLOCK_END + 1)

/* container (shop / grave) grid - sized at runtime like the inventory */
#define BUT_CON_BEG (BUT_PANEL_BODY + 1)
#define BUT_CON_END (BUT_CON_BEG + CON_GRID_MAX_SLOTS - 1)

#define MAX_BUT (BUT_SKL_END + 1) /* keep > the highest BUT_* id */

_Static_assert(
    BUT_INV_END - BUT_INV_BEG + 1 == INV_GRID_MAX_SLOTS, "inventory button range must hold the densest possible grid");
_Static_assert(
    BUT_CON_END - BUT_CON_BEG + 1 == CON_GRID_MAX_SLOTS, "container button range must hold the densest possible grid");
_Static_assert(BUT_MILBAR < MAX_BUT && BUT_EXPBAR < MAX_BUT && BUT_HOTBAR_END < MAX_BUT && BUT_DRAG_END < MAX_BUT,
    "MAX_BUT must exceed every BUT_* id (but[] is indexed by id)");
_Static_assert(
    BUT_EXPBAR > BUT_HOTBAR_END && BUT_MILBAR > BUT_HOTBAR_END, "bar button ids must not fall into the hotbar range");
_Static_assert(BUT_DRAG_BEG > BUT_MILBAR, "drag handle ids must not collide with the bar button ids");
_Static_assert(BUT_DRAG_END < BUT_PCLOSE_BEG && BUT_PCLOSE_END < BUT_PMIN_BEG && BUT_PMIN_END < BUT_PSIZE_BEG &&
                   BUT_PSIZE_END < BUT_PLOCK_BEG && BUT_PLOCK_END < BUT_PANEL_BODY && BUT_PANEL_BODY < MAX_BUT,
    "the per-panel button banks must not overlap");

#define BUTF_NOHIT    (1 << 1) // button is ignored int hit processing
#define BUTF_CAPTURE  (1 << 2) // button captures mouse on lclick
#define BUTF_MOVEEXEC (1 << 3) // button calls cmd_exec(lcmd) on mousemove
#define BUTF_RECT     (1 << 4) // editor - button is a rectangle
#define BUTF_TOPOFF   (1 << 5) // button moves with top bar

#define CMD_RETURN 256
#define CMD_DELETE 257
#define CMD_BACK   258
#define CMD_LEFT   259
#define CMD_RIGHT  260
#define CMD_HOME   261
#define CMD_END    262
#define CMD_UP     263
#define CMD_DOWN   264


#define CMD_NONE     0
#define CMD_MAP_MOVE 1
#define CMD_MAP_DROP 2

#define CMD_ITM_TAKE     3
#define CMD_ITM_USE      4
#define CMD_ITM_USE_WITH 5

#define CMD_CHR_ATTACK 6
#define CMD_CHR_GIVE   7

#define CMD_INV_USE      8
#define CMD_INV_USE_WITH 9
#define CMD_INV_TAKE     10
#define CMD_INV_SWAP     11
#define CMD_INV_DROP     12

#define CMD_WEA_USE      13
#define CMD_WEA_USE_WITH 14
#define CMD_WEA_TAKE     15
#define CMD_WEA_SWAP     16
#define CMD_WEA_DROP     17

#define CMD_CON_TAKE 18
#define CMD_CON_BUY  19
#define CMD_CON_SWAP 20
#define CMD_CON_DROP 21
#define CMD_CON_SELL 22

#define CMD_MAP_LOOK 23
#define CMD_ITM_LOOK 24
#define CMD_CHR_LOOK 25
#define CMD_INV_LOOK 26
#define CMD_WEA_LOOK 27
#define CMD_CON_LOOK 28

#define CMD_MAP_CAST_L 29
#define CMD_ITM_CAST_L 30
#define CMD_CHR_CAST_L 31
#define CMD_MAP_CAST_R 32
#define CMD_ITM_CAST_R 33
#define CMD_CHR_CAST_R 34
#define CMD_MAP_CAST_K 35
#define CMD_CHR_CAST_K 36
#define CMD_SLF_CAST_K 37

// #define CMD_SPL_SET_L           38
// #define CMD_SPL_SET_R           39

#define CMD_SKL_RAISE 40

#define CMD_INV_OFF_UP 41
#define CMD_INV_OFF_DW 42
#define CMD_INV_OFF_TR 43

#define CMD_SKL_OFF_UP 44
#define CMD_SKL_OFF_DW 45
#define CMD_SKL_OFF_TR 46

#define CMD_CON_OFF_UP 47
#define CMD_CON_OFF_DW 48
#define CMD_CON_OFF_TR 49

#define CMD_USE_FKEYITEM 50

#define CMD_SAY_HITSEL 51

#define CMD_DROP_GOLD 52
#define CMD_TAKE_GOLD 53

#define CMD_JUNK_ITEM 54

#define CMD_SPEED0 55
#define CMD_SPEED1 56
#define CMD_SPEED2 57

#define CMD_CON_FASTTAKE 61
#define CMD_CON_FASTBUY  62
#define CMD_CON_FASTSELL 63
#define CMD_TELEPORT     64
#define CMD_CON_FASTDROP 65

#define CMD_HELP_NEXT  66
#define CMD_HELP_PREV  67
#define CMD_HELP_MISC  68
#define CMD_HELP_CLOSE 69
#define CMD_EXIT       70
#define CMD_HELP       71
#define CMD_NOLOOK     72

#define CMD_COLOR     73
#define CMD_SKL_LOOK  74
#define CMD_QUEST     75
#define CMD_HELP_DRAG 76

#define CMD_ACTION        77
#define CMD_ACTION_CANCEL 78

#define CMD_ACTION_LOCK 79
#define CMD_ACTION_OPEN 80

#define CMD_WEAR_LOCK   81
#define CMD_HELP_INDEX  82
#define CMD_EXPBAR      83 /* cycle the numbers printed on the experience bar */
#define CMD_MILBAR      84 /* cycle the numbers printed on the military bar */
#define CMD_DRAG_PANEL  85 /* move the panel whose drag handle captured the mouse */
#define CMD_PANEL_CLOSE 86 /* hide the framed panel whose X was clicked        */
#define CMD_PANEL_MIN   87 /* collapse/expand the framed panel to its titlebar */
#define CMD_PANEL_SIZE  88 /* resize the panel whose grip captured the mouse   */
#define CMD_PANEL_LOCK  89 /* toggle the panel's position lock                 */

#define STV_EMPTYLINE  -1
#define STV_JUSTAVALUE -2 // value is in curr

#define TGT_MAP 1
#define TGT_ITM 2
#define TGT_CHR 3
#define TGT_SLF 4

#define HOVER_DELAY (TICKS / 4)

struct dot {
	int flags;

	int x;
	int y;
};
typedef struct dot DOT;

struct but {
	int flags; // flags

	// int id;         // something an application can give a button, but it need not ;-)
	// int val;        // something an application can give a button, but it need not ;-)

	int x; // center x coordinate - or left if button is a RECT
	int y; // center y coordinate - or top if button is a RECT
	int dx; // width of a rect button
	int dy; // height of a rect button

	int sqhitrad; // hit (square) radius of this button
};

typedef struct but BUT;

struct skltab {
	int v; // negative v-values indicate a special display (empty lines, negative exp, etc...)
	int button; // show button
	char name[80];
	int base;
	int curr;
	int raisecost;
	int barsize; // positive is blue, negative is red
};

typedef struct skltab SKLTAB;

struct keytab {
	SDL_Keycode keycode;
	SDL_Keycode userdef;
	int vk_item, vk_char, vk_spell;
	char name[40];
	int tgt;
	int cl_spell;
	int skill;
	Uint64 usetime;
};

typedef struct keytab KEYTAB;

struct spell {
	int cl; // id of spell sent to server (0=look/spellmode change)
	char name[40]; // name in text display
};

typedef struct spell SPELL;

#ifndef HAVE_SPECIAL_TAB
#define HAVE_SPECIAL_TAB

struct special_tab {
	char *name;
	int shift_over;
	int control_over;
	int spell, target;
	int req;
};

typedef struct special_tab SPECIAL_TAB;
#endif

extern int gui_topoff;
extern DOT *dot;
extern BUT *but;

extern int invsel; // index into item
extern int weasel; // index into weatab
extern int consel; // index into item
extern int sklsel;
extern int sklsel2;
extern int butsel; // is always set, if any of the others is set
extern int telsel;
extern int helpsel;
extern int questsel;
extern int colsel;
extern int actsel;
extern int skl_look_sel;

#define ACTION_NONE     -1
#define ACTION_ATTACK   0
#define ACTION_FIREBALL 1
#define ACTION_LBALL    2
#define ACTION_FLASH    3
#define ACTION_FREEZE   4
#define ACTION_SHIELD   5
#define ACTION_BLESS    6
#define ACTION_HEAL     7
#define ACTION_WARCRY   8
#define ACTION_PULSE    9
#define ACTION_FIRERING 10
#define ACTION_TAKEGIVE 11
#define ACTION_MAP      12
#define ACTION_LOOK     13

extern int action_ovr; // action bar overrides other functions

DLL_EXPORT extern int weatab[12];
extern char weaname[12][32];

extern int cur_cursor;
extern int mousex, mousey;
DLL_EXPORT extern int vk_shift, vk_control, vk_alt;
extern int vk_rbut, vk_lbut, shift_override;
extern int mousedx, mousedy;
extern int vk_item, vk_char, vk_spell;

extern int capbut; // the button capturing the mouse

extern int invoff, max_invoff;
extern int conoff, max_conoff;
extern int skloff, max_skloff;
extern int __skldy;
extern int __invdy;
extern int __condy;

extern int fkeyitem[4];

extern int lcmd;
extern int rcmd;

extern uint32_t takegold; // the amout of gold to take

DLL_EXPORT extern SKLTAB *skltab;
extern int skltab_max;
DLL_EXPORT extern int skltab_cnt;

extern KEYTAB *keytab;
extern int max_keytab;

extern int clan_offset;

extern int show_color, show_cur;
extern unsigned short show_color_c[];
extern int show_cx;
extern char hitsel[];
extern int hittype;
extern int act_lck;

// ============================================================================
// Shared variables from gui_core.c
// ============================================================================
extern uint64_t gui_time_misc;
extern int skip, idle, tota, frames;
extern int display_vc;
extern int display_help, display_quest;
extern int playersprite_override;
extern int update_skltab;
extern int show_look;
extern int control_override;
extern int vk_rbut, vk_lbut;
extern int vk_special;
extern Uint64 vk_special_time;
extern struct special_tab *special_tab;
extern int max_special;
extern int plrmn;
extern map_index_t mapsel;
extern map_index_t itmsel;
extern map_index_t chrsel;
extern int last_right_click_invsel;
extern int mapoffx, mapoffy;
extern int mapaddx, mapaddy;
extern int nextframe, nexttick;
extern uint64_t gui_time_network;
extern uint64_t gui_frametime;
extern uint64_t gui_ticktime;
DLL_EXPORT extern int game_slowdown;

// Platform-specific GUI functions
void gui_sdl_draghack(void);

/* non-zero while a pointer gesture (client button or mod surface) owns the
 * mouse - gui_input.c */
int gui_pointer_grabbed(void);

// ============================================================================
// Shared variables from gui_map.c (shared for map coordinate functions)
// ============================================================================
// (mapoffx, mapoffy, mapaddx, mapaddy are already declared above)

// ============================================================================
// Shared variables from gui_buttons.c
// ============================================================================
// (button handling variables)

// ============================================================================
// Shared variables from gui_display.c
// ============================================================================
// (display functions)

// ============================================================================
// Internal function prototypes for cross-module use
// ============================================================================

// From gui_core.c
int gui_keymode(void);
int vk_special_inc(void);
int vk_special_dec(void);

// From gui_inventory.c
void set_invoff(int bymouse, int ny);
void set_invsel(int newinvsel);
void set_skloff(int bymouse, int ny);
void set_skltab(void);
void set_conoff(int bymouse, int ny);
void set_cmd_invsel(void);
void set_cmd_consel(void);
void set_cmd_weasel(void);
void set_weasel(int newweasel);
void set_button_flags(void);
// get_skltab_* functions are function pointers declared in gui.h, not here
int is_fkey_use_item(int invnr);

// From gui_buttons.c
int get_near_button(int x, int y);
void calculate_lcmd_logic(void);
void calculate_rcmd_logic(void);
void handle_special_buttons_logic(void);
void apply_gear_lock_logic(void);
void exec_cmd(int cmd, int param);
void set_cmd_states(void);

// From gui_display.c
void display_helpandquest(void);
void display_wheel(void);
void display(void);
void update_ui_layout(void);

// Help data (loaded from JSON)
#define HELP_TEXT_WIDTH              192
#define HELP_INDEX_COL_WIDTH         100
#define HELP_INDEX_ROW_HEIGHT        10
#define HELP_PAGE_MARGIN_TOP         8
#define HELP_PAGE_MARGIN_BOTTOM      20
#define HELP_INDEX_TITLE_SPACING     10
#define HELP_FAST_HELP_TITLE_SPACING 5
#define HELP_TITLE_SPACING           5
#define HELP_PARAGRAPH_SPACING       10

extern int help_page_count;
extern int help_index_count;

int help_index_page_for_entry(int entry);

// From gui_map.c (already declared in gui.h but repeated here for clarity)
// void set_mapoff(int cx, int cy, int mdx, int mdy);
// void set_mapadd(int dx, int dy);
// int mtos(int m, int o, int a, int *xs, int *ys);
// int stom(int s, int o, int a, int *xm, int *ym);

void dx_copysprite_emerald(int scrx, int scry, int emx, int emy);

void display_wear(void);
void display_wear_bonus_hover(int mx, int my); /* row tooltip, from hover.c */
void display_look(void);
void display_citem(void);
void display_gold(void);
void display_container(void);
void display_inventory(void);
void display_keys(void);
void display_skill(void);
void display_scrollbars(void);
void display_scrollbar_left(void);
void display_scrollbar_right(void);
void display_tutor(void);
int tutor_click(int x, int y);
void display_sysmenu(void);
void display_clock(void);
void display_text(void);
void display_mode(void);
void display_mouseover(void);
void display_selfspells(void);
void display_exp(void);
void display_military(void);
void display_rage(void);
void display_game_special(void);

// hover.c
int16_t tactics2melee(int val);
int16_t tactics2immune(int val);
int16_t tactics2spell(int val);

int do_display_questlog(int nr);
void display_action(void);
void display_selfbars(void);

const char *get_action_text(int slot);
const char *get_action_desc(int slot);
int get_action_cast_id(int slot);

void display_teleport(void);
int get_teleport(int x, int y);

void display_color(void);
int get_color(int x, int y);
void cmd_color(int nr);
void cmd_reset(void);
void cmd_proc(int key);
int cmd_is_active(void);

/* hotbar_ui.c */
int spellbook_over(int mx, int my);

#define NEAR_ITEM    1024
#define NEAR_CHAR    2048
#define NEAR_NOTSELF 4096
map_index_t get_near_ex(int x, int y, unsigned int flags, unsigned int looksize);

DLL_EXPORT size_t get_near_char(int x, int y, unsigned int looksize);
DLL_EXPORT size_t get_near_item(int x, int y, unsigned int flag, unsigned int looksize);
DLL_EXPORT size_t get_near_ground(int x, int y);

int context_open(int mx, int my);
void context_display(int mx, int my);
void context_stop(void);
int context_click(int mx, int my);
int context_key(int key);
void context_keydown(SDL_Keycode key);
void context_activate_action(int action_slot);
int context_execute_action(int action_slot);
int context_execute_action_normal(int action_slot);
void context_keyup(SDL_Keycode key);
int context_key_set(int onoff);
int context_key_isset(void);
int context_key_enabled(void);
int context_key_set_cmd(void);
void context_key_reset(void);
int context_targeting_active(void);
int context_key_click(void);

/* hover.c accessors for hotbar */
const char *hover_get_item_name(int inv_slot);
void hover_request_item_info(int inv_slot);
int hover_render_for_slot(int inv_slot, int anchor_x, int anchor_y);

DLL_EXPORT extern char hover_bless_text[];
DLL_EXPORT extern char hover_freeze_text[];
DLL_EXPORT extern char hover_heal_text[];
DLL_EXPORT extern char hover_potion_text[];
DLL_EXPORT extern char hover_rage_text[];
DLL_EXPORT extern char hover_level_text[];
DLL_EXPORT extern char hover_rank_text[];
DLL_EXPORT extern char hover_time_text[];

int action_key2slot(SDL_Keycode key);
int16_t has_action_skill(int i);
void context_action_enable(int onoff);

void minimap_init(void);
void minimap_reanchor(void);
void minimap_toggle(void);
void minimap_hide(void);
void minimap_zoom_in(void);
void minimap_zoom_out(void);
void minimap_zoom_reset(void);
int minimap_wheel_zoom(int x, int y, int delta);
void display_minimap(void);
void minimap_update(void);
void minimap_display_hover(int x, int y);
void dots_update(void);
void display_action_lock(void);
void display_action_open(void);
void display_wear_lock(void);
DLL_EXPORT void cmd_add_text(const char *buf, int typ);
