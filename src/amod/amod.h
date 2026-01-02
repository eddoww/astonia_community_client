/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Mod API Header - Include this in standalone mods to access client functionality
 *
 * This header declares all functions and variables that the client exports
 * for mods to use. Link against moac.lib (MSVC) or moac.a (GCC/Clang).
 */

#ifndef AMOD_H
#define AMOD_H

#include "../dll.h"
#include "../astonia.h"
#include "amod_structs.h"
#include <SDL3/SDL_keycode.h>

// ============================================================================
// Mod Callback Functions - Implement these in your mod
// ============================================================================

void amod_init(void);
void amod_exit(void);
char *amod_version(void);
void amod_gamestart(void);
void amod_frame(void);
void amod_tick(void);
void amod_mouse_move(int x, int y);
void amod_update_hover_texts(void);

// Event handlers - return values:
//   1  = event consumed, client and later mods should ignore it
//  -1  = client should ignore, but allow other mods to process
//   0  = event not consumed, continue normal processing
int amod_mouse_click(int x, int y, int what);
int amod_keydown(SDL_Keycode key); // if you catch keydown ...
int amod_keyup(SDL_Keycode key); // ... you must also catch keyup
int amod_client_cmd(const char *buf);

// Main mod only (amod.dll):
int amod_process(const unsigned char *buf); // return length of server command, 0 = unknown
int amod_prefetch(const unsigned char *buf); // return length of server command, 0 = unknown
int amod_display_skill_line(int v, int base, int curr, int cn, char *buf);
int amod_is_playersprite(int sprite);

// ============================================================================
// Client Exported Functions - Logging
// ============================================================================

DLL_IMPORT int note(const char *format, ...) __attribute__((format(printf, 1, 2)));
DLL_IMPORT int warn(const char *format, ...) __attribute__((format(printf, 1, 2)));
DLL_IMPORT int fail(const char *format, ...) __attribute__((format(printf, 1, 2)));
DLL_IMPORT void paranoia(const char *format, ...) __attribute__((format(printf, 1, 2)));
DLL_IMPORT void addline(const char *format, ...) __attribute__((format(printf, 1, 2)));
DLL_IMPORT void cmd_add_text(const char *buf, int typ);

// ============================================================================
// Client Exported Functions - Basic Rendering
// ============================================================================

// Sprite rendering
DLL_IMPORT void render_sprite(unsigned int sprite, int scrx, int scry, char light, char align);
DLL_IMPORT int render_sprite_fx(RenderFX *ddfx, int scrx, int scry);

// Text rendering
DLL_IMPORT int render_text_length(int flags, const char *text);
DLL_IMPORT int render_text(int sx, int sy, unsigned short int color, int flags, const char *text);
DLL_IMPORT int render_text_fmt(int64_t sx, int64_t sy, unsigned short int color, int flags, const char *format, ...)
    __attribute__((format(printf, 5, 6)));
DLL_IMPORT int render_text_nl(int x, int y, unsigned short color, int flags, const char *ptr);
DLL_IMPORT int render_text_break(int x, int y, int breakx, unsigned short color, int flags, const char *ptr);
DLL_IMPORT int render_text_break_fmt(int sx, int sy, int breakx, unsigned short int color, int flags,
    const char *format, ...) __attribute__((format(printf, 6, 7)));
DLL_IMPORT int render_text_break_length(int x, int y, int breakx, unsigned short color, int flags, const char *ptr);

// Basic primitives
DLL_IMPORT void render_pixel(int x, int y, unsigned short col);
DLL_IMPORT void render_rect(int sx, int sy, int ex, int ey, unsigned short int color);
DLL_IMPORT void render_line(int fx, int fy, int tx, int ty, unsigned short col);

// ============================================================================
// Client Exported Functions - Alpha Blending Primitives
// ============================================================================

