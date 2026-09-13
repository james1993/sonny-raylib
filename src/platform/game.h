/* The running game: which screen is up, the campaign behind it, and the
 * per-battle state the battle screen needs.
 *
 * Screens mirror the original's own menu, which the zone hub lists as Next
 * Battle, Item Store, Inventory, Abilities, Save Game, Options, Quit and World
 * Map (SYSTEM[13..28] in the language table).
 */
#ifndef SONNY_GAME_H
#define SONNY_GAME_H

#include "raylib.h"
#include "../gen/assets_gen.h"
#include "../core/campaign.h"
#include "../core/save.h"

#define STAGE_W   800
#define STAGE_H   575
#define STAGE_FPS 30

#define PLAYER_SLOT   1
#define ABILITY_SLOTS 8
#define LOG_LINES     6
#define RESOLVE_FRAMES 14

typedef enum {
    SCREEN_TITLE = 0,
    SCREEN_START,
    SCREEN_SLOTS,
    SCREEN_CLASS,
    SCREEN_OPTIONS,
    SCREEN_MANUAL,
    SCREEN_SETTINGS,
    SCREEN_LOST,
    SCREEN_GAMEOVER,
    SCREEN_ZONE,
    SCREEN_BATTLE,
    SCREEN_VICTORY,
    SCREEN_TALENTS,
    SCREEN_INVENTORY,
    SCREEN_SHOP,
    SCREEN_MAP
} Screen;

typedef struct {
    Screen    screen;
    Campaign  campaign;

    /* Battle state. */
    Battle           battle;
    const BattleDef *def;
    int32_t   ability_ids[ABILITY_SLOTS];
    /* Krin.abilityCoolDown: how many of the player's own moves each slot has
       still to sit out. Every slot counts down as one of the player's moves
       resolves, and the slot just used is then set to its ability's own
       cooldown. */
    int32_t   ability_cooldown[ABILITY_SLOTS];
    /* Which slot the queued move came from, so its cooldown can be set when
       the move resolves. -1 when nothing is waiting. */
    int32_t   cooldown_slot;
    int32_t   selected;
    int32_t   hovered_unit;
    /* The unit the ability ring is up around. It follows the pointer onto a
       unit and stays while the pointer is anywhere within the ring, which is
       what lets the pointer leave the unit for one of the orbs. */
    int32_t   ring_unit;
    int32_t   queued;
    int32_t   resolve_timer;
    int32_t   anim_tick;
    MoveEvent last;
    int32_t   has_last;
    const char *effect;
    int32_t   effect_slot;
    int32_t   effect_tick;

    /* Floating damage numbers, spawned on the unit they landed on. */
    struct {
        char    text[16];
        float   x, y;
        int32_t life;
        int32_t crit;
        Color   color;
    } numbers[12];
    int32_t number_count;
    char      log[LOG_LINES][128];
    int32_t   log_count;

    /* Battle dialogue. turn_counter is the original's turnTimeKKK (completed
       turns) and speech_seq its within-turn counter; a line holds for its own
       duration and the fight waits while one is showing. */
    int32_t   turn_counter;
    int32_t   speech_seq;
    int32_t   speech_index;
    int32_t   speech_timer;
    const Speech *speech;

    /* Rewards from the battle just won. */
    BattleRewards rewards;
    int32_t   dropped[SONNY_MAX_DROPPED];
    int32_t   dropped_count;
    int32_t   taken[SONNY_MAX_DROPPED];

    /* Menu/screen interaction. */
    int32_t   hovered_item;
    int32_t   selected_item;
    /* Whether the last attribute point went while the ability screen has
       been open: the swatches beside the attributes go grey then, and are
       coloured again the next time the screen is opened. */
    int32_t   stat_points_spent;
    /* Which store marker opened the shop screen: its button's character is
       what says which store, as Krin.shopId does in the original. */
    int32_t   shop_button;
    /* Krin.bossFight and Krin.progressFight, set by the marker that started
       the fight: the first picks the boss music and, on a win, counts the
       zone as beaten; the second is what carries progress forward. */
    int32_t   boss_fight;
    int32_t   progress_fight;
    /* How far through the original's four-track battle playlist we are. */
    int32_t   music_turn;
    /* Which way the slot screen was opened: a new game writes over the slot,
       a load reads it. */
    enum { SLOT_SAVE = 0, SLOT_LOAD } slot_mode;
    /* The settings the original asks for once, before the story starts. */
    struct {
        int32_t sound, graphics, quality, autosave;
    } options;
    int32_t   lost_timer;
    int32_t   gameover_tip;
    /* What the tooltip is showing this frame. The original keeps one of
       these and every button fills it on roll-over. */
    char      tip_title[128];
    char      tip_body[256];
    char      notice[128];
    int32_t   notice_timer;

    Rng       rng;
    uint64_t  seed;
} Game;

/* Text, drawn in the game's own font.
 *
 * The SWF embeds Tahoma, Verdana and Trebuchet MS as subsets; the UI uses
 * Tahoma, so that is what this loads. Every screen draws through ui_text
 * rather than raylib's DrawText, which would use the built-in bitmap font. */
void ui_font_load(void);
void ui_font_unload(void);
/* Where a piece of art goes, given the placement the original recorded: its
   exported canvas carries the piece's own origin inside it, so the top-left
   is the placement point less that origin. */
Rectangle placed_rect(float x, float y, float scale_x, float scale_y,
                      float w, float h, float ox, float oy);
