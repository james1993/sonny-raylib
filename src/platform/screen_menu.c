/* The screens before the game and the screens after it: the title, the new
 * game / load game choice, the four save slots, the class menu, the settings
 * the original asks for once, the manual, and the two ways a run can end.
 *
 * Each is one of the original's own root frames, laid out from the display
 * list that frame places and driven by the handlers its buttons carry.
 */
#include <stdio.h>
#include <string.h>
#include "assets.h"
#include "audio.h"
#include "game.h"

/* The frames, by the labels the root timeline gives them. */
#define TITLE_SCREEN   "mainMenu"
#define START_SCREEN   "subMenu"
#define SLOTS_SCREEN   "dataMenu"
#define CLASS_SCREEN   "classMenu"
#define OPTIONS_SCREEN "optionsMenu"
#define MANUAL_SCREEN  "designMenu"
#define OVER_SCREEN    "gameOverMenu"
#define LOST_SCREEN    "loseCombat"
#define DRAW_SCREEN    "drawCombat"

/* Every one of these frames carries the same "Back" button, which goes to the
   title, and the same two corner captions. */
#define BUTTON_BACK 1117

/* The title's own two: "Start!" and the manual. */
#define BUTTON_START  1108
#define BUTTON_MANUAL 1109

/* The new game / load game pair. */
#define BUTTON_NEW  1113
#define BUTTON_LOAD 1115

/* The four save slots, in order. */
static const int32_t SLOT_BUTTONS[SONNY_SAVE_SLOTS] = {1135, 1136, 1137, 1138};
/* The four classes the class menu offers, in the order it lists them. */
static const int32_t CLASS_BUTTONS[4] = {1148, 1149, 1150, 1151};
/* The settings rows, and the button that leaves for the story. */
#define BUTTON_SOUND    1156
#define BUTTON_GRAPHICS 1157
#define BUTTON_QUALITY  1162
#define BUTTON_AUTOSAVE 1168
#define BUTTON_PLAY     1165
/* The one button the game-over screen has: load the slot again. */
#define BUTTON_RELOAD 1171

static const Vector2 NOWHERE = {0, 0};

/* A frame fills its screen by setting variables, and each field says which
   one it is bound to, so text goes in by the name the script uses. */
static void say(const char *screen, const char *variable, const char *text)
{
    draw_field_wrapped(text_field_var(screen, variable), NOWHERE, text);
}

/* A screen's furniture, its buttons' own art, and the two captions every one
   of these frames carries in its corners. */
static void draw_frame(const char *screen, Vector2 mouse)
{
    ClearBackground(BLACK);
    draw_screen_chrome(screen);
    draw_screen_buttons(screen, mouse);
    /* The captions the frame authored into its own fields: the "Back" on
       every one of them, the slot numbers, the copyright line. */
    draw_screen_text(screen);
}

/* ------------------------------------------------------------ the title */

void screen_title_draw(Game *g, Vector2 mouse)
{
    (void)g;
    draw_frame(TITLE_SCREEN, mouse);
    say(TITLE_SCREEN, "whatToSay1", lang_text("MENU", 18));
    say(TITLE_SCREEN, "whatToSay2", lang_text("MENU", 20));
}

void screen_title_update(Game *g, Vector2 mouse)
{
    if (screen_button_pressed(TITLE_SCREEN, BUTTON_START, mouse)) {
        audio_play("Click3pickup");
        g->screen = SCREEN_START;
    } else if (screen_button_pressed(TITLE_SCREEN, BUTTON_MANUAL, mouse)) {
        audio_play("Click3pickup");
        g->screen = SCREEN_MANUAL;
    }
}

/* ------------------------------------------- new game or carry one on */

void screen_start_draw(Game *g, Vector2 mouse)
{
    (void)g;
    draw_frame(START_SCREEN, mouse);
    say(START_SCREEN, "whatToSay1", lang_text("MENU", 21));
    say(START_SCREEN, "whatToSay2", lang_text("MENU", 22));
}

void screen_start_update(Game *g, Vector2 mouse)
{
    if (screen_button_pressed(START_SCREEN, BUTTON_NEW, mouse)) {
        audio_play("Click3pickup");
        g->slot_mode = SLOT_SAVE;
        g->screen = SCREEN_SLOTS;
    } else if (screen_button_pressed(START_SCREEN, BUTTON_LOAD, mouse)) {
        audio_play("Click3pickup");
        g->slot_mode = SLOT_LOAD;
        g->screen = SCREEN_SLOTS;
    } else if (screen_button_pressed(START_SCREEN, BUTTON_BACK, mouse)) {
        audio_play("Click3pickup");
        g->screen = SCREEN_TITLE;
    }
}