DLL_IMPORT void render_pixel_alpha(int x, int y, unsigned short col, unsigned char alpha);
DLL_IMPORT void render_rect_alpha(int sx, int sy, int ex, int ey, unsigned short color, unsigned char alpha);
DLL_IMPORT void render_rect_outline_alpha(int sx, int sy, int ex, int ey, unsigned short color, unsigned char alpha);
DLL_IMPORT void render_line_alpha(int fx, int fy, int tx, int ty, unsigned short col, unsigned char alpha);
DLL_IMPORT void render_line_aa(int x0, int y0, int x1, int y1, unsigned short color, unsigned char alpha);
DLL_IMPORT void render_thick_line_alpha(
    int fx, int fy, int tx, int ty, int thickness, unsigned short color, unsigned char alpha);

// Circles and ellipses
DLL_IMPORT void render_circle_alpha(int cx, int cy, int radius, unsigned short color, unsigned char alpha);
DLL_IMPORT void render_circle_filled_alpha(int cx, int cy, int radius, unsigned short color, unsigned char alpha);
DLL_IMPORT void render_ellipse_alpha(int cx, int cy, int rx, int ry, unsigned short color, unsigned char alpha);
DLL_IMPORT void render_ellipse_filled_alpha(int cx, int cy, int rx, int ry, unsigned short color, unsigned char alpha);
DLL_IMPORT void render_ring_alpha(int cx, int cy, int inner_radius, int outer_radius, int start_angle, int end_angle,
    unsigned short color, unsigned char alpha);

// Rounded rectangles
DLL_IMPORT void render_rounded_rect_alpha(
    int sx, int sy, int ex, int ey, int radius, unsigned short color, unsigned char alpha);
DLL_IMPORT void render_rounded_rect_filled_alpha(
    int sx, int sy, int ex, int ey, int radius, unsigned short color, unsigned char alpha);

// Triangles
DLL_IMPORT void render_triangle_alpha(
    int x1, int y1, int x2, int y2, int x3, int y3, unsigned short color, unsigned char alpha);
DLL_IMPORT void render_triangle_filled_alpha(
    int x1, int y1, int x2, int y2, int x3, int y3, unsigned short color, unsigned char alpha);

// Arcs and curves
DLL_IMPORT void render_arc_alpha(
    int cx, int cy, int radius, int start_angle, int end_angle, unsigned short color, unsigned char alpha);
DLL_IMPORT void render_bezier_quadratic_alpha(
    int x0, int y0, int x1, int y1, int x2, int y2, unsigned short color, unsigned char alpha);
DLL_IMPORT void render_bezier_cubic_alpha(
    int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, unsigned short color, unsigned char alpha);

// Gradients
DLL_IMPORT void render_gradient_rect_h(
    int sx, int sy, int ex, int ey, unsigned short color1, unsigned short color2, unsigned char alpha);
DLL_IMPORT void render_gradient_rect_v(
    int sx, int sy, int ex, int ey, unsigned short color1, unsigned short color2, unsigned char alpha);
DLL_IMPORT void render_gradient_circle(
    int cx, int cy, int radius, unsigned short color, unsigned char center_alpha, unsigned char edge_alpha);

// ============================================================================
// Client Exported Functions - Blend Modes
// ============================================================================

// Blend mode constants
#define BLEND_NORMAL   0
#define BLEND_ADDITIVE 1
#define BLEND_MULTIPLY 2
#define BLEND_SCREEN   3

DLL_IMPORT void render_set_blend_mode(int mode);
DLL_IMPORT int render_get_blend_mode(void);

// ============================================================================
// Client Exported Functions - Texture Management
// ============================================================================

DLL_IMPORT int render_load_texture(const char *path);
DLL_IMPORT void render_unload_texture(int tex_id);
DLL_IMPORT void render_texture(int tex_id, int x, int y, unsigned char alpha);
DLL_IMPORT void render_texture_scaled(int tex_id, int x, int y, float scale, unsigned char alpha);
DLL_IMPORT int render_texture_width(int tex_id);
DLL_IMPORT int render_texture_height(int tex_id);

