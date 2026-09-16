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
#include "../core/stagefit.h"

/* The stage the original authored everything in, and how fast it runs. */
#define STAGE_W   SONNY_STAGE_W
#define STAGE_H   SONNY_STAGE_H
#define STAGE_FPS 30

#define PLAYER_SLOT   1
#define ABILITY_SLOTS 8
#define LOG_LINES     6
/* How long one move takes to play out, which is the original's
   AttackEndCounterLimit: twenty-five frames, and the model's animation and
   the ability's effect both run inside it. */
#define RESOLVE_FRAMES 25

typedef enum {
    SCREEN_TITLE = 0,
    SCREEN_START,
    SCREEN_SLOTS,
    SCREEN_CLASS,
    SCREEN_OPTIONS,
    SCREEN_MANUAL,
    SCREEN_SETTINGS,
    SCREEN_CUTSCENE,
    SCREEN_ENDING,
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

/* How long the victory screen takes to fill the experience bar, in frames:
   the original's setLimiter. */
#define WIN_FILL_FRAMES 30

/* How many attribute lines an item's tooltip can stack: the original
   makes ten fields and fills as many as the item has. */
#define TOOLTIP_LINES 10

/* The sign the game prints against every price. raylib bakes printable
   ASCII and nothing else, so this one is asked for by codepoint. */
#define EURO "\u20ac"

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
    /* Frames since the impact graphic was attached. It is its own counter
       because the caster's animation is clocked from when the move began,
       and restarting that clock when the blow lands played it twice. */
    int32_t   effect_tick;
    /* Frames since the move being played started. The model's attack and the
       ability's effect are both played off this, so each starts from its own
       first frame rather than from wherever a free-running counter had got
       to -- which showed nothing but the last frame of every animation. */
    int32_t   move_tick;

    /* GridZoomer: the battlefield leans in on whatever a move is aimed at,
       holds, and comes back out. zoom_point counts the ten steps of a leg
       down to zero, zoom_way is 1 going in and -1 coming out, and the deltas
       are one step's worth at zoom_point 1. */
    int32_t   zoom_point;
    int32_t   zoom_way;
    int32_t   zoom_hold;        /* frames to wait at the top */
    int32_t   zoom_held;
    float     zoom_step_x, zoom_step_y, zoom_step_scale;
    float     camera_x, camera_y;   /* how far the battlefield has moved */
    float     camera_scale;

    /* krinMelee: the attacker walks up to whoever it is hitting, swings, and
       walks back. Nothing else moves, so one runner at a time is enough. */
    int32_t   melee_slot;
    int32_t   melee_dir;        /* 1 out, 0 while swinging, -1 back */
    int32_t   melee_state;      /* 0 running, 1 swing, 2 follow through */
    int32_t   melee_counter;
    /* Frames since this leg of the walk began, which is what the run, the
       swing and the walk home are each played off. */
    int32_t   melee_frame;
    float     melee_x, melee_y;      /* how far from where it stands */
    float     melee_rel;             /* the same along x, for the easing */
    float     melee_span;            /* how far along x it has to go */
    float     melee_step_x, melee_step_y;
    float     melee_facing;

    /* krinBoltMake: a missile's projectile on its way over. It starts at the
       caster, accelerates, and the move lands when it reaches the target. */
    const char *bolt;
    int32_t   bolt_target;
    float     bolt_x, bolt_y;        /* where it is now */
    float     bolt_step_x, bolt_step_y;
    float     bolt_speed;
    float     bolt_angle;
    float     bolt_facing;           /* which way it is crossing */
    int32_t   bolt_tick;

    /* colortobe: the colour each unit was last told to cast in. The original
       sets it on the model before it plays, and the effect the model shows
       over itself reads it as it loads, so it lasts until the next move. */
    int32_t   cast_colour[SONNY_SLOTS];

    /* KrinTrail: the streak the projectile leaves behind it. The original
       attaches it once, on the bolt's first frame, at the point the bolt had
       reached by then and turned to face the same way; it is then stretched
       along its own x as the bolt runs on, so its bright end keeps up while
       its faded end stays where it was thrown from. It lives thirty-three
       frames of its own whatever the bolt does, so a trail outlasts the hit. */
    int32_t   trail_tick;            /* 1..33, or 0 for no trail */
    float     trail_x, trail_y;
    float     trail_angle;
    float     trail_scale;           /* per cent, as the original's _xscale */
    Color     trail_colour;

    /* GridShaker: the battlefield bounces when a blow pierces, and on every
       shock. The clip walks four frames a cycle, taking a tenth off the
       throw each time round, until there is nothing left of it. */
    float     shake_value;
    float     shake_y;
    int32_t   shake_phase;
    /* The move has been worked out but not yet shown: a melee attacker is
       still on its way over. */
    int32_t   move_pending;
    /* moveSelectBoomer: frames since the ring closed over the turn
       indicator on the player's choice, or -1 when it is not running. */
    int32_t   boomer_tick;

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
    int32_t   balloon_tick;    /* how far through the speaker's balloon */

    /* Rewards from the battle just won. */
    BattleRewards rewards;
    /* The victory screen fills the experience bar towards what the fight paid
       over thirty frames, and levels up when the fill gets there. */
    int32_t   win_fill;        /* setLimiter */
    float     win_xp;          /* exp, the running percentage */
    float     win_step;        /* adderPer */
    int32_t   win_leveled;     /* playerLeveled */
    /* The hub's welcome: which of the story's notes is up, or -1, and
       whether this visit has already offered one. */
    /* The reticle does not snap on and off: it comes up over a few frames
       and goes down the same way, so `fade` is how far in it is and
       `fade_unit` who it is still fading out from. */
    /* Where the pointer was when the screen last ran. The battle screen
       draws from it, so a headless capture with the pointer parked sees what
       a player hovering there would. */
    Vector2   pointer;
    float     ring_fade;
    int32_t   ring_fade_unit;
    int32_t   hub_note;
    int32_t   hub_note_done;
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
    /* Which of MENU[43..46] the skills screen is showing. The original
       picks one at random each time the clip loads. */
    int32_t   skill_tip;
    /* Which step of the level-up walkthrough the box is on, or -1 when it
       is showing a tip instead. */
    int32_t   skill_step;
    /* Which store marker opened the shop screen: its button's character is
       what says which store, as Krin.shopId does in the original. */
    int32_t   shop_button;
    /* Krin.bossFight and Krin.progressFight, set by the marker that started
       the fight: the first picks the boss music and, on a win, counts the
       zone as beaten; the second is what carries progress forward. */
    int32_t   boss_fight;
    int32_t   progress_fight;
    /* How far through the original's four-track battle playlist we are. */
    /* addSound("Music", n): `mode` is soundModeKrin -- 0 nothing, 1 roaming,
       2 a fight -- and asking for the mode already playing does nothing.
       `music_next` is soundPlayCounter, one counter walked by the hub and by
       every fight alike. */
    int32_t   music_mode;
    int32_t   music_next;
    /* Which way the slot screen was opened: a new game writes over the slot,
       a load reads it. */
    enum { SLOT_SAVE = 0, SLOT_LOAD } slot_mode;
    /* The settings the original asks for once, before the story starts. */
    struct {
        int32_t sound, graphics, quality, autosave;
    } options;
    int32_t   lost_timer;
    /* Both sides ran out at once, which the original has its own frame for. */
    int32_t   battle_drawn;
    int32_t   gameover_tip;
    /* What the tooltip is showing this frame. The original keeps one of
       these and every button fills it on roll-over. */
    char      tip_title[128];
    char      tip_body[256];
    /* An item's tooltip is a different shape: the name on a backing tinted by
       the item's own rarity, then what it takes to wear it, then every
       attribute it adds, then what it says about itself. */
    char      tip_req[128];
    char      tip_lines[TOOLTIP_LINES][64];
    int32_t   tip_line_count;
    Color     tip_tint;
    /* Krin.UITmouseHold: the ability the pointer is carrying. The original
       does not drag -- clicking a node or a pool row picks one up, clicking
       a slot of the action bar puts it down, and clicking a slot while
       carrying nothing clears it. */
    int32_t   carrying;
    /* Krin.mouseItem: the item the pointer is carrying, which every slot
       swaps with rather than moves. */
    int32_t   carried_item;
    /* Which of the three comics is playing, how far through it is, and which
       line of the caption it has reached. */
    int32_t   cutscene;
    int32_t   cutscene_frame;
    int32_t   cutscene_line;
    /* How far into the comic the clock has got. The narration's playhead is
       that clock when there is a sound device; without one it runs on its
       own. */
    float     cutscene_clock;
    float     cutscene_playhead;
    int32_t   cutscene_voiced;
    /* Krin.bossJustPwned: the fight just won was the one that finishes a
       zone, which is what sends the player out to the map. */
    int32_t   boss_beaten;
    /* Krin.MenuPlayerSelect: which of the party the character screen is
       turned to. */
    int32_t   menu_member;
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
/* Where an exported image goes: its own pixel size, stood on the placement
   point by the origin the SWF records. */
Rectangle placed_texture(const Texture2D *tex, float x, float y,
                         float scale_x, float scale_y, float ox, float oy);
void draw_texture_placed(const Texture2D *tex, float x, float y,
                         float scale_x, float scale_y, float ox, float oy,
                         Color tint);
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
/* Every piece of a clip, moved by `moved`. A piece the game points at a
   frame of by name is drawn only when `framed` names that piece and `chosen`
   says which frame; the others are drawn as themselves. */
void draw_clip_parts(const char *screen, const char *owner, Vector2 moved,
                     const char *framed, const char *chosen, Color tint);

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
void game_tooltip_line(Game *g, const char *text);
void screen_talents_open(Game *g);
/* An item's tooltip, built the way the original builds it: the requirement
   line, then one line for every attribute the item adds, then its own text.
   `price` over zero puts the price in front of the name, as a shop does. */
void game_tooltip_item(Game *g, const ItemDef *item, int32_t price);

/* Offer the story's note for where the player has got to, if there is one and
   this visit has not already offered it, and whether one is up. */
/* addSound("Music", n): the roaming track and a fight's, which share one
   counter -- so which track a fight gets depends on what has played since. */
void game_music_roaming(Game *g);
void game_music_battle(Game *g);

void game_hub_note(Game *g);
int  game_hub_note_up(const Game *g);
void game_draw_tooltip(const Game *g, Vector2 mouse);

void game_draw_notice(const Game *g);
void ui_text(const char *text, float x, float y, float size, Color color);
float ui_text_width(const char *text, float size);
/* The same, in the system sans face the original's "_sans" device-font text
   fields are rendered with. Falls back to the embedded font when the system
   has nothing suitable. */
void ui_sans_text(const char *text, float x, float y, float size, Color color);
float ui_sans_text_width(const char *text, float size);
/* The bold weight of the same face, which the tooltip's title asks for. */
void ui_sans_bold_text(const char *text, float x, float y, float size,
                       Color color);
float ui_sans_bold_width(const char *text, float size);

/* Shared helpers. */
void game_log(Game *g, const char *fmt, ...);
void game_notice(Game *g, const char *fmt, ...);
Vector2 stage_mouse(void);
/* Whether a press happened this frame, counting the ones a headless run
   makes for itself. */
void ui_set_synthetic_click(int on);
int ui_clicked(void);
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
void screen_cutscene_update(Game *g, Vector2 mouse);
void screen_cutscene_draw(Game *g, Vector2 mouse);
void screen_ending_update(Game *g, Vector2 mouse);
void screen_ending_draw(Game *g, Vector2 mouse);
/* Play one of the three comics: 0 the opening, 1 the bridge, 2 the ending. */
void game_play_cutscene(Game *g, int32_t which);

/* Leave the settings for the story, which is where a new game begins. */
void game_begin_story(Game *g);

void battle_screen_start(Game *g, int32_t battle_id);
void battle_screen_update(Game *g, Vector2 mouse, int headless);
void battle_screen_draw(Game *g);

/* Where the player's save lives. */


#endif