/* --------------------------------------------------------- the four slots */

/* The names the slots show. An empty one says so in the game's own words. */
static const char *slot_name(int32_t slot, char *buf, size_t max)
{
    Campaign peek;
    if (save_read(&peek, save_slot_path(slot)) == 0) {
        /* A slot is named for whoever is in it: "Lvl N Class", which is what
           the save itself carries. */
        int32_t class_id = peek.player.class_template
                         ? peek.player.class_template->id - 1 : 0;
        snprintf(buf, max, "%s%d %s", lang_text("MENU", 0), peek.player.level,
                 lang_text("CLASS", class_id));
        return buf;
    }
    /* "< Empty Slot >" is built by the frame rather than kept in the table. */
    snprintf(buf, max, "< Empty Slot >");
    return buf;
}

void screen_slots_draw(Game *g, Vector2 mouse)
{
    (void)g;
    draw_frame(SLOTS_SCREEN, mouse);
    /* Every row's word is the same one -- the number beside it is authored
       into the frame -- and each row's name is its own variable. */
    for (int32_t i = 0; i < SONNY_TEXT_FIELD_COUNT; i++) {
        const TextField *f = &SONNY_TEXT_FIELDS[i];
        if (strcmp(f->screen, SLOTS_SCREEN) == 0
            && strcmp(f->variable, "slotNamerText") == 0)
            draw_field(f, NOWHERE, lang_text("MENU", 23));
    }
    for (int32_t i = 0; i < SONNY_SAVE_SLOTS; i++) {
        char buf[64];
        say(SLOTS_SCREEN, TextFormat("nameSlot%d", i + 1),
            slot_name(i + 1, buf, sizeof(buf)));
    }
}

void screen_slots_update(Game *g, Vector2 mouse)
{
    for (int32_t i = 0; i < SONNY_SAVE_SLOTS; i++) {
        if (!screen_button_pressed(SLOTS_SCREEN, SLOT_BUTTONS[i], mouse))
            continue;
        audio_play("Click3pickup");
        g->campaign.slot = i + 1;
        if (g->slot_mode == SLOT_SAVE) {
            g->screen = SCREEN_CLASS;
            return;
        }
        /* Loading only happens when the slot has something in it, which is
           what the original checks before it moves. */
        if (save_read(&g->campaign, save_slot_path(i + 1)) == 0) {
            campaign_story_joins(&g->campaign);
            g->campaign.slot = i + 1;
            g->screen = SCREEN_ZONE;
        }
        return;
    }
    if (screen_button_pressed(SLOTS_SCREEN, BUTTON_BACK, mouse)) {
        audio_play("Click3pickup");
        g->screen = SCREEN_TITLE;
    }
}

/* ------------------------------------------------------------- the class */

void screen_class_draw(Game *g, Vector2 mouse)
{
    (void)g;
    draw_frame(CLASS_SCREEN, mouse);
    say(CLASS_SCREEN, "whatToSay1", lang_text("MENU", 24));
    for (int i = 0; i < 4; i++)
        say(CLASS_SCREEN, TextFormat("whatToSay%d", i + 2),
            lang_text("CLASS", i));
}

void screen_class_update(Game *g, Vector2 mouse)
{
    for (int i = 0; i < 4; i++) {
        if (!screen_button_pressed(CLASS_SCREEN, CLASS_BUTTONS[i], mouse))
            continue;
        audio_play("Click3pickup");
        /* Krin.Class is the choice; the character's class is one past it, the
           way ClassStats numbers them. */
        int32_t slot = g->campaign.slot;
        campaign_new(&g->campaign, i + 1);
        g->campaign.slot = slot;
        g->screen = SCREEN_OPTIONS;
        return;
    }
    if (screen_button_pressed(CLASS_SCREEN, BUTTON_BACK, mouse)) {
        audio_play("Click3pickup");
        g->screen = SCREEN_TITLE;
    }
}

/* ----------------------------------------------------------- the settings */