// ============================================================================
// Client Exported Functions - Clipping
// ============================================================================

DLL_IMPORT void render_push_clip(void);
DLL_IMPORT void render_pop_clip(void);
DLL_IMPORT void render_more_clip(int sx, int sy, int ex, int ey);
DLL_IMPORT void render_set_clip(int sx, int sy, int ex, int ey);
DLL_IMPORT void render_clear_clip(void);
DLL_IMPORT void render_get_clip(int *sx, int *sy, int *ex, int *ey);

// ============================================================================
// Client Exported Functions - Screen Effects
// ============================================================================

DLL_IMPORT void render_screen_tint(unsigned short color, unsigned char intensity);
DLL_IMPORT void render_vignette(unsigned char intensity);
DLL_IMPORT void render_screen_flash(unsigned short color, unsigned char intensity);

// ============================================================================
// Client Exported Functions - Render Targets (Off-screen rendering)
// ============================================================================

DLL_IMPORT int render_create_target(int width, int height);
DLL_IMPORT void render_destroy_target(int target_id);
DLL_IMPORT int render_set_target(int target_id);
DLL_IMPORT void render_target_to_screen(int target_id, int x, int y, unsigned char alpha);
DLL_IMPORT void render_clear_target(int target_id);

// ============================================================================
// Client Exported Functions - GUI Helpers
// ============================================================================

DLL_IMPORT int dotx(int didx);
DLL_IMPORT int doty(int didx);
DLL_IMPORT int butx(int bidx);
DLL_IMPORT int buty(int bidx);
DLL_IMPORT size_t get_near_ground(int x, int y);
DLL_IMPORT size_t get_near_item(int x, int y, unsigned int flag, unsigned int looksize);
DLL_IMPORT size_t get_near_char(int x, int y, unsigned int looksize);

// ============================================================================
// Client Exported Functions - Game Logic
// ============================================================================

DLL_IMPORT map_index_t mapmn(unsigned int x, unsigned int y);
DLL_IMPORT void set_teleport(int idx, int x, int y);
DLL_IMPORT int exp2level(int val);
DLL_IMPORT int level2exp(int level);
DLL_IMPORT int raise_cost(int v, int n);
DLL_IMPORT int mil_rank(int exp);

// ============================================================================
// Client Exported Functions - Network
// ============================================================================

DLL_IMPORT void client_send(void *buf, size_t len);

// ============================================================================
// Client Exported Functions - SDL Utilities
// ============================================================================

DLL_IMPORT uint32_t *sdl_load_png(char *filename, int *dx, int *dy);

// ============================================================================
// Client Exported Variables - Display
// ============================================================================

DLL_IMPORT extern int __yres;
DLL_IMPORT extern int sdl_scale;
DLL_IMPORT extern int sdl_frames;
DLL_IMPORT extern int sdl_multi;
DLL_IMPORT extern int sdl_cache_size;
DLL_IMPORT extern int frames_per_second;

// ============================================================================
// Client Exported Variables - Colors (16-bit IRGB format)
// ============================================================================

DLL_IMPORT extern unsigned short int healthcolor, manacolor, endurancecolor, shieldcolor;
DLL_IMPORT extern unsigned short int whitecolor, lightgraycolor, graycolor, darkgraycolor, blackcolor;
DLL_IMPORT extern unsigned short int lightredcolor, redcolor, darkredcolor;
DLL_IMPORT extern unsigned short int lightgreencolor, greencolor, darkgreencolor;
DLL_IMPORT extern unsigned short int lightbluecolor, bluecolor, darkbluecolor;
DLL_IMPORT extern unsigned short int lightorangecolor, orangecolor, darkorangecolor;
DLL_IMPORT extern unsigned short int textcolor;

// ============================================================================
// Client Exported Variables - Input State
// ============================================================================

DLL_IMPORT extern int vk_shift, vk_control, vk_alt;

// ============================================================================
// Client Exported Variables - Game State
// ============================================================================