/* One of a screen's text fields, laid out the way the SWF lays it out: its
   own box, alignment, leading, size, colour and face. `clip` is how far the
   clip it belongs to has moved from where the original places it. */
void draw_field(const TextField *f, Vector2 clip, const char *text);
void draw_field_tinted(const TextField *f, Vector2 clip, const char *text,
                       Color colour);
void draw_field_wrapped(const TextField *f, Vector2 clip, const char *text);
/* The graphics of a clip that carries text, which cannot be drawn as one
   picture because the export bakes its fields' design-time copy in. */
void draw_clip_parts(const char *screen, const char *owner, Vector2 moved,
                     const char *chosen, Color tint);

/* One ability orb, put back together from the shared pieces the orb clip is
   made of, centred on `centre` and at the scale the screen places it: a ball,
   the icon masked to it, the glass over that, and -- when `dim` is not zero
   -- the black disc the original lays over an orb that cannot be used, at
   that alpha. */
void draw_orb(const char *icon, Vector2 centre, float scale, int32_t dim,
              int32_t cooldown);
/* The alphas the original gives that disc: eighty-five percent over a move
   the fight will not let the player use, and eighty over a tree node with
   nothing spent on it. */
#define ORB_DIM_RING   217
#define ORB_DIM_TALENT 204
/* A button's own art, which is nowhere else in the file: at rest, or the
   state it swaps in while the pointer is on it. */
void draw_button_art(const StageButton *b, Color tint);
void draw_button_state(const StageButton *b, int over, Color tint);

/* Everything a root frame places that is a picture, in its own depth order,
   and the art the buttons on it carry. Anything the engine fills itself is
   left to the screen. */
void draw_screen_chrome(const char *screen);
void draw_screen_buttons(const char *screen, Vector2 mouse);
/* The text a frame bakes into its own fields, which the game never sets.
   `owner` narrows it to one clip on that screen; NULL takes them all. */
void draw_static_text(const char *screen, const char *owner);
void draw_screen_text(const char *screen);
/* One of a root frame's own text fields, by the name the frame gives it. */
const TextField *chrome_field(const char *screen, const char *name);
/* Whether the pointer has just pressed the button with this character. */
int screen_button_pressed(const char *screen, int32_t character,
                          Vector2 mouse);

/* The box the original parks under the pointer wherever something has a
   name. Screens fill it as the pointer passes over things; it is cleared at
   the top of every frame, so it only shows while something is under the
   pointer. */
void game_tooltip(Game *g, const char *title, const char *body);
void game_draw_tooltip(const Game *g, Vector2 mouse);

void game_draw_notice(const Game *g);
void ui_text(const char *text, float x, float y, float size, Color color);
float ui_text_width(const char *text, float size);
/* The same, in the system sans face the original's "_sans" device-font text
   fields are rendered with. Falls back to the embedded font when the system
   has nothing suitable. */
void ui_sans_text(const char *text, float x, float y, float size, Color color);
float ui_sans_text_width(const char *text, float size);

/* Shared helpers. */
void game_log(Game *g, const char *fmt, ...);
void game_notice(Game *g, const char *fmt, ...);
Vector2 stage_mouse(void);
int hit(Rectangle r, Vector2 p);
void draw_panel(Rectangle r, const char *title);
int draw_button(Rectangle r, const char *label, Vector2 mouse, int enabled);

/* Screens. */
void screen_zone_update(Game *g, Vector2 mouse);
void screen_zone_draw(Game *g, Vector2 mouse);
void screen_talents_update(Game *g, Vector2 mouse);
void screen_talents_draw(Game *g, Vector2 mouse);
void screen_inventory_update(Game *g, Vector2 mouse);
void screen_inventory_draw(Game *g, Vector2 mouse);
void screen_shop_update(Game *g, Vector2 mouse);
void screen_shop_draw(Game *g, Vector2 mouse);
void screen_victory_update(Game *g, Vector2 mouse);
void screen_victory_draw(Game *g, Vector2 mouse);
void screen_map_update(Game *g, Vector2 mouse);
void screen_map_draw(Game *g, Vector2 mouse);
void screen_title_update(Game *g, Vector2 mouse);
void screen_title_draw(Game *g, Vector2 mouse);
void screen_start_update(Game *g, Vector2 mouse);
void screen_start_draw(Game *g, Vector2 mouse);
void screen_slots_update(Game *g, Vector2 mouse);
void screen_slots_draw(Game *g, Vector2 mouse);
void screen_class_update(Game *g, Vector2 mouse);
void screen_class_draw(Game *g, Vector2 mouse);
void screen_options_update(Game *g, Vector2 mouse);
void screen_options_draw(Game *g, Vector2 mouse);
void screen_manual_update(Game *g, Vector2 mouse);
void screen_manual_draw(Game *g, Vector2 mouse);
void screen_lost_update(Game *g, Vector2 mouse);
void screen_lost_draw(Game *g, Vector2 mouse);
void screen_gameover_update(Game *g, Vector2 mouse);
void screen_gameover_draw(Game *g, Vector2 mouse);
void screen_settings_update(Game *g, Vector2 mouse);
void screen_settings_draw(Game *g, Vector2 mouse);

/* Leave the settings for the story, which is where a new game begins. */
void game_begin_story(Game *g);

void battle_screen_start(Game *g, int32_t battle_id);
void battle_screen_update(Game *g, Vector2 mouse, int headless);
void battle_screen_draw(Game *g);

/* Where the player's save lives. */


#endif