void screen_options_draw(Game *g, Vector2 mouse)
{
    draw_frame(OPTIONS_SCREEN, mouse);
    /* The four rows, their headings and the two lines of advice, exactly as
       the frame's own script sets them. */
    say(OPTIONS_SCREEN, "tit_1", lang_text("MENU", 27));
    say(OPTIONS_SCREEN, "tit_2", lang_text("MENU", 28));
    say(OPTIONS_SCREEN, "tit_8", lang_text("MENU", 35));
    say(OPTIONS_SCREEN, "tit_9", lang_text("MENU", 38));
    say(OPTIONS_SCREEN, "tit_3", lang_text("MENU", g->options.sound ? 29 : 30));
    say(OPTIONS_SCREEN, "tit_4",
        lang_text("MENU", g->options.graphics ? 31 : 32));
    say(OPTIONS_SCREEN, "tit_11",
        lang_text("MENU", g->options.quality ? 31 : 32));
    say(OPTIONS_SCREEN, "tit_7",
        lang_text("MENU", g->options.autosave ? 29 : 30));
    say(OPTIONS_SCREEN, "TitLOL1", lang_text("MENU", 33));
    say(OPTIONS_SCREEN, "TitLOL2", lang_text("MENU", 34));
    say(OPTIONS_SCREEN, "whatToSayY", lang_text("MENU", 36));
}

void screen_options_update(Game *g, Vector2 mouse)
{
    if (screen_button_pressed(OPTIONS_SCREEN, BUTTON_SOUND, mouse))
        g->options.sound = !g->options.sound;
    else if (screen_button_pressed(OPTIONS_SCREEN, BUTTON_GRAPHICS, mouse))
        g->options.graphics = !g->options.graphics;
    else if (screen_button_pressed(OPTIONS_SCREEN, BUTTON_QUALITY, mouse))
        g->options.quality = !g->options.quality;
    else if (screen_button_pressed(OPTIONS_SCREEN, BUTTON_AUTOSAVE, mouse))
        g->options.autosave = !g->options.autosave;
    else if (screen_button_pressed(OPTIONS_SCREEN, BUTTON_PLAY, mouse)) {
        audio_play("Click3pickup");
        game_begin_story(g);
    } else if (screen_button_pressed(OPTIONS_SCREEN, BUTTON_BACK, mouse))
        g->screen = SCREEN_TITLE;
}

/* Leaving the settings behind. The original runs its opening cutscene here
   and then drops the player on the deck of the ship. */
void game_begin_story(Game *g)
{
    game_play_cutscene(g, 0);
}

/* ------------------------------------------------------------ the manual */

void screen_manual_draw(Game *g, Vector2 mouse)
{
    (void)g;
    draw_frame(MANUAL_SCREEN, mouse);
    /* Five blocks of the manual and the note beside them. */
    for (int i = 0; i < 5; i++)
        say(MANUAL_SCREEN, TextFormat("xRx%d", i + 1),
            lang_text("MANUAL", i));
    say(MANUAL_SCREEN, "xRx6", lang_text("EXPLAINEXTRA", 0));
}

void screen_manual_update(Game *g, Vector2 mouse)
{
    if (screen_button_pressed(MANUAL_SCREEN, BUTTON_BACK, mouse)) {
        audio_play("Click3pickup");
        g->screen = SCREEN_TITLE;
    }
}

/* ------------------------------------------ the settings inside the game */

/* The same four switches again, on the menu clip's own frame, with the tally
   the original keeps beside them. */
#define SETTINGS_MENU  "options"
#define MENU_SCREEN_ID "menu"
#define SETTINGS_SOUND    1483
#define SETTINGS_GRAPHICS 1157
#define SETTINGS_QUALITY  1162
#define SETTINGS_AUTOSAVE 1490
#define SETTINGS_CLOSE    1364

static void menu_say(const char *variable, const char *text)
{
    draw_field_wrapped(text_field_var(MENU_SCREEN_ID, variable), NOWHERE,
                       text);
}