DLL_IMPORT extern tick_t tick;
DLL_IMPORT extern uint32_t mirror;
DLL_IMPORT extern uint32_t realtime;
DLL_IMPORT extern int protocol_version;

DLL_IMPORT extern uint16_t act;
DLL_IMPORT extern uint16_t actx;
DLL_IMPORT extern uint16_t acty;

DLL_IMPORT extern unsigned int cflags; // current item flags
DLL_IMPORT extern unsigned int csprite; // and sprite

DLL_IMPORT extern uint16_t originx;
DLL_IMPORT extern uint16_t originy;

DLL_IMPORT extern struct map map[MAPDX * MAPDY];
DLL_IMPORT extern struct map map2[MAPDX * MAPDY];

// ============================================================================
// Client Exported Variables - Character Stats
// ============================================================================

DLL_IMPORT extern uint16_t value[2][V_MAX];
DLL_IMPORT extern uint32_t item[INVENTORYSIZE];
DLL_IMPORT extern uint32_t item_flags[INVENTORYSIZE];

DLL_IMPORT extern stat_t hp;
DLL_IMPORT extern stat_t mana;
DLL_IMPORT extern stat_t rage;
DLL_IMPORT extern stat_t endurance;
DLL_IMPORT extern stat_t lifeshield;

DLL_IMPORT extern uint32_t experience;
DLL_IMPORT extern uint32_t experience_used;
DLL_IMPORT extern uint32_t mil_exp;
DLL_IMPORT extern uint32_t gold;

DLL_IMPORT extern int pspeed; // 0=normal 1=fast 2=stealth

// ============================================================================
// Client Exported Variables - Characters
// ============================================================================

DLL_IMPORT extern struct player player[MAXCHARS];
DLL_IMPORT extern union ceffect ceffect[MAXEF];
DLL_IMPORT extern unsigned char ueffect[MAXEF];

// ============================================================================
// Client Exported Variables - Container/Shop
// ============================================================================

DLL_IMPORT extern int con_type;
DLL_IMPORT extern char con_name[80];
DLL_IMPORT extern int con_cnt;
DLL_IMPORT extern uint32_t container[CONTAINERSIZE];
DLL_IMPORT extern uint32_t price[CONTAINERSIZE];
DLL_IMPORT extern uint32_t itemprice[CONTAINERSIZE];
DLL_IMPORT extern uint32_t cprice;

// ============================================================================
// Client Exported Variables - Look/Inspect
// ============================================================================

DLL_IMPORT extern uint32_t lookinv[12];
DLL_IMPORT extern uint32_t looksprite, lookc1, lookc2, lookc3;
DLL_IMPORT extern char look_name[80];
DLL_IMPORT extern char look_desc[1024];

// ============================================================================
// Client Exported Variables - Pentagram
// ============================================================================

DLL_IMPORT extern char pent_str[7][80];

// ============================================================================
// Client Exported Variables - Quests and Shrines
// ============================================================================

DLL_IMPORT extern struct quest quest[MAXQUEST];
DLL_IMPORT extern struct shrine_ppd shrine;

// ============================================================================
// Client Exported Variables - Skills
// ============================================================================

DLL_IMPORT extern int skltab_cnt;
DLL_IMPORT extern SKLTAB *skltab;
DLL_IMPORT extern int weatab[12];

// ============================================================================
// Client Exported Variables - Hover Text
// ============================================================================

DLL_IMPORT extern char hover_bless_text[120];
DLL_IMPORT extern char hover_freeze_text[120];
DLL_IMPORT extern char hover_potion_text[120];
DLL_IMPORT extern char hover_rage_text[120];
DLL_IMPORT extern char hover_level_text[120];
DLL_IMPORT extern char hover_rank_text[120];
DLL_IMPORT extern char hover_time_text[120];

// ============================================================================
// Client Exported Variables - Connection
// ============================================================================

