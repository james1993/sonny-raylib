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
#define BUTTON_PLAY     1165
/* The one button the game-over screen has: load the slot again. */
#define BUTTON_RELOAD 1171

static const Vector2 NOWHERE = {0, 0};

/* A frame fills its screen by setting variables, and each field says which
   one it is bound to, so text goes in by the name the script uses. */
/* Setting a variable in Flash fills every field bound to it, and several of
   these frames bind two -- a dark copy under a light one, which is how they
   get a drop shadow -- so all of them are drawn, in the order the display
   list has them. */
static void say(const char *screen, const char *variable, const char *text)
{
    for (int32_t i = 0; i < SONNY_TEXT_FIELD_COUNT; i++) {
        const TextField *f = &SONNY_TEXT_FIELDS[i];
        if (strcmp(f->screen, screen) == 0
            && strcmp(f->variable, variable) == 0)
            draw_field_wrapped(f, NOWHERE, text);
    }
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
    draw_frame(CLASS_SCREEN, mouse);
    say(CLASS_SCREEN, "whatToSay1", lang_text("MENU", 24));
    for (int i = 0; i < 4; i++)
        say(CLASS_SCREEN, TextFormat("whatToSay%d", i + 2),
            lang_text("CLASS", i));
    /* Each class button names itself on rollOver: the class as the title and
       CLASSDESCRIPT as the body. */
    for (int i = 0; i < 4; i++) {
        const StageButton *b = stage_button(CLASS_SCREEN, CLASS_BUTTONS[i], 0);
        if (b && CheckCollisionPointRec(mouse, (Rectangle){b->x, b->y,
                                                           b->width,
                                                           b->height}))
            game_tooltip(g, lang_text("CLASS", i),
                         lang_text("CLASSDESCRIPT", i));
    }
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

/* A departure from the original, which asks four things here.
 *
 * Three of them are gone. Effects and Graphics chose between the original's
 * own two qualities, and this port only has the one -- the good one -- so the
 * question had no answer to give; Autosave is on, because a game that loses a
 * run to a setting is the setting's fault. What is left is the sound, and one
 * switch does not need a row and a word: it is the quaver, and it carries the
 * whole screen. The two lines of advice went with them, being advice about
 * turning down settings that are no longer there.
 *
 * `options.sound` now does something, too. The original's switch was wired to
 * nothing here -- the port read the value nowhere -- so turning the sound off
 * left it playing. */
#define SOUND_NOTE_X 404.0f
#define SOUND_NOTE_Y 145.4f
/* Bigger than the row it replaces: it is the only thing on the screen to
   press besides START, and at a row's height it read as a stray glyph. */
#define SOUND_NOTE_W 58.0f
#define SOUND_NOTE_H 62.0f

static Rectangle sound_note_box(void)
{
    return (Rectangle){SOUND_NOTE_X - SOUND_NOTE_W / 2,
                       SOUND_NOTE_Y - SOUND_NOTE_H / 2,
                       SOUND_NOTE_W, SOUND_NOTE_H};
}

void screen_options_draw(Game *g, Vector2 mouse)
{
    draw_frame(OPTIONS_SCREEN, mouse);
    say(OPTIONS_SCREEN, "TitLOL1", lang_text("MENU", 33));
    say(OPTIONS_SCREEN, "whatToSayY", lang_text("MENU", 36));
    Rectangle note = sound_note_box();
    draw_music_note(note, g->options.sound, hit(note, mouse));
}

void screen_options_update(Game *g, Vector2 mouse)
{
    if (hit(sound_note_box(), mouse) && ui_clicked()) {
        g->options.sound = !g->options.sound;
        audio_set_muted(!g->options.sound);
        /* After the switch, so that turning the sound on is audible and
           turning it off is not. */
        audio_play("Click3pickup");
    } else if (screen_button_pressed(OPTIONS_SCREEN, BUTTON_PLAY, mouse)) {
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

/* The same switches again, on the menu clip's own frame, with the tally the
   original keeps beside them -- and the same three of them gone, for the same
   reasons. The rows here are the clip's own static text rather than fields
   the screen fills, so the three that go have to be left out by where they
   are; their y is the only thing that names them. */
#define SETTINGS_MENU  "options"
#define MENU_SCREEN_ID "menu"
#define SETTINGS_SOUND    1483
#define SETTINGS_CLOSE    1364
/* The three rows that go, by the field each of them fills: Effects, Graphics
   and Autosave. Their headings are the clip's own static text and sit at the
   same height, so this is what names them. */
static const char *const SETTINGS_ROWS_GONE[] = {"tit_4", "tit_7", "tit_11"};

/* Where the quaver goes on this screen: over the switch the sound row's own
   button occupies, so it is where the original's switch was. */
static Rectangle settings_note_box(void)
{
    const StageButton *b = NULL;
    for (int i = 0; i < SONNY_BUTTON_COUNT; i++)
        if (strcmp(SONNY_BUTTONS[i].screen, SETTINGS_MENU) == 0
            && SONNY_BUTTONS[i].character == SETTINGS_SOUND)
            b = &SONNY_BUTTONS[i];
    if (!b)
        return (Rectangle){0, 0, 0, 0};
    return (Rectangle){b->x + b->width / 2 - 23.0f,
                       b->y + b->height / 2 - 20.0f, 46.0f, 40.0f};
}

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
    draw_clip_parts(MENU_SCREEN_ID, SETTINGS_MENU, NOWHERE, NULL, NULL, WHITE);
    /* The clip's own static text, less the headings of the rows that are
       gone. The tally on the other side of the panel has rows at the same
       heights, so this cannot be done by height alone. */
    float gone[3];
    int goners = 0;
    for (size_t i = 0; i < sizeof(SETTINGS_ROWS_GONE)
                           / sizeof(SETTINGS_ROWS_GONE[0]); i++) {
        const TextField *f = text_field_var(MENU_SCREEN_ID,
                                            SETTINGS_ROWS_GONE[i]);
        if (f)
            gone[goners++] = f->y;
    }
    draw_static_text_except(MENU_SCREEN_ID, SETTINGS_MENU, gone, goners);
    for (int i = 0; i < SONNY_BUTTON_COUNT; i++)
        if (strcmp(SONNY_BUTTONS[i].screen, SETTINGS_MENU) == 0
            && SONNY_BUTTONS[i].character == SETTINGS_SOUND)
            draw_button_art(&SONNY_BUTTONS[i], WHITE);

    Rectangle note = settings_note_box();
    draw_music_note(note, g->options.sound, hit(note, mouse));

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
    if (hit(settings_note_box(), mouse) && ui_clicked()) {
        g->options.sound = !g->options.sound;
        audio_set_muted(!g->options.sound);
        audio_play("Click3pickup");
    } else if (screen_button_pressed("Navigation", SETTINGS_CLOSE, mouse))
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
    say(OVER_SCREEN, "tipTextGO", lang_text("GOTIP", g->gameover_tip));
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

/* The SWF runs at 30 frames a second, and a comic's narration carries exactly
   one frame of audio for each frame of animation (735 samples at 22050 Hz), so
   the playhead and the animation are the same clock. */
#define CUTSCENE_FPS 30.0f

/* Which clip each cutscene frame plays, the narration over it, and where the
   root timeline goes when it ends. The intro does not go to the hub: SKIP and
   the last frame both hand the root over to `gotoSceneKrin`, which the PLAY
   button set to "IntroSeq" -- fifteen frames that end by setting BattlePick to
   2 and progressFight, and going to LOADBATTLESCENE. So the opening comic
   leads straight into the first fight. */
#define INTRO_BATTLE 2

static const struct {
    const char *screen;
    int32_t     clip;
    const char *voice;
    int32_t     then;           /* SCREEN_BATTLE means IntroSeq's own ending */
} CUTSCENES[] = {
    {"CS_INTRO", 1695, "CutsceneVoiceIntro", SCREEN_BATTLE},
    {"CS_BRIDGE", 1710, "CutsceneVoiceBridge", SCREEN_MAP},
    {"CS_OUTRO", 1719, "CutsceneVoiceOutro", SCREEN_ENDING},
};

void game_play_cutscene(Game *g, int32_t which)
{
    if (which < 0 || which >= (int32_t)(sizeof(CUTSCENES)
                                        / sizeof(CUTSCENES[0])))
        return;
    g->cutscene = which;
    g->cutscene_frame = 1;
    g->cutscene_clock = 0.0f;
    g->cutscene_playhead = 0.0f;
    const CutsceneDef *def = cutscene_by_clip(CUTSCENES[which].clip);
    g->cutscene_line = def ? def->start : 0;
    g->screen = SCREEN_CUTSCENE;
    /* The narration is the animation's clock, so it starts with it. */
    g->cutscene_voiced = audio_narration(CUTSCENES[which].voice);
}

/* Where the root timeline goes once the comic has faded out. */
static void cutscene_over(Game *g)
{
    int32_t then = CUTSCENES[g->cutscene].then;
    audio_narration_stop();
    if (then != SCREEN_BATTLE) {
        g->screen = then;
        return;
    }
    /* IntroSeq's last frame: the first fight, and it carries progress. It
       also sets soundPlayCounter to 1, so the opening fight is the second
       track rather than the first. */
    g->boss_fight = 0;
    g->progress_fight = 1;
    g->music_next = 1;
    battle_screen_start(g, INTRO_BATTLE);
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
        int32_t frame = g->cutscene_frame;
        if (count > 0 && frame > count)
            frame = count;
        if (frame < 1)
            frame = 1;
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
        /* SKIP stops the sound and hands the root timeline on; that is all it
           does. */
        audio_play("Click3pickup");
        cutscene_over(g);
        return;
    }

    /* The clock. A comic's narration is a stream sound, which is what Flash
       holds the timeline to, so the playhead is the animation's position --
       but only while it is actually moving. Flash falls back to the frame
       rate when the sound cannot play, and so does this: the clock runs on
       its own and the narration pulls it into step whenever it advances.
       (Ruffle, which has no fallback, is why the original stands still in a
       capture with no sound device.) */
    g->cutscene_clock += GetFrameTime();
    if (g->cutscene_voiced && audio_narration_playing()) {
        float played = audio_narration_time();
        if (played > g->cutscene_playhead) {
            g->cutscene_playhead = played;
            g->cutscene_clock = played;
        }
    }
    g->cutscene_frame = 1 + (int32_t)(g->cutscene_clock * CUTSCENE_FPS);

    int32_t count = asset_frame_count(TextFormat("#%d",
                                                 CUTSCENES[g->cutscene].clip));
    if (count > 0 && g->cutscene_frame > count)
        cutscene_over(g);
}