void screen_settings_draw(Game *g, Vector2 mouse)
{
    ClearBackground(BLACK);
    draw_screen_chrome("Navigation");
    draw_screen_buttons("Navigation", mouse);
    draw_clip_parts(MENU_SCREEN_ID, SETTINGS_MENU, NOWHERE, NULL, WHITE);
    draw_static_text(MENU_SCREEN_ID, SETTINGS_MENU);
    for (int i = 0; i < SONNY_BUTTON_COUNT; i++)
        if (strcmp(SONNY_BUTTONS[i].screen, SETTINGS_MENU) == 0)
            draw_button_art(&SONNY_BUTTONS[i], WHITE);

    menu_say("tit_3", lang_text("MENU", g->options.sound ? 29 : 30));
    menu_say("tit_4", lang_text("MENU", g->options.graphics ? 31 : 32));
    menu_say("tit_7", lang_text("MENU", g->options.autosave ? 29 : 30));
    menu_say("tit_11", lang_text("MENU", g->options.quality ? 31 : 32));

    const Campaign *c = &g->campaign;
    menu_say("gs_zone_cleared", TextFormat("%d", c->stats.zones_cleared));
    menu_say("gs_respec_used", TextFormat("%d", c->stats.respec_used));
    menu_say("gs_training_used", TextFormat("%d", c->stats.training_used));
    menu_say("gs_top_dmg_physical", TextFormat("%d", c->stats.top_physical));
    menu_say("gs_top_dmg_elemental", TextFormat("%d", c->stats.top_elemental));
    menu_say("gs_bg_found", TextFormat("%d", c->stats.scenery_found));
}

void screen_settings_update(Game *g, Vector2 mouse)
{
    if (screen_button_pressed(SETTINGS_MENU, SETTINGS_SOUND, mouse))
        g->options.sound = !g->options.sound;
    else if (screen_button_pressed(SETTINGS_MENU, SETTINGS_GRAPHICS, mouse))
        g->options.graphics = !g->options.graphics;
    else if (screen_button_pressed(SETTINGS_MENU, SETTINGS_QUALITY, mouse))
        g->options.quality = !g->options.quality;
    else if (screen_button_pressed(SETTINGS_MENU, SETTINGS_AUTOSAVE, mouse))
        g->options.autosave = !g->options.autosave;
    else if (screen_button_pressed("Navigation", SETTINGS_CLOSE, mouse))
        g->screen = SCREEN_ZONE;
}

/* -------------------------------------------------------- losing a fight */

/* The original stops on this frame for a moment and then goes to the
   game-over screen, which is where the choice is. */
#define LOST_HOLD_FRAMES 60

void screen_lost_draw(Game *g, Vector2 mouse)
{
    (void)mouse;
    /* Both ways a fight can end badly have a frame of their own, and each is
       one line on black. */
    const char *screen = g->battle_drawn ? DRAW_SCREEN : LOST_SCREEN;
    ClearBackground(BLACK);
    draw_screen_chrome(screen);
    draw_screen_text(screen);
}

void screen_lost_update(Game *g, Vector2 mouse)
{
    (void)mouse;
    if (++g->lost_timer >= LOST_HOLD_FRAMES)
        g->screen = SCREEN_GAMEOVER;
}

void screen_gameover_draw(Game *g, Vector2 mouse)
{
    draw_frame(OVER_SCREEN, mouse);
    say(OVER_SCREEN, "whatToSay111", lang_text("MENU", 50));
    say(OVER_SCREEN, "whatToSay222",
        TextFormat("< %s >", lang_text("MENU", 51)));
    /* The tip under it, which the frame draws twice -- a dark copy under a
       light one -- and picks by the counter the original keeps. */
    const char *tip = lang_text("GOTIP", g->gameover_tip);
    for (int32_t i = 0; i < SONNY_TEXT_FIELD_COUNT; i++) {
        const TextField *f = &SONNY_TEXT_FIELDS[i];
        if (strcmp(f->screen, OVER_SCREEN) == 0
            && strcmp(f->variable, "tipTextGO") == 0)
            draw_field_wrapped(f, NOWHERE, tip);
    }
}

void screen_gameover_update(Game *g, Vector2 mouse)
{
    if (screen_button_pressed(OVER_SCREEN, BUTTON_RELOAD, mouse)) {
        audio_play("Click3pickup");
        /* Reloading the slot the run was in is the only way on. */
        if (save_read(&g->campaign, save_slot_path(g->campaign.slot)) == 0) {
            campaign_story_joins(&g->campaign);
            g->screen = SCREEN_ZONE;
        } else {
            g->screen = SCREEN_TITLE;
        }
    } else if (screen_button_pressed(OVER_SCREEN, BUTTON_BACK, mouse)) {
        g->screen = SCREEN_TITLE;
    }
}

/* ------------------------------------------------------------ the end */

#define END_SCREEN "endMenu"
#define BUTTON_END_AGAIN 1175
#define BUTTON_END_MORE  1177

