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
    SCREEN_ZONE = 0,
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
    int32_t   selected;
    int32_t   hovered_unit;
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

void battle_screen_start(Game *g, int32_t battle_id);
void battle_screen_update(Game *g, Vector2 mouse, int headless);
void battle_screen_draw(Game *g);

/* Where the player's save lives. */
#define SONNY_SAVE_PATH "sonny-save.txt"

#endif