DLL_IMPORT extern char *target_server;
DLL_IMPORT extern char password[16];
DLL_IMPORT extern char username[40];
DLL_IMPORT extern char server_url[256];
DLL_IMPORT extern int server_port;
DLL_IMPORT extern int want_width;
DLL_IMPORT extern int want_height;

// ============================================================================
// Client Exported Variables - Options
// ============================================================================

DLL_IMPORT extern uint64_t game_options;
DLL_IMPORT extern int game_slowdown;

// ============================================================================
// Override-able Functions (also exported from client as _prefixed versions)
// ============================================================================

// Client provides default implementations with _ prefix
DLL_IMPORT int _is_cut_sprite(unsigned int sprite);
DLL_IMPORT int _is_mov_sprite(unsigned int sprite, int itemhint);
DLL_IMPORT int _is_door_sprite(unsigned int sprite);
DLL_IMPORT int _is_yadd_sprite(unsigned int sprite);
DLL_IMPORT int _get_chr_height(unsigned int csprite);
DLL_IMPORT unsigned int _trans_asprite(map_index_t mn, unsigned int sprite, tick_t attick, unsigned char *pscale,
    unsigned char *pcr, unsigned char *pcg, unsigned char *pcb, unsigned char *plight, unsigned char *psat,
    unsigned short *pc1, unsigned short *pc2, unsigned short *pc3, unsigned short *pshine);
DLL_IMPORT int _trans_charno(int csprite, int *pscale, int *pcr, int *pcg, int *pcb, int *plight, int *psat, int *pc1,
    int *pc2, int *pc3, int *pshine, int attick);
DLL_IMPORT int _get_player_sprite(int nr, int zdir, int action, int step, int duration, int attick);
DLL_IMPORT void _trans_csprite(map_index_t mn, struct map *cmap, tick_t attick);
DLL_IMPORT int _get_lay_sprite(int sprite, int lay);
DLL_IMPORT int _get_offset_sprite(int sprite, int *px, int *py);
DLL_IMPORT int _additional_sprite(unsigned int sprite, int attick);
DLL_IMPORT unsigned int _opt_sprite(unsigned int sprite);
DLL_IMPORT int _no_lighting_sprite(unsigned int sprite);
DLL_IMPORT int _get_skltab_sep(int i);
DLL_IMPORT int _get_skltab_index(int n);
DLL_IMPORT int _get_skltab_show(int i);
DLL_IMPORT int _do_display_random(void);
DLL_IMPORT int _do_display_help(int nr);

// Mod can provide these to override client behavior (amod only)
int is_cut_sprite(unsigned int sprite);
int is_mov_sprite(unsigned int sprite, int itemhint);
int is_door_sprite(unsigned int sprite);
int is_yadd_sprite(unsigned int sprite);
int get_chr_height(unsigned int csprite);
unsigned int trans_asprite(map_index_t mn, unsigned int sprite, tick_t attick, unsigned char *pscale,
    unsigned char *pcr, unsigned char *pcg, unsigned char *pcb, unsigned char *plight, unsigned char *psat,
    unsigned short *pc1, unsigned short *pc2, unsigned short *pc3, unsigned short *pshine);
int trans_charno(int csprite, int *pscale, int *pcr, int *pcg, int *pcb, int *plight, int *psat, int *pc1, int *pc2,
    int *pc3, int *pshine, int attick);
int get_player_sprite(int nr, int zdir, int action, int step, int duration, int attick);
void trans_csprite(map_index_t mn, struct map *cmap, tick_t attick);
int get_lay_sprite(int sprite, int lay);
int get_offset_sprite(int sprite, int *px, int *py);
int additional_sprite(unsigned int sprite, int attick);
unsigned int opt_sprite(unsigned int sprite);
int no_lighting_sprite(unsigned int sprite);
int get_skltab_sep(int i);
int get_skltab_index(int n);
int get_skltab_show(int i);
int do_display_random(void);
int do_display_help(int nr);

#endif // AMOD_H