void screen_ending_draw(Game *g, Vector2 mouse)
{
    (void)g;
    draw_frame(END_SCREEN, mouse);
    say(END_SCREEN, "enderText", lang_text("ENDING", 0));
    say(END_SCREEN, "whatToSay3", lang_text("MENU", 19));
}

void screen_ending_update(Game *g, Vector2 mouse)
{
    if (screen_button_pressed(END_SCREEN, BUTTON_END_AGAIN, mouse)
        || screen_button_pressed(END_SCREEN, BUTTON_END_MORE, mouse)) {
        audio_play("Click3pickup");
        g->screen = SCREEN_TITLE;
    }
}

/* ---------------------------------------------------------- cutscenes */

/* The three comics the story is told in. Each is one long animation on a root
 * frame of its own, with a caption under it the animation's own frames change
 * and a SKIP the player can press at any point.
 */
#define CUTSCENE_SKIP 1697

/* Which clip each cutscene frame plays, and where it goes when it ends. */
static const struct {
    const char *screen;
    int32_t     clip;
    int32_t     then;
} CUTSCENES[] = {
    {"CS_INTRO", 1695, SCREEN_ZONE},
    {"CS_BRIDGE", 1710, SCREEN_MAP},
    {"CS_OUTRO", 1719, SCREEN_ENDING},
};

void game_play_cutscene(Game *g, int32_t which)
{
    if (which < 0 || which >= (int32_t)(sizeof(CUTSCENES)
                                        / sizeof(CUTSCENES[0])))
        return;
    g->cutscene = which;
    g->cutscene_frame = 1;
    const CutsceneDef *def = cutscene_by_clip(CUTSCENES[which].clip);
    g->cutscene_line = def ? def->start : 0;
    g->screen = SCREEN_CUTSCENE;
}

/* The caption as the animation has reached it: every cue up to this frame,
   replayed, because a cue either shows the next line or clears it. */
static const char *cutscene_caption(const Game *g)
{
    int32_t clip = CUTSCENES[g->cutscene].clip;
    const CutsceneDef *def = cutscene_by_clip(clip);
    int32_t line = def ? def->start : 0;
    const char *shown = "";
    for (int i = 0; i < SONNY_CUTSCENE_CUE_COUNT; i++) {
        const CutsceneCue *cue = &SONNY_CUTSCENE_CUES[i];
        if (cue->clip != clip || cue->frame > g->cutscene_frame)
            continue;
        if (cue->clear) {
            shown = "";
        } else {
            shown = lang_text("CUTSUB", line);
            line++;
        }
    }
    return shown;
}

void screen_cutscene_draw(Game *g, Vector2 mouse)
{
    const char *screen = CUTSCENES[g->cutscene].screen;
    draw_frame(screen, mouse);
    /* The comic itself, which the frame places over the backing. */
    const StageChrome *panel = NULL;
    for (int i = 0; i < SONNY_STAGE_CHROME_COUNT; i++) {
        const StageChrome *c = &SONNY_STAGE_CHROME[i];
        if (strcmp(c->screen, screen) == 0
            && c->character == CUTSCENES[g->cutscene].clip)
            panel = c;
    }
    if (panel) {
        const char *art = TextFormat("#%d", panel->character);
        int32_t count = asset_frame_count(art);
        int32_t frame = count > 0 ? ((g->cutscene_frame - 1) % count) + 1 : 1;
        /* Each frame of a comic is its own size, so every one is stood on
           the placement point by its own recorded origin rather than by the
           bounds the first frame happened to have. */
        asset_draw_placed(art, frame, (Vector2){panel->x, panel->y},
                          panel->scale_x, WHITE);
    }
    draw_field_wrapped(text_field_var(screen, "subText"), NOWHERE,
                       cutscene_caption(g));
}

void screen_cutscene_update(Game *g, Vector2 mouse)
{
    const char *screen = CUTSCENES[g->cutscene].screen;
    if (screen_button_pressed(screen, CUTSCENE_SKIP, mouse)) {
        audio_play("Click3pickup");
        g->screen = CUTSCENES[g->cutscene].then;
        return;
    }
    const char *art = TextFormat("#%d", CUTSCENES[g->cutscene].clip);
    int32_t count = asset_frame_count(art);
    if (++g->cutscene_frame > count && count > 0)
        g->screen = CUTSCENES[g->cutscene].then;
}
