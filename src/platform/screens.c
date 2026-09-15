/* The screens outside battle: the zone hub and everything it opens.
 *
 * Text comes from the game's own language arrays, so the menu reads exactly as
 * the original's does (SYSTEM[13..28]), as do the zone names, item names and
 * victory lines.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "assets.h"
#include "audio.h"
#include "game.h"

/* The menu clip's frames, by the labels the original gives them: one screen
   each, and the same slot name means different places on different frames. */
#define MENU_SCREEN    "menu"
#define MENU_INVENTORY "inventory"
#define MENU_WIN       "win"

/* Nothing on a menu moves, so its pieces are drawn where they are recorded. */
static const Vector2 NO_OFFSET = {0, 0};

/* What the game spends, as its own fields carry it. */

/* How many bag slots a menu frame's grid has. */
#define MENU_BAG_SLOTS 36

/* What a full element bar stands for. A character's piercing and defense
   start at twenty-five and climb from there, and the band is scaled so the
   numbers a first zone reaches fill about half of it. */
#define ELEMENT_BAR_FULL 200.0

/* The root frame the hub is laid out from. */
#define HUB_SCREEN "Navigation"

/* One of the hub's fields, by the name the frame gives it. */
static const TextField *hub_field(const char *name)
{
    return chrome_field(HUB_SCREEN, name);
}

/* The cross the menu screens close with, which the original places once on
   the hub's own frame. */
#define MENU_BUTTON_CLOSE 1364

static int menu_close_pressed(Vector2 mouse)
{
    const StageButton *b = stage_button(HUB_SCREEN, MENU_BUTTON_CLOSE, 0);
    return b && ui_clicked()
        && CheckCollisionPointRec(mouse, (Rectangle){b->x, b->y, b->width,
                                                     b->height});
}

/* One of the victory frame's fields, by the name the clip gives it. */
static const TextField *win_field(const char *name)
{
    return text_field_named(MENU_SCREEN, MENU_WIN, name, 0);
}

/* ------------------------------------------------------------ zone hub */

/* What the hub's buttons do, by the character id of the button itself --
   which is the only thing that tells them apart, since a button is never
   part of the art. These are the ids the original's own handlers hang off. */
#define HUB_BUTTON_INVENTORY 1252
#define HUB_BUTTON_SKILLS    1253
#define HUB_BUTTON_SAVE      1254
#define HUB_BUTTON_OPTIONS   1257   /* opens the menu's own settings frame */
#define HUB_BUTTON_RESPEC    1258   /* gives every point back to spend again */
#define HUB_BUTTON_MAP       1251
/* The button that clicks the story's note away. */
#define HUB_NOTE_CLOSE 1512

/* The zone's progress bar: a track, and a fill placed inside it at this
   offset. Only the fill is scaled. */
#define BAR_TRACK  "#1259"
#define BAR_FILL       "ZoneBarFill"
#define BAR_FILL_WIDTH 251.0f
#define BAR_FILL_X (-126.0f)

/* Whether the pointer is on the hub's button with this character. */
static int hub_pressed(int32_t character, Vector2 mouse)
{
    const StageButton *b = stage_button(HUB_SCREEN, character, 0);
    return b && ui_clicked()
        && CheckCollisionPointRec(mouse, (Rectangle){b->x, b->y, b->width,
                                                     b->height});
}

/* The `index`-th marker on this zone's scene, in the order the frame stacks
   them. Every marker is a button, and what its button is says what it does --
   the next story fight, a practice fight, or a store. Everything else on the
   scene is scenery the original only animates. */
static const StageButton *zone_marker_button(const Campaign *c, int32_t index)
{
    const ZoneDef *zone = campaign_zone(c);
    if (!zone || zone->zone >= SONNY_ZONE_LABEL_COUNT)
        return NULL;
    const char *label = SONNY_ZONE_LABELS[zone->zone];
    for (int i = 0; i < SONNY_BUTTON_COUNT; i++) {
        const StageButton *b = &SONNY_BUTTONS[i];
        if (strcmp(b->screen, label) != 0 || !zone_button(b->character))
            continue;
        if (index-- == 0)
            return b;
    }
    return NULL;
}

/* Which battle the story marker starts. The original clamps to the zone's
   last fight once progress has passed it, and calls that fight -- and the
   fight that ends the zone -- a boss fight. */
static int32_t progress_pick(const Campaign *c, int *boss)
{
    const ZoneDef *zone = campaign_zone(c);
    int32_t pick = c->progress_battle;
    *boss = 0;
    if (!zone)
        return pick;
    if (c->progress_battle > zone->last_battle - 1) {
        pick = zone->last_battle - 1;
        *boss = 1;
    } else if (c->progress_battle == zone->last_battle - 1) {
        *boss = 1;
    }
    return pick;
}

/* Which battle a practice marker rolls. The marker's own number is what it
   rolls against, not how long the zone's training list is -- the second
   zone lists nine and rolls eight of them. */
static int32_t training_pick(const Campaign *c, Rng *rng, int32_t choices)
{
    const ZoneDef *zone = campaign_zone(c);
    if (!zone || zone->training_count <= 0 || choices <= 0)
        return 0;
    if (choices > zone->training_count)
        choices = zone->training_count;
    return zone->training[rng_below(rng, (uint32_t)choices)];
}

/* What each of the hub's buttons calls itself, as SYSTEM keeps them: a name
   and the line under it, in pairs. */
static void hub_tooltips(Game *g, Vector2 mouse)
{
    static const struct { int32_t character; int32_t say; } NAMED[] = {
        {HUB_BUTTON_INVENTORY, 17}, {HUB_BUTTON_SKILLS, 19},
        {HUB_BUTTON_SAVE, 21}, {HUB_BUTTON_OPTIONS, 23},
        {HUB_BUTTON_RESPEC, 32}, {HUB_BUTTON_MAP, 27},
    };
    for (size_t i = 0; i < sizeof(NAMED) / sizeof(NAMED[0]); i++) {
        const StageButton *b = stage_button(HUB_SCREEN, NAMED[i].character, 0);
        if (b && CheckCollisionPointRec(mouse, (Rectangle){b->x, b->y,
                                                           b->width,
                                                           b->height}))
            game_tooltip(g, lang_text("SYSTEM", NAMED[i].say),
                         lang_text("SYSTEM", NAMED[i].say + 1));
    }
    /* The markers on the scene name themselves the same way. */
    for (int32_t i = 0; ; i++) {
        const StageButton *b = zone_marker_button(&g->campaign, i);
        if (!b)
            break;
        if (!CheckCollisionPointRec(mouse, (Rectangle){b->x, b->y, b->width,
                                                       b->height}))
            continue;
        const ZoneButton *marker = zone_button(b->character);
        int32_t say = marker->kind == MARKER_SHOP ? 15
                    : marker->kind == MARKER_TRAINING ? 29 : 13;
        game_tooltip(g, lang_text("SYSTEM", say),
                     lang_text("SYSTEM", say + 1));
    }
}

void screen_zone_update(Game *g, Vector2 mouse)
{
    /* The hub's own frame asks for the roaming track every time it is
       reached, which is also what moves the counter the fights share. */
    game_music_roaming(g);
    /* The note holds the hub still: PauseForScreen, which is what stops every
       marker and every button answering until it is clicked away. */
    if (game_hub_note_up(g)) {
        if (screen_button_pressed(HUB_SCREEN, HUB_NOTE_CLOSE, mouse)) {
            audio_play("Click3pickup");
            g->hub_note = -1;
        }
        return;
    }
    hub_tooltips(g, mouse);
    /* The markers on the scene. Which one was pressed decides what happens,
       and for a store it is also what says which store. */
    for (int32_t i = 0; ui_clicked(); i++) {
        const StageButton *b = zone_marker_button(&g->campaign, i);
        if (!b)
            break;
        if (!CheckCollisionPointRec(mouse, (Rectangle){b->x, b->y, b->width,
                                                       b->height}))
            continue;
        const ZoneButton *marker = zone_button(b->character);
        audio_play("Click3pickup");
        switch (marker->kind) {
        case MARKER_SHOP:
            g->shop_button = b->character;
            g->screen = SCREEN_SHOP;
            return;
        case MARKER_TRAINING: {
            int32_t pick = training_pick(&g->campaign, &g->rng,
                                         marker->choices);
            g->boss_fight = 0;
            g->progress_fight = 0;
            g->campaign.stats.training_used++;
            if (pick)
                battle_screen_start(g, pick);
            return;
        }
        case MARKER_PROGRESS:
        default: {
            int boss = 0;
            int32_t pick = progress_pick(&g->campaign, &boss);
            g->boss_fight = boss;
            /* Clamping past the zone's last fight makes it a boss fight that
               no longer carries progress. */
            g->progress_fight = (g->campaign.progress_battle
                                 <= campaign_zone(&g->campaign)->last_battle
                                    - 1);
            battle_screen_start(g, pick);
            return;
        }
        }
    }

    if (hub_pressed(HUB_BUTTON_INVENTORY, mouse)) {
        audio_play("Click3pickup");
        g->screen = SCREEN_INVENTORY;
    } else if (hub_pressed(HUB_BUTTON_SKILLS, mouse)) {
        audio_play("Click3pickup");
        /* Opening the screen puts the menu clip back on its first frame, so
           the swatches beside the attributes start coloured again. */
        g->stat_points_spent = 0;
        g->screen = SCREEN_TALENTS;
    } else if (hub_pressed(HUB_BUTTON_MAP, mouse)) {
        audio_play("Click3pickup");
        g->screen = SCREEN_MAP;
    } else if (hub_pressed(HUB_BUTTON_SAVE, mouse)) {
        audio_play("Click3pickup");
        if (save_write(&g->campaign,
                       save_slot_path(g->campaign.slot)) != 0)
            game_notice(g, "Could not write the save file.");
    } else if (hub_pressed(HUB_BUTTON_OPTIONS, mouse)) {
        audio_play("Click3pickup");
        g->screen = SCREEN_SETTINGS;
    } else if (hub_pressed(HUB_BUTTON_RESPEC, mouse)) {
        audio_play("Click3pickup");
        g->campaign.stats.respec_used++;
        character_respec(&g->campaign.player);
    }
}



/* ------------------------------------------------------ the hub's welcome */

/* The story stops the player on the hub at seven points to explain something.
   Krin.progressSpeech is keyed by how far the story has got, and each entry
   is a title and a body out of NAVTITLE2 and NAVTEXT2; the fade behind it
   holds the hub still until it is clicked away. Proceed! on the victory
   screen is what offers one, and winning a fight re-arms the offer. */
static const struct { int32_t at; int32_t say; } HUB_NOTES[] = {
    {3, 0}, {4, 1}, {5, 2}, {10, 3}, {16, 4}, {20, 5}, {33, 6},
};

void game_hub_note(Game *g)
{
    if (g->hub_note_done)
        return;
    g->hub_note_done = 1;
    g->hub_note = -1;
    for (size_t i = 0; i < sizeof(HUB_NOTES) / sizeof(HUB_NOTES[0]); i++)
        if (HUB_NOTES[i].at == g->campaign.progress_battle)
            g->hub_note = HUB_NOTES[i].say;
}

int game_hub_note_up(const Game *g)
{
    return g->hub_note >= 0;
}

/* The note over the hub: the fade across the whole stage, the panel on it,
   and the two lines. Nothing else on the hub answers while it is up. */
static void draw_hub_note(Game *g, Vector2 mouse)
{
    if (!game_hub_note_up(g))
        return;
    /* The fade across the stage and the panel on it, drawn as the clip's own
       two pieces. The clip cannot be exported whole: it carries text fields,
       and the decompiler bakes their design-time copy into the picture -- the
       close line comes out as "< EWOINENJFEONVFOENV >". (The other
       stage-sized clip on this frame, @1242, is the black the screens change
       behind, nothing to do with the note.) */
    draw_clip_parts(HUB_SCREEN, "krinNavFadeSpeech", NO_OFFSET, NULL, NULL,
                    WHITE);
    draw_field_wrapped(chrome_field(HUB_SCREEN, "@1241"), NO_OFFSET,
                       lang_text("NAVTITLE2", g->hub_note));
    draw_field_wrapped(chrome_field(HUB_SCREEN, "@1240"), NO_OFFSET,
                       lang_text("NAVTEXT2", g->hub_note));
    /* The clip's own frame sets this from the text table; what it was
       authored with is placeholder. */
    draw_field(text_field_var(HUB_SCREEN, "texter"), NO_OFFSET,
               lang_text("SYSTEM", 8));
    const StageButton *close = stage_button(HUB_SCREEN, HUB_NOTE_CLOSE, 0);
    if (close)
        draw_button_state(close, CheckCollisionPointRec(mouse,
                              (Rectangle){close->x, close->y, close->width,
                                          close->height}), WHITE);
}

/* The furniture along the bottom of the hub: the panels, the row of buttons
   and their icons, the bar that says how far through the zone the player is,
   and the three lines beside it. Every menu the hub opens is a clip laid over
   this, so it stays on screen behind all of them -- which is why the original
   never shows a bare menu with nothing under it. */
static void draw_hub_panel(Game *g, Vector2 mouse)
{
    const ZoneDef *zone = campaign_zone(&g->campaign);
    draw_screen_chrome(HUB_SCREEN);
    draw_screen_buttons(HUB_SCREEN, mouse);

    /* How far through the zone the player is. The widget is a track with a
       fill inside it, and the original scales only the fill -- the track is
       left alone, which is why the bar does not shrink with it. */
    const StageChrome *bar = stage_chrome(HUB_SCREEN, "krinXbarPro");
    if (bar && zone) {
        float total = (float)(zone->last_battle - zone->first_battle);
        float done = 1.0f + (float)(g->campaign.progress_battle
                                    - zone->first_battle);
        if (done > total)
            done = total;
        const Texture2D *track = asset_texture(BAR_TRACK, 1);
        if (track) {
            Vector2 off = asset_frame_offset(BAR_TRACK, 1);
            draw_texture_placed(track, bar->x, bar->y, bar->scale_x,
                                bar->scale_y, off.x, off.y, WHITE);
        }
        /* The fill is lit: its placement carries a glow, so what is drawn is
           the fill with the halo already under it. Cutting it to the fraction
           cuts the halo with it, which is what scaling the clip does. */
        const Texture2D *fill = asset_texture(BAR_FILL, 1);
        if (fill && total > 0) {
            float fraction = done / total;
            Vector2 off = asset_frame_offset(BAR_FILL, 1);
            Rectangle box = placed_texture(fill,
                                           bar->x + BAR_FILL_X * bar->scale_x,
                                           bar->y, bar->scale_x, bar->scale_y,
                                           off.x, off.y);
            /* The glow's margin is outside the art, so only the art's own
               width takes the fraction. */
            float edge = (fill->width - BAR_FILL_WIDTH) / 2.0f;
            float shown = edge * 2 + BAR_FILL_WIDTH * fraction;
            box.width *= shown / fill->width;
            Rectangle src = {0, 0, shown, (float)fill->height};
            DrawTexturePro(*fill, src, box, (Vector2){0, 0}, 0.0f, WHITE);
        }
    }

    /* The three lines the original builds beside the bar. */
    if (zone) {
        const char *title = TextFormat("%s%d", lang_text("SYSTEM", 9),
                                       zone->zone + 1);
        const char *where = TextFormat("%s: %s", zone->name, zone->subtitle);
        draw_field(hub_field("@132"), NO_OFFSET, title);
        /* The name is drawn twice, a dark copy under a light one. */
        draw_field(hub_field("@130"), NO_OFFSET, where);
        draw_field(hub_field("@133"), NO_OFFSET, where);
        draw_field(hub_field("@131"), NO_OFFSET,
                   TextFormat("%s%d", lang_text("SYSTEM", 10),
                              g->campaign.progress_battle - 1));
    }
}


/* The hub, laid out from the original's Navigation frame: the zone's own
   scene across the top with the markers that start a fight or open the shop,
   the row of buttons along the bottom, and the zone's name and progress
   beside them. */
void screen_zone_draw(Game *g, Vector2 mouse)
{
    const ZoneDef *zone = campaign_zone(&g->campaign);

    ClearBackground(BLACK);
    /* The scene, one frame of the clip per zone. The markers are not in it:
       they are a clip of their own, and the art build takes them out so they
       can be drawn here and turn. */
    const char *label = (zone && zone->zone < SONNY_ZONE_LABEL_COUNT)
                      ? SONNY_ZONE_LABELS[zone->zone] : NULL;
    Vector2 scene = {SONNY_ZONE_SCREEN.x, SONNY_ZONE_SCREEN.y};
    if (label)
        asset_draw_placed(label, 1, scene, 1.0f, WHITE);

    /* The markers, turning. The clip is its own loop at the SWF's frame rate,
       and every marker on a scene is the same clip, so they turn together. */
    if (label) {
        int32_t frame = 1;
        if (SONNY_MARKER_FRAMES > 0)
            frame = 1 + (int32_t)(GetTime() * SONNY_MARKER_FPS)
                        % SONNY_MARKER_FRAMES;
        for (int32_t i = 0; ; i++) {
            const ZoneMarker *m = zone_marker(label, i);
            if (!m)
                break;
            /* Two of these are squashed by their placement, so the two scales
               are kept apart. */
            const Texture2D *tex = asset_texture(m->style, frame);
            if (!tex)
                continue;
            Vector2 offset = asset_frame_offset(m->style, frame);
            draw_texture_placed(tex, scene.x + m->x, scene.y + m->y,
                                m->scale_x, m->scale_y, offset.x, offset.y,
                                WHITE);
        }
    }

    draw_hub_panel(g, mouse);
    draw_hub_note(g, mouse);
}

/* ----------------------------------------------------------- world map */

/* The map, which the root timeline stops on its own frame for: one picture
   with a marker standing on each zone. A marker shows once the player has
   reached the zone it leads to -- the original's
   `progressLevelOn >= progressArray[i][0] - 1`, which is the battle before
   that zone's first -- and the map draws its own lines between the ones
   showing. Clicking one travels there. */
#define MAP_SCREEN "overMap"
#define MAP_CLIP   "krinMapper"

/* The line the map draws between one marker and the one before it: two wide,
   black, and nearly transparent. */
#define MAP_LINE_WIDTH 2.0f
#define MAP_LINE_ALPHA 77          /* lineStyle(2, 0, 30) */

static int zone_unlocked(const Campaign *c, const ZoneDef *zone)
{
    return c->progress_battle >= zone->first_battle - 1;
}

/* Whether this marker's zone has been reached. The marker's index is the
   zone's, which is how the frame's own loop pairs them up. */
static int marker_unlocked(const Campaign *c, const MapMarker *m)
{
    return m->zone < SONNY_ZONE_COUNT
        && zone_unlocked(c, &SONNY_ZONES[m->zone]);
}

static Rectangle marker_rect(const MapMarker *m)
{
    return (Rectangle){m->x - m->width / 2, m->y - m->height / 2,
                       m->width, m->height};
}

void screen_map_update(Game *g, Vector2 mouse)
{
    g->hovered_item = -1;
    for (int i = 0; i < SONNY_MAP_MARKER_COUNT; i++) {
        const MapMarker *m = &SONNY_MAP_MARKERS[i];
        if (!marker_unlocked(&g->campaign, m) || !hit(marker_rect(m), mouse))
            continue;
        g->hovered_item = m->zone;
        if (!ui_clicked())
            break;
        /* Travelling changes where you are, not how far you have got. */
        g->campaign.zone = m->zone;
        audio_play("Click3pickup");
        g->screen = SCREEN_ZONE;
        return;
    }
    /* There is no way off the map but to pick somewhere: the original's
       frame has no close of its own, and the first zone's marker is always
       showing. */
}

void screen_map_draw(Game *g, Vector2 mouse)
{
    (void)mouse;
    ClearBackground(BLACK);
    /* The map is a black backing with the picture over it. The picture's
       export carries a margin its filter spread into, which the original
       never shows, so it is kept inside the backing. */
    const ClipPart *backing = clip_part(MAP_SCREEN, MAP_CLIP, 0);
    if (backing) {
        Rectangle box = placed_rect(backing->x, backing->y, backing->scale_x,
                                    backing->scale_y, backing->width,
                                    backing->height, backing->origin_x,
                                    backing->origin_y);
        BeginScissorMode((int)box.x, (int)box.y, (int)box.width,
                         (int)box.height);
    }
    draw_clip_parts(MAP_SCREEN, MAP_CLIP, NO_OFFSET, NULL, NULL, WHITE);
    if (backing)
        EndScissorMode();

    /* The route: a line from each marker showing back to the one before it. */
    for (int i = 1; i < SONNY_MAP_MARKER_COUNT; i++) {
        const MapMarker *m = &SONNY_MAP_MARKERS[i];
        if (!marker_unlocked(&g->campaign, m))
            continue;
        const MapMarker *back = &SONNY_MAP_MARKERS[i - 1];
        DrawLineEx((Vector2){m->x, m->y}, (Vector2){back->x, back->y},
                   MAP_LINE_WIDTH, (Color){0, 0, 0, MAP_LINE_ALPHA});
    }

    for (int i = 0; i < SONNY_MAP_MARKER_COUNT; i++) {
        const MapMarker *m = &SONNY_MAP_MARKERS[i];
        if (!marker_unlocked(&g->campaign, m))
            continue;
        asset_draw_placed(TextFormat("#%d", m->character), 1,
                          (Vector2){m->x, m->y}, 1.0f, WHITE);
    }

    /* What the pointer is on: the tooltip the marker's own handler fills,
       which is the zone's name over what the place is called. */
    if (g->hovered_item >= 0 && g->hovered_item < SONNY_ZONE_COUNT)
        game_tooltip(g, lang_text("ZONES", g->hovered_item),
                     lang_text("ZONES2", g->hovered_item));
}

/* A slot's own empty square, which the menu screens share. */
static void draw_slot_art(const char *menu, const char *prefix, int i);

/* ------------------------------------------------------------- talents */

/* The ability screen, which the menu clip carries on its "skills" frame: the
   tree down the left, who the character is and their attributes down the
   middle, and the combat action bar over the pool of everything they know on
   the right. Everything below is laid out from that frame.

   The frame's own script (sprite 1503, frame 25) is what says where the text
   comes from; the tree's node clip carries the rest, in its load handler. */
#define MENU_SKILLS "skills"

/* The button every tree node carries, which is both what the node answers in
   and, being the only thing that tells a node apart from the art, how big its
   box is. */
#define TREE_NODE_BUTTON 1418

/* The plus beside each attribute, by the button's own character: the handlers
   hang off these, and each adds to one of Krin.StatSets0. */
#define STAT_BUTTON_VITALITY 1451
#define STAT_BUTTON_STRENGTH 1448
#define STAT_BUTTON_MAGIC    1449
#define STAT_BUTTON_SPEED    1450

/* The swatches behind those buttons. They are one clip with a frame for each
   state: coloured while there are points to spend, and grey once the last one
   goes -- which the original only ever does part way through a visit, since
   opening the screen puts the clip back on its first frame. */
#define STAT_COLOUR_SLOT "statColor"
#define STAT_COLOUR_DEAD "#1444@8"

/* The ability pool, which the screen fills with one row per ability the
   character knows. The rows go in two columns, thirty-five apart down and
   twenty-five below the top of the pool, exactly as KrinCreateAbilityMatrix
   places them. */
#define POOL_SLOT      "talentPool"
#define POOL_ROW       "talenter"
#define POOL_ROW_STEP  35.0f
#define POOL_ROW_TOP   25.0f
#define POOL_COLUMN_A  5.0f
#define POOL_COLUMN_B  85.0f
/* Where the row's own orb sits in it. */
#define POOL_ROW_ORB_X 28.5f
#define POOL_ROW_ORB_Y 0.05f

/* The frame the orb clip shows for a slot with nothing assigned to it: the
   screen does gotoAndStop("Empty") on any such slot of the bar. */
#define EMPTY_ORB "Empty"

/* How wide an orb answers to the pointer, and how wide a pool row is. */
#define ORB_HIT        24.0f
#define POOL_ROW_WIDTH 64.0f

/* The orb inside a tree node sits a fraction above the node's own point. */
#define TREE_NODE_ORB_Y (-0.35f)

/* The branches the tree draws between a node and each of its prerequisites:
   a six-wide black line with a two-wide one over it, gold once the
   prerequisite is learned and near-black while it is not. */
#define TREE_LINE_BACK   6.0f
#define TREE_LINE_FRONT  2.0f
#define TREE_LINE_OPEN   (Color){0xFF, 0xCC, 0x00, 255}
#define TREE_LINE_SHUT   (Color){0x2B, 0x2B, 0x2B, 255}

/* One of the skills frame's fields, by the name the frame gives it. */
static const TextField *skill_field(const char *name, int32_t occurrence)
{
    return text_field_named(MENU_SCREEN, MENU_SKILLS, name, occurrence);
}

static Rectangle talent_rect(int32_t node)
{
    const TalentSlot *slot = talent_slot(node);
    const StageButton *box = stage_button(MENU_SKILLS, TREE_NODE_BUTTON, 0);
    if (!slot || !box) {
        int col = node % 7;
        int row = node / 7;
        return (Rectangle){70 + col * 92, 110 + row * 92, 74, 74};
    }
    return (Rectangle){slot->x - box->width / 2, slot->y - box->height / 2,
                       box->width, box->height};
}

/* The rank readout over a node: the rank and the tier it tops out at, each
   drawn twice -- black, then white a pixel up and to the left. The frame lays
   an identical set over every node, but in the tree's own stacking order
   rather than the nodes' own, so a node takes the set laid nearest to it. */
#define TREE_RANK_FIELDS 4

static const TextField *node_field(const TalentSlot *slot, int32_t which)
{
    int32_t best = -1;
    float nearest = 0;
    for (int32_t i = 0; i < SONNY_TALENT_SLOT_COUNT; i++) {
        const TextField *f = text_field(MENU_SCREEN, MENU_SKILLS,
                                        i * TREE_RANK_FIELDS);
        if (!f)
            break;
        float dx = f->x - slot->x, dy = f->y - slot->y;
        float away = dx * dx + dy * dy;
        if (best < 0 || away < nearest) {
            best = i;
            nearest = away;
        }
    }
    if (best < 0)
        return NULL;
    return text_field(MENU_SCREEN, MENU_SKILLS,
                      best * TREE_RANK_FIELDS + which);
}

/* What a node's orb shows. A node that grants a move shows that move's icon,
   at the rank currently held -- or the first rank's while nothing is learned,
   which is what the tree's own load handler points the orb at. A passive
   shows its buff's icon instead, which the orb clip has its own frame for. */
static const char *node_icon(const Character *c, int32_t node)
{
    const TalentDef *t = &SONNY_TALENTS[node];
    if (t->passive)
        return t->buff_name;
    int32_t rank = c->rank[node];
    const AbilityDef *a = ability_by_id(t->ability_id
                                        + (rank > 0 ? rank - 1 : 0));
    return a ? a->icon : NULL;
}

/* The eight slots of the combat action bar, which are the same ring the fight
   puts round a unit -- here the menu places it flat on the screen, so each
   orb is taken from the selector's own pieces by name. */
static const SlotPiece *bar_slot(int32_t slot)
{
    char name[16];
    snprintf(name, sizeof(name), "thing%d", (int)slot);
    for (int32_t i = 0; ; i++) {
        const SlotPiece *piece = slot_piece(MENU_SKILLS, "selector", i);
        if (!piece)
            return NULL;
        if (strcmp(piece->name, name) == 0)
            return piece;
    }
}

/* Where the pool's `index`-th row goes, in stage coordinates. */
static Vector2 pool_row(int32_t index)
{
    const MenuSlot *pool = menu_slot(MENU_SKILLS, POOL_SLOT);
    Vector2 at = {0, 0};
    if (!pool)
        return at;
    at.x = pool->x + (index % 2 ? POOL_COLUMN_B : POOL_COLUMN_A);
    at.y = pool->y + POOL_ROW_TOP + (index / 2) * POOL_ROW_STEP;
    return at;
}

/* The four attributes the screen offers, in the order the frame stacks their
   rows: each is one of SYSTEM's names, one of the character's derived
   numbers, and the button that buys another point of it. */
static const struct {
    const char *label, *value;
    int32_t     button;
    int32_t     stat;
} SKILL_ATTRIBUTES[4] = {
    {"@573", "@577", STAT_BUTTON_VITALITY, 0},
    {"@574", "@578", STAT_BUTTON_STRENGTH, 1},
    {"@575", "@579", STAT_BUTTON_MAGIC,    2},
    {"@576", "@580", STAT_BUTTON_SPEED,    3},
};

void screen_talents_draw(Game *g, Vector2 mouse)
{
    (void)mouse;
    const Character *c = &g->campaign.player;

    ClearBackground(BLACK);
    /* The hub stays behind the menu, as it does in the original. */
    draw_hub_panel(g, mouse);
    draw_clip_parts(MENU_SCREEN, MENU_SKILLS, NO_OFFSET, NULL, NULL, WHITE);

    /* The four headings, which the frame reads out of the language table. */
    draw_field(skill_field("@603", 0), NO_OFFSET, lang_text("K_TITLE1", 0));
    draw_field(skill_field("@601", 0), NO_OFFSET, lang_text("K_TITLE2", 0));
    draw_field(skill_field("@602", 0), NO_OFFSET, lang_text("K_TITLE3", 0));
    draw_field(skill_field("@604", 0), NO_OFFSET, lang_text("MENU", 16));

    /* Who the character is, and what they have to spend. */
    draw_field(skill_field("@583", 0), NO_OFFSET, lang_text("NAVTITLE2", 3));
    draw_field(skill_field("@584", 0), NO_OFFSET,
               TextFormat("%s%d %s", lang_text("MENU", 0), c->level,
                          lang_text("CLASS", c->class_template
                                    ? c->class_template->id - 1 : 0)));
    draw_field(skill_field("@581", 0), NO_OFFSET,
               TextFormat("%s:", lang_text("SKLOAD", 0)));
    draw_field(skill_field("@585", 0), NO_OFFSET,
               TextFormat("%d", character_unspent_skill_points(c)));
    draw_field(skill_field("@582", 0), NO_OFFSET,
               TextFormat("%s:", lang_text("SPLOAD", 0)));
    draw_field(skill_field("@586", 0), NO_OFFSET,
               TextFormat("%d", character_unspent_stat_points(c)));
    /* The tip. Every tree node carries a rank readout whose own fields are
       called "@1" to "@4" as well, so the screen's own is the one after
       them. */
    draw_field_wrapped(skill_field("@1", SONNY_TALENT_SLOT_COUNT), NO_OFFSET,
                       lang_text("MENU", 44));

    /* The attributes: the swatches behind the row of plus buttons, then each
       row's name, number and button. */
    const MenuSlot *swatch = menu_slot(MENU_SKILLS, STAT_COLOUR_SLOT);
    if (swatch && swatch->width > 0) {
        const Texture2D *tex = asset_texture(
            g->stat_points_spent ? STAT_COLOUR_DEAD
                                 : TextFormat("#%d", swatch->character), 1);
        if (tex) {
            draw_texture_placed(tex, swatch->x, swatch->y, swatch->scale,
                                swatch->scale, swatch->origin_x,
                                swatch->origin_y, WHITE);
        }
    }
    DerivedStats stats = character_derive(c);
    const double shown[4] = {stats.life, stats.strength, stats.magic,
                             stats.speed};
    for (int i = 0; i < 4; i++) {
        draw_button_art(stage_button(MENU_SKILLS, SKILL_ATTRIBUTES[i].button,
                                     0), WHITE);
        draw_field(skill_field(SKILL_ATTRIBUTES[i].label, 0), NO_OFFSET,
                   TextFormat("%s:", lang_text("SYSTEM",
                                               SKILL_ATTRIBUTES[i].stat)));
        draw_field(skill_field(SKILL_ATTRIBUTES[i].value, 0), NO_OFFSET,
                   TextFormat("%d", (int32_t)shown[i]));
    }

    /* The tree. The branches go in first, under the nodes, and each is drawn
       twice: a thick black line with a thin coloured one over it, gold once
       the prerequisite it comes from is learned. */
    for (int32_t node = 0; node < SONNY_TALENT_COUNT; node++) {
        const TalentSlot *from = talent_slot(node);
        if (!from)
            continue;
        for (int32_t i = 0; i < SONNY_TALENTS[node].prereq_count; i++) {
            int32_t prereq = SONNY_TALENTS[node].prereq[i];
            const TalentSlot *to = prereq >= 0 ? talent_slot(prereq) : NULL;
            if (!to)
                continue;
            Vector2 a = {from->x, from->y}, b = {to->x, to->y};
            DrawLineEx(a, b, TREE_LINE_BACK, BLACK);
            DrawLineEx(a, b, TREE_LINE_FRONT,
                       c->rank[prereq] > 0 ? TREE_LINE_OPEN : TREE_LINE_SHUT);
        }
    }
    for (int32_t node = 0; node < SONNY_TALENT_COUNT; node++) {
        const TalentSlot *slot = talent_slot(node);
        if (!slot)
            continue;
        Vector2 centre = {slot->x, slot->y + TREE_NODE_ORB_Y};
        /* A node with nothing spent on it wears the same black disc the ring
           puts over a move that cannot be used. */
        draw_orb(node_icon(c, node), centre, slot->scale,
                 c->rank[node] > 0 ? 0 : ORB_DIM_TALENT, 0);
        /* The rank over it, which the tree hides unless the space bar is
           down. */
        if (!IsKeyDown(KEY_SPACE))
            continue;
        const char *rank = TextFormat("%d", c->rank[node]);
        const char *tier = TextFormat("%d", SONNY_TALENTS[node].max_rank);
        draw_field(node_field(slot, 0), NO_OFFSET, rank);
        draw_field(node_field(slot, 1), NO_OFFSET, tier);
        draw_field(node_field(slot, 2), NO_OFFSET, rank);
        draw_field(node_field(slot, 3), NO_OFFSET, tier);
    }

    /* The combat action bar: the eight slots of the loadout, each an orb, and
       a slot with nothing on it showing the ring's own empty frame. */
    for (int32_t i = 0; i < SONNY_MOVE_SLOTS; i++) {
        const SlotPiece *piece = bar_slot(i);
        if (!piece)
            continue;
        const AbilityDef *a = ability_by_id(c->move_matrix[i]);
        Vector2 centre = {piece->x, piece->y};
        draw_orb((a && a->id != 0) ? a->icon : EMPTY_ORB, centre,
                 piece->scale_x, 0, 0);
    }

    /* The pool below it: one row per ability the character knows, two to a
       line, inside the pool's own box. */
    const MenuSlot *pool = menu_slot(MENU_SKILLS, POOL_SLOT);
    if (pool && pool->width > 0) {
        Rectangle box = placed_rect(pool->x, pool->y, pool->scale, pool->scale,
                                    pool->width, pool->height, pool->origin_x,
                                    pool->origin_y);
        int32_t known[SONNY_TALENT_MAX + SONNY_MOVE_SLOTS];
        int32_t count = character_known_abilities(c, known,
                                                  (int32_t)(sizeof(known)
                                                            / sizeof(known[0])));
        draw_slot_art(MENU_SKILLS, POOL_SLOT, -1);
        BeginScissorMode((int)box.x, (int)box.y, (int)box.width,
                         (int)box.height);
        for (int32_t i = 0; i < count; i++) {
            Vector2 at = pool_row(i);
            draw_clip_parts(MENU_SCREEN, POOL_ROW, at, NULL, NULL, WHITE);
            /* The original walks the list of known abilities with for..in,
               which hands back an array's indices last to first, so the row
               the pool fills first is the last ability learned. */
            const AbilityDef *a = ability_by_id(known[count - 1 - i]);
            Vector2 centre = {at.x + POOL_ROW_ORB_X, at.y + POOL_ROW_ORB_Y};
            draw_orb(a ? a->icon : NULL, centre, 1.0f, 0, 0);
        }
        EndScissorMode();
    }
}

/* Put what the pointer is carrying on one of the eight slots, the way
   addMoveForPlayer does: carrying nothing empties the slot, and a move can
   only take as many slots at once as its own number allows. */
static void place_on_bar(Game *g, int32_t slot)
{
    Character *c = &g->campaign.player;
    if (g->carrying == 0) {
        c->move_matrix[slot] = 0;
        return;
    }
    const AbilityDef *a = ability_by_id(g->carrying);
    int32_t already = 0;
    for (int32_t i = 0; i < SONNY_MOVE_SLOTS; i++)
        if (c->move_matrix[i] == g->carrying)
            already++;
    if (a && already < a->bar_copies)
        c->move_matrix[slot] = g->carrying;
    else
        game_notice(g, "%s", lang_text("SKILLERROR", 0));
    g->carrying = 0;
}

void screen_talents_update(Game *g, Vector2 mouse)
{
    Character *c = &g->campaign.player;
    g->hovered_item = -1;

    /* The action bar: pick an ability up off the pool or the tree, drop it on
       a slot. */
    for (int32_t i = 0; i < SONNY_MOVE_SLOTS; i++) {
        const SlotPiece *piece = bar_slot(i);
        if (!piece)
            continue;
        Rectangle box = {piece->x - ORB_HIT / 2, piece->y - ORB_HIT / 2,
                         ORB_HIT, ORB_HIT};
        if (!hit(box, mouse))
            continue;
        const AbilityDef *on = ability_by_id(c->move_matrix[i]);
        if (on && on->id != 0)
            game_tooltip(g, on->name, on->tooltip);
        else
            game_tooltip(g, lang_text("SKILLNONE", 0),
                         lang_text("SKILLTUT", 0));
        if (ui_clicked()) {
            audio_play("Click2putdown");
            place_on_bar(g, i);
            return;
        }
    }

    /* The pool below it: clicking a row picks that ability up. */
    {
        int32_t known[SONNY_TALENT_MAX + SONNY_MOVE_SLOTS];
        int32_t count = character_known_abilities(c, known,
                                                  (int32_t)(sizeof(known)
                                                            / sizeof(known[0])));
        for (int32_t i = 0; i < count; i++) {
            Vector2 at = pool_row(i);
            Rectangle box = {at.x, at.y - ORB_HIT / 2, POOL_ROW_WIDTH,
                             ORB_HIT};
            if (!hit(box, mouse))
                continue;
            const AbilityDef *a = ability_by_id(known[count - 1 - i]);
            if (!a)
                continue;
            game_tooltip(g, a->name, a->tooltip);
            if (ui_clicked()) {
                audio_play("Click3pickup");
                g->carrying = a->id;
                return;
            }
        }
    }

    /* A point of an attribute, which the original buys one click at a time
       and stops offering the moment the last one is gone. */
    for (int i = 0; i < 4; i++) {
        const StageButton *b = stage_button(MENU_SKILLS,
                                            SKILL_ATTRIBUTES[i].button, 0);
        if (!b || !ui_clicked()
            || !hit((Rectangle){b->x, b->y, b->width, b->height}, mouse))
            continue;
        if (character_unspent_stat_points(c) <= 0)
            break;
        c->spent[SKILL_ATTRIBUTES[i].stat] += 1;
        c->spent_stat_points++;
        /* The swatches go grey as the last point goes, and stay that way
           until the screen is opened again. */
        if (character_unspent_stat_points(c) == 0)
            g->stat_points_spent = 1;
        return;
    }

    for (int32_t node = 0; node < SONNY_TALENT_COUNT; node++) {
        if (!hit(talent_rect(node), mouse))
            continue;
        g->hovered_item = node;
        /* What the node calls itself: its rank out of its tier, then the
           move's own name and what it does. */
        const TalentDef *t = &SONNY_TALENTS[node];
        int32_t rank = c->rank[node];
        const AbilityDef *shown = ability_by_id(t->ability_id
                                                + (rank > 0 ? rank - 1 : 0));
        if (shown)
            game_tooltip(g, TextFormat("(%d/%d)  %s", rank, t->max_rank,
                                       shown->name),
                         rank > 0 ? shown->tooltip
                                  : lang_text("SKILLTALENTTIP2", 0));
        if (!ui_clicked())
            break;

        TalentError err = character_learn(c, node);
        switch (err) {
        case TALENT_OK:
            audio_play("Click2putdown");
            /* A newly learned active talent goes on the first free slot. */
            if (!SONNY_TALENTS[node].passive) {
                for (int32_t i = 0; i < SONNY_MOVE_SLOTS; i++) {
                    if (c->move_matrix[i] == 0) {
                        c->move_matrix[i] = c->skill_adder[node];
                        break;
                    }
                }
            }
            break;
        case TALENT_NO_POINTS:
            game_notice(g, "%s", lang_text("MENU", 20));
            break;
        default:
            break;
        }
        break;
    }

    if (menu_close_pressed(mouse))
        g->screen = SCREEN_ZONE;
}

/* ----------------------------------------------------------- inventory */

/* Whoever the sheet is turned to: the player, or one of the party built from
   the table. */
static const Character *menu_character(Game *g, Character *scratch)
{
    if (g->menu_member <= 0)
        return &g->campaign.player;
    campaign_ally(&g->campaign, g->menu_member, scratch);
    return scratch;
}


/* Whether this item may go in that equipment row. The original checks three
   things: the row takes that kind of item -- a row's kind is its own index
   plus two -- the item is for this class or for any, and the character is
   high enough level for it. Carrying nothing always passes, which is how a
   row is emptied. */
static int item_fits(const Character *c, int32_t item_id, int32_t row)
{
    if (item_id == 0)
        return 1;
    const ItemDef *item = item_by_id(item_id);
    if (!item || item->slot != row + 2)
        return 0;
    int32_t class_id = c->class_template ? c->class_template->id : 0;
    if (item->class_req != 0 && item->class_req != class_id)
        return 0;
    return item->level_req <= c->level;
}

/* A slot the pointer pressed: what it holds and what the pointer is carrying
   change places, which is all any of these slots ever does. */
static void swap_carried(Game *g, int32_t *slot)
{
    audio_play(g->carried_item == 0 ? "Click3pickup" : "Click2putdown");
    int32_t held = *slot;
    *slot = g->carried_item;
    g->carried_item = held;
}

/* The same, for an equipment row of whoever the sheet is turned to: what
   comes off stops counting and what goes on starts, which is the only way
   anything ever enters the running total a character carries. */
static void swap_equipped(Game *g, int32_t row)
{
    Campaign *c = &g->campaign;
    int32_t member = g->menu_member;
    int32_t *worn = (member <= 0) ? &c->player.equip[row]
                                  : &c->ally_equip[member][row];
    double *sets = (member <= 0) ? c->player.stat_sets
                                 : c->ally_stat_sets[member];
    const ItemDef *off = item_by_id(*worn);
    swap_carried(g, worn);
    const ItemDef *on = item_by_id(*worn);
    for (int32_t i = 0; i < SONNY_STATS; i++) {
        if (off)
            sets[i] -= off->stat[i];
        if (on)
            sets[i] += on->stat[i];
    }
    if (member <= 0) {
        for (int32_t e = 0; e < SONNY_ELEMENTS; e++) {
            if (off) {
                c->player.per_sets[e] -= off->per[e];
                c->player.def_sets[e] -= off->def[e];
            }
            if (on) {
                c->player.per_sets[e] += on->per[e];
                c->player.def_sets[e] += on->def[e];
            }
        }
    }
}

/* The bag is a list rather than a grid of holes: the original keeps
   itemArray dense, so an item put down past the end is appended and one
   picked up off the end leaves nothing behind. */
static void swap_bag(Game *g, int32_t index)
{
    Campaign *c = &g->campaign;
    int32_t max = (int32_t)(sizeof(c->inventory) / sizeof(c->inventory[0]));
    if (index < c->inventory_count) {
        swap_carried(g, &c->inventory[index]);
        if (c->inventory[index] == 0) {
            for (int32_t i = index; i + 1 < c->inventory_count; i++)
                c->inventory[i] = c->inventory[i + 1];
            c->inventory_count--;
        }
    } else if (g->carried_item != 0 && c->inventory_count < max) {
        c->inventory[c->inventory_count++] = g->carried_item;
        g->carried_item = 0;
        audio_play("Click2putdown");
    }
}


/* The character screen's own layout: seven equipment slots either side of a
   doll preview, and a six-by-six bag grid to the right, all placed where the
   original's menu places them. The slot art is 34 square, drawn centred on
   its placement point. */
#define MENU_SLOT_SIZE 34.0f

static Rectangle named_slot(const char *menu, const char *prefix, int i,
                            Rectangle fallback)
{
    char name[32];
    snprintf(name, sizeof(name), "%s%d", prefix, i);
    const MenuSlot *slot = menu_slot(menu, name);
    if (!slot)
        return fallback;
    return (Rectangle){slot->x - MENU_SLOT_SIZE / 2,
                       slot->y - MENU_SLOT_SIZE / 2,
                       MENU_SLOT_SIZE, MENU_SLOT_SIZE};
}

/* The equipment slots and the bag, as the inventory frame places them. */
static Rectangle slot_rect(int i)
{
    return named_slot(MENU_INVENTORY, "playerSlot", i,
                      (Rectangle){70, 110 + i * 44, 300, 38});
}

static Rectangle bag_rect(int i)
{
    return named_slot(MENU_INVENTORY, "itemSlot", i,
                      (Rectangle){420, 110 + i * 34, 320, 30});
}

/* What a fight dropped, and the bag again -- the victory frame lays both out
   itself, on a different grid to the inventory's. */
static Rectangle drop_rect(int i)
{
    return named_slot(MENU_WIN, "dropSlot", i,
                      (Rectangle){70 + i * 150, 190, 140, 34});
}

static Rectangle win_bag_rect(int i)
{
    return named_slot(MENU_WIN, "itemSlot", i,
                      (Rectangle){420, 110 + i * 34, 320, 30});
}

/* A slot's own empty square, drawn where the frame places it. The engine
   fills the slot itself, but the square under it is the original's. */
static void draw_slot_art(const char *menu, const char *prefix, int i)
{
    char name[32];
    /* A slot the frame numbers, or -- with i below zero -- one it names
       outright, like the two bands of element bars. */
    if (i < 0)
        snprintf(name, sizeof(name), "%s", prefix);
    else
        snprintf(name, sizeof(name), "%s%d", prefix, i);
    const MenuSlot *slot = menu_slot(menu, name);
    if (!slot || slot->width <= 0)
        return;
    const Texture2D *tex = asset_texture(TextFormat("#%d", slot->character), 1);
    if (!tex)
        return;
    draw_texture_placed(tex, slot->x, slot->y, slot->scale, slot->scale,
                        slot->origin_x, slot->origin_y, WHITE);
}


/* An item's icon is a frame label on the icon sprite, named for the item. */
/* An item's picture: a frame of the clip every slot shows its contents
   through, labelled with the item's name and drawn at its own size on the
   middle of the slot, as the original attaches it. */
static void draw_item_icon(const ItemDef *item, Rectangle r, Color tint)
{
    if (!item || item->id == 0)
        return;
    Vector2 middle = {r.x + r.width / 2, r.y + r.height / 2};
    if (!asset_draw_placed(item->name, 1, middle, 1.0f, tint))
        ui_text(item->name, (int)r.x + 2, (int)(r.y + r.height / 2 - 4), 9,
                tint);
}


void screen_inventory_update(Game *g, Vector2 mouse)
{
    Character scratch;
    const Character *who = menu_character(g, &scratch);
    g->hovered_item = -1;

    for (int32_t i = 0; i < MENU_BAG_SLOTS; i++) {
        Rectangle r = bag_rect(i);
        if (!hit(r, mouse))
            continue;
        g->hovered_item = 100 + i;
        if (ui_clicked())
            swap_bag(g, i);
        break;
    }

    for (int i = 0; i < SONNY_EQUIP_SLOTS; i++) {
        Rectangle r = slot_rect(i);
        if (!hit(r, mouse))
            continue;
        g->hovered_item = i;
        /* A row only takes what belongs in it, and the row keeps what it has
           if the carried item does not fit. */
        if (ui_clicked() && item_fits(who, g->carried_item, i))
            swap_equipped(g, i);
        break;
    }

    if (menu_close_pressed(mouse))
        g->screen = SCREEN_ZONE;
}

/* One of the two bands of element bars. Each band is eight pieces, one per
   element, and the game gives each the element's own colour and stands it at
   a height for the number it shows -- twenty-five being the baseline every
   character starts at. */
static void draw_element_bars(const char *menu, const char *slot,
                              const double *values)
{
    for (int i = 0; i < SONNY_ELEMENT_DEF_COUNT; i++) {
        /* Each bar is drawn twice: a dim full-height one behind, then the
           value over it. The pieces come in that order. */
        const SlotPiece *back = slot_piece(menu, slot, i);
        const SlotPiece *front = slot_piece(menu, slot,
                                            SONNY_ELEMENT_DEF_COUNT + i);
        if (!back || !front)
            return;
        uint32_t rgb = SONNY_ELEMENT_DEFS[i].colour;
        Color colour = {(unsigned char)(rgb >> 16), (unsigned char)(rgb >> 8),
                        (unsigned char)rgb, 255};
        float fraction = (float)(values[i] / ELEMENT_BAR_FULL);
        if (fraction < 0) fraction = 0;
        if (fraction > 1) fraction = 1;

        Rectangle box = placed_rect(back->x, back->y, back->scale_x,
                                    back->scale_y, back->width, back->height,
                                    back->origin_x, back->origin_y);
        DrawRectangleRec(box, (Color){colour.r / 4, colour.g / 4,
                                      colour.b / 4, 255});
        Rectangle filled = box;
        filled.height = box.height * fraction;
        filled.y = box.y + box.height - filled.height;
        DrawRectangleRec(filled, colour);
    }
}

/* One of the inventory frame's fields, by the name the clip gives it. */
static const TextField *inv_field(const char *name)
{
    return text_field_named(MENU_SCREEN, MENU_INVENTORY, name, 0);
}

/* The character sheet, laid out from the menu clip's "inventory" frame: who
   the character is and what they are wearing on the left, their stats and
   their piercing and defense in the middle, and the bag on the right. */
/* The row of six along the bottom of the character screen: everyone the story
   has handed over, the ones not in the fighting line dimmed, and a frame
   round whoever is being looked at. The portrait clip is the same one the
   speech box uses, pointed at a frame by number. */
#define AVATAR_DIM   "#1314"
#define AVATAR_FRAME "#1317"

static void draw_party_row(Game *g, const char *menu, Vector2 mouse)
{
    Campaign *c = &g->campaign;
    for (int32_t i = 0; i < SONNY_PARTY_SIZE; i++) {
        const MenuSlot *slot = menu_slot(menu, TextFormat("fa%d", i));
        if (!slot || !campaign_has_friend(c, i))
            continue;
        Vector2 at = {slot->x, slot->y};
        /* gotoAndStop(friendArray[i]) by number: the first place is the
           player, and nothing moves the clip off him. */
        asset_draw_placed(TextFormat("#1312@%d", i == 0 ? 6 : i), 1, at,
                          slot->scale, WHITE);
        /* The player is always in the fight, so the dim only ever goes over
           the other five -- updateFriends starts its loop at one. */
        int in_line = (i == 0 || c->line[0] == i || c->line[1] == i);
        if (!in_line)
            asset_draw_placed(AVATAR_DIM, 1, at, slot->scale, WHITE);
        if (g->menu_member == i)
            asset_draw_placed(AVATAR_FRAME, 1, at, slot->scale, WHITE);

        Rectangle box = {at.x - slot->width / 2, at.y - slot->height / 2,
                         slot->width, slot->height};
        if (!hit(box, mouse))
            continue;
        game_tooltip(g, SONNY_PARTY[i].name,
                     lang_text("MENU", i != 0 ? 48 : 49));
        if (!ui_clicked())
            continue;
        /* Holding shift moves someone in or out of the fighting line; a
           plain press turns the sheet over to them. */
        if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
            if (i == 0)
                continue;       /* the player is always in it */
            if (c->line[0] == i)
                c->line[0] = 0;
            else if (c->line[1] == i)
                c->line[1] = 0;
            else if (c->line[0] == 0)
                c->line[0] = i;
            else
                c->line[1] = i;
        } else {
            g->menu_member = i;
        }
    }
}

void screen_inventory_draw(Game *g, Vector2 mouse)
{
    const Campaign *c = &g->campaign;
    Character scratch;
    const Character *who = menu_character(g, &scratch);

    ClearBackground(BLACK);
    /* The hub's own furniture stays behind the menu, as it does in the
       original: the row of buttons and the zone's progress are still there. */
    draw_hub_panel(g, mouse);
    draw_clip_parts(MENU_SCREEN, MENU_INVENTORY, NO_OFFSET, NULL, NULL, WHITE);

    draw_field(inv_field("@881"), NO_OFFSET,
               g->menu_member > 0 ? SONNY_PARTY[g->menu_member].name
                                  : lang_text("NAVTITLE2", 3));
    draw_field(inv_field("@882"), NO_OFFSET,
               TextFormat("%s%d %s", lang_text("MENU", 0), who->level,
                          /* Krin.ClassStats[0] is Krin.Class + 1, so the
                             name the class menu offered is one back. */
                          lang_text("CLASS", who->class_template
                                    ? who->class_template->id - 1 : 0)));

    /* The five stats. The frame gives each its own label and value field, in
       its own colour, and the names come from the game's text table. */
    static const char *const STAT_LABEL[5] = {"@883", "@884", "@885", "@886",
                                              "@887"};
    static const char *const STAT_VALUE[5] = {"@888", "@889", "@890", "@891",
                                              "@892"};
    DerivedStats stats = character_derive(who);
    const double shown[5] = {stats.life, stats.strength, stats.magic,
                             stats.speed, stats.focus};
    for (int i = 0; i < 5; i++) {
        draw_field(inv_field(STAT_LABEL[i]), NO_OFFSET,
                   TextFormat("%s:", lang_text("SYSTEM", i)));
        draw_field(inv_field(STAT_VALUE[i]), NO_OFFSET,
                   TextFormat("%d", (int32_t)shown[i]));
    }
    draw_field(inv_field("@893"), NO_OFFSET, lang_text("SYSTEM", 5));
    draw_field(inv_field("@894"), NO_OFFSET, lang_text("SYSTEM", 6));
    draw_element_bars(MENU_INVENTORY, "perBarShow", stats.per);
    draw_element_bars(MENU_INVENTORY, "defBarShow", stats.def);

    draw_field(inv_field("@1027"), NO_OFFSET, lang_text("MENU", 17));
    draw_field(inv_field("@1032"), NO_OFFSET,
               TextFormat("%d%%", (int32_t)c->player.xp));
    draw_field(inv_field("@1059"), NO_OFFSET, lang_text("MENU", 14));
    draw_field(inv_field("@1052"), NO_OFFSET, TextFormat("%d", c->euros));
    draw_field(inv_field("@1053"), NO_OFFSET, EURO);

    /* The doll. The menu attaches each part at its own place rather than
       through the model sprite, so the model's own matrices do not carry
       over; it is drawn from the model, stood where the frame stands it. */
    const MenuSlot *doll = menu_slot(MENU_INVENTORY, "chest");
    if (doll) {
        DollSpec spec;
        memset(&spec, 0, sizeof(spec));
        spec.gender = "M";
        spec.skin = "ONE";
        spec.hair = "ONE";
        char looks[SONNY_EQUIP_SLOTS][24];
        for (int i = 0; i < SONNY_EQUIP_SLOTS; i++) {
            const ItemDef *item = item_by_id(who->equip[i]);
            snprintf(looks[i], sizeof(looks[i]), "%s",
                     (item && item->looks) ? item->looks : "");
            spec.looks[i] = looks[i];
        }
        doll_draw(&spec, doll_animation_frame("stand", g->anim_tick / 3, 1),
                  (Vector2){doll->x - 6.0f, doll->y + 10.0f}, 1.3f, 0, WHITE);
    }

    for (int i = 0; i < SONNY_EQUIP_SLOTS; i++) {
        Rectangle r = slot_rect(i);
        draw_slot_art(MENU_INVENTORY, "playerSlot", i);
        draw_item_icon(item_by_id(who->equip[i]), r, WHITE);
        if (hit(r, mouse))
            DrawRectangleLinesEx(r, 1.0f, (Color){235, 200, 90, 255});
    }

    draw_party_row(g, MENU_INVENTORY, mouse);

    for (int32_t i = 0; i < MENU_BAG_SLOTS; i++) {
        Rectangle r = bag_rect(i);
        draw_slot_art(MENU_INVENTORY, "itemSlot", i);
        if (i < c->inventory_count)
            draw_item_icon(item_by_id(c->inventory[i]), r, WHITE);
        if (hit(r, mouse))
            DrawRectangleLinesEx(r, 1.0f, (Color){235, 200, 90, 255});
    }

    /* What the pointer is on, in the game's own words. */
    const ItemDef *shown_item = NULL;
    if (g->hovered_item >= 100) {
        int32_t i = g->hovered_item - 100;
        if (i < c->inventory_count)
            shown_item = item_by_id(c->inventory[i]);
    } else if (g->hovered_item >= 0 && g->hovered_item < SONNY_EQUIP_SLOTS) {
        shown_item = item_by_id(c->player.equip[g->hovered_item]);
    }
    if (shown_item && shown_item->id != 0)
        game_tooltip_item(g, shown_item, 0);
}

/* ---------------------------------------------------------------- shop */

/* The store, which the menu clip carries on its "shop" frame: a picture of
   the place and what its keeper says on the left, the character and what they
   are wearing in the middle with the stock under it, and the bag on the
   right. The frame's own script (sprite 1503, frame 16) is what says where
   the text comes from.

   Which store it is comes from the marker that opened it: every zone's scene
   has one, and the shopId its button sets picks both the stock and the line
   the keeper says. */
#define MENU_SHOP "shop"

/* The slot that buys back what is dropped into it. */
#define SHOP_RECYCLER 1395

/* How many slots the store's grid has, and the bag's on this frame. */
#define SHOP_STOCK_SLOTS SONNY_SHOP_SLOTS

static const TextField *shop_field(const char *name)
{
    return text_field_named(MENU_SCREEN, MENU_SHOP, name, 0);
}

/* Where the store's picture goes, and which of its frames this store is. */
static const ClipPart *shop_picture(void)
{
    for (int32_t i = 0; ; i++) {
        const ClipPart *part = clip_part(MENU_SCREEN, MENU_SHOP, i);
        if (!part)
            return NULL;
        if (part->frames > 0)
            return part;
    }
}

static Rectangle shop_stock_rect(int32_t i)
{
    return named_slot(MENU_SHOP, "dropSlot", i,
                      (Rectangle){305 + (i % 5) * 38, 292 + (i / 5) * 38,
                                  34, 34});
}

static Rectangle shop_bag_rect(int32_t i)
{
    return named_slot(MENU_SHOP, "itemSlot", i,
                      (Rectangle){533 + (i % 6) * 38, 128 + (i / 6) * 38,
                                  34, 34});
}

static Rectangle shop_equip_rect(int32_t i)
{
    return named_slot(MENU_SHOP, "playerSlot", i,
                      (Rectangle){301, 118 + i * 34, 34, 34});
}

void screen_shop_draw(Game *g, Vector2 mouse)
{
    const Campaign *c = &g->campaign;
    const ShopDef *shop = shop_for_button(g->shop_button);

    ClearBackground(BLACK);
    draw_hub_panel(g, mouse);
    draw_clip_parts(MENU_SCREEN, MENU_SHOP, NO_OFFSET, NULL, NULL, WHITE);
    /* The two buttons in the purse strip -- the store's own euro sign and the
       recycler -- are art the frame keeps inside the buttons themselves. */
    for (int32_t i = 0; i < SONNY_BUTTON_COUNT; i++)
        if (strcmp(SONNY_BUTTONS[i].screen, MENU_SHOP) == 0)
            draw_button_art(&SONNY_BUTTONS[i], WHITE);

    /* The picture of the place. Its clip has a frame per store and the
       screen points it at shopId + 1. */
    const ClipPart *picture = shop_picture();
    if (picture && shop) {
        const Texture2D *tex = asset_texture(
            TextFormat("#%d@%d", picture->character, shop->id + 1), 1);
        if (tex) {
            draw_texture_placed(tex, picture->x, picture->y,
                                picture->scale_x, picture->scale_y,
                                picture->origin_x, picture->origin_y, WHITE);
        }
    }

    draw_field_wrapped(shop_field("@895"), NO_OFFSET,
                       shop ? lang_text("SHOP", shop->id) : "");
    draw_field(shop_field("@896"), NO_OFFSET, lang_text("MENU", 6));
    draw_field(shop_field("@1059"), NO_OFFSET, lang_text("MENU", 14));
    draw_field(shop_field("@1052"), NO_OFFSET, TextFormat("%d", c->euros));
    draw_field(shop_field("@1053"), NO_OFFSET, EURO);

    /* The character, stood where the frame stands them, in what they wear. */
    const MenuSlot *doll = menu_slot(MENU_SHOP, "chest");
    if (doll) {
        DollSpec spec;
        memset(&spec, 0, sizeof(spec));
        spec.gender = "M";
        spec.skin = "ONE";
        spec.hair = "ONE";
        char looks[SONNY_EQUIP_SLOTS][24];
        for (int i = 0; i < SONNY_EQUIP_SLOTS; i++) {
            const ItemDef *item = item_by_id(c->player.equip[i]);
            snprintf(looks[i], sizeof(looks[i]), "%s",
                     (item && item->looks) ? item->looks : "");
            spec.looks[i] = looks[i];
        }
        doll_draw(&spec, doll_animation_frame("stand", g->anim_tick / 3, 1),
                  (Vector2){doll->x - 6.0f, doll->y + 10.0f}, 1.3f, 0, WHITE);
    }

    draw_party_row(g, MENU_SHOP, mouse);

    for (int i = 0; i < SONNY_EQUIP_SLOTS; i++) {
        Rectangle r = shop_equip_rect(i);
        draw_slot_art(MENU_SHOP, "playerSlot", i);
        draw_item_icon(item_by_id(c->player.equip[i]), r, WHITE);
        if (hit(r, mouse))
            DrawRectangleLinesEx(r, 1.0f, (Color){235, 200, 90, 255});
    }

    /* The stock. A slot the store leaves empty is hidden outright, which is
       what the frame's script does with a zero. */
    for (int32_t i = 0; i < SHOP_STOCK_SLOTS; i++) {
        int32_t id = shop ? shop->item[i] : 0;
        if (id == 0)
            continue;
        Rectangle r = shop_stock_rect(i);
        draw_slot_art(MENU_SHOP, "dropSlot", i);
        draw_item_icon(item_by_id(id), r, WHITE);
        if (hit(r, mouse))
            DrawRectangleLinesEx(r, 1.0f, (Color){235, 200, 90, 255});
    }

    for (int32_t i = 0; i < MENU_BAG_SLOTS; i++) {
        Rectangle r = shop_bag_rect(i);
        draw_slot_art(MENU_SHOP, "itemSlot", i);
        if (i < c->inventory_count)
            draw_item_icon(item_by_id(c->inventory[i]), r, WHITE);
        if (hit(r, mouse))
            DrawRectangleLinesEx(r, 1.0f, (Color){235, 200, 90, 255});
    }

    /* What the pointer is on, in the game's own words. */
    const ItemDef *shown = NULL;
    if (g->hovered_item >= 100) {
        int32_t i = g->hovered_item - 100;
        if (i < c->inventory_count)
            shown = item_by_id(c->inventory[i]);
    } else if (g->hovered_item >= 0 && shop
               && g->hovered_item < SHOP_STOCK_SLOTS) {
        shown = item_by_id(shop->item[g->hovered_item]);
    }
    if (shown && shown->id != 0)
        /* The store's stock names its price first; a bag slot does not. */
        game_tooltip_item(g, shown,
                          g->hovered_item >= 100 ? 0 : shown->price);
}

void screen_shop_update(Game *g, Vector2 mouse)
{
    Campaign *c = &g->campaign;
    const ShopDef *shop = shop_for_button(g->shop_button);
    Character scratch;
    const Character *who = menu_character(g, &scratch);
    g->hovered_item = -1;

    /* Buying: a stock slot hands the item over for its price, and the store
       never runs out of it. */
    for (int32_t i = 0; shop && i < SHOP_STOCK_SLOTS; i++) {
        const ItemDef *item = item_by_id(shop->item[i]);
        if (!item || item->id == 0)
            continue;
        Rectangle r = shop_stock_rect(i);
        if (!hit(r, mouse))
            continue;
        g->hovered_item = i;
        if (!ui_clicked())
            break;
        if (c->euros < item->price) {
            game_notice(g, "%s", lang_text("MENU", 20));
            break;
        }
        if (c->inventory_count >= (int32_t)(sizeof(c->inventory)
                                            / sizeof(c->inventory[0])))
            break;
        c->euros -= item->price;
        c->inventory[c->inventory_count++] = item->id;
        audio_play("Click2putdown");
        break;
    }

    for (int32_t i = 0; i < MENU_BAG_SLOTS; i++) {
        Rectangle r = shop_bag_rect(i);
        if (!hit(r, mouse))
            continue;
        g->hovered_item = 100 + i;
        if (ui_clicked())
            swap_bag(g, i);
        break;
    }

    for (int i = 0; i < SONNY_EQUIP_SLOTS; i++) {
        Rectangle r = shop_equip_rect(i);
        if (!hit(r, mouse))
            continue;
        g->hovered_item = i;
        if (ui_clicked() && item_fits(who, g->carried_item, i))
            swap_equipped(g, i);
        break;
    }

    /* The recycler: whatever the pointer is carrying goes in for a quarter of
       what it is worth, rounded up. */
    const StageButton *bin = stage_button(MENU_SHOP, SHOP_RECYCLER, 0);
    if (bin && CheckCollisionPointRec(mouse, (Rectangle){bin->x, bin->y,
                                                         bin->width,
                                                         bin->height})) {
        game_tooltip(g, lang_text("MENU", 5), lang_text("MENU", 5));
        if (ui_clicked() && g->carried_item != 0) {
            const ItemDef *item = item_by_id(g->carried_item);
            if (item)
                c->euros += (item->price + 3) / 4;
            g->carried_item = 0;
            audio_play("Click2putdown");
        }
    }

    if (menu_close_pressed(mouse))
        g->screen = SCREEN_ZONE;
}

/* ------------------------------------------------------------- victory */

void screen_victory_update(Game *g, Vector2 mouse)
{
    /* The experience bar fills a thirtieth of what the fight paid each frame.
       If it reaches the end the player levels there and then -- the bar goes
       to full, stops, and the level (and with it a skill point and an
       attribute point) is granted. The original discards the overflow. */
    if (g->win_fill > 0) {
        g->win_fill--;
        g->win_xp += g->win_step;
        if (g->win_xp >= 100.0f) {
            g->win_leveled = 1;
            g->win_fill = 0;
            g->win_xp = 100.0f;
            g->rewards.leveled = campaign_apply_xp(&g->campaign,
                                                   100.0 - g->campaign.player.xp);
        } else if (g->win_fill == 0) {
            g->campaign.player.xp = g->win_xp;
        }
    }

    /* What is under the pointer says what it is, the same way it does in the
       bag: a drop names itself, and the bag's own squares do too. */
    for (int32_t i = 0; i < g->dropped_count; i++)
        if (hit(drop_rect(i), mouse))
            game_tooltip_item(g, item_by_id(g->dropped[i]), 0);
    for (int32_t i = 0; i < g->campaign.inventory_count; i++)
        if (hit(win_bag_rect(i), mouse))
            game_tooltip_item(g, item_by_id(g->campaign.inventory[i]), 0);

    /* Drops are chosen by clicking them, as VICTORY[1] instructs. */
    for (int32_t i = 0; i < g->dropped_count; i++) {
        Rectangle r = drop_rect(i);
        if (!hit(r, mouse) || !ui_clicked())
            continue;
        if (g->taken[i])
            continue;
        Campaign *c = &g->campaign;
        if (c->inventory_count >= (int32_t)(sizeof(c->inventory)
                                            / sizeof(c->inventory[0]))) {
            game_notice(g, "The bag is full.");
            break;
        }
        c->inventory[c->inventory_count++] = g->dropped[i];
        g->taken[i] = 1;
        audio_play("Click3pickup");
        break;
    }

    /* "Proceed!" is one of the frame's own fields, so its hit area is that
       field's box. */
    const TextField *proceed = win_field("@23");
    if (!proceed || !ui_clicked()
        || !CheckCollisionPointRec(mouse, (Rectangle){proceed->x, proceed->y,
                                                      proceed->width,
                                                      proceed->height}))
        return;

    /* Where the game goes from here, as the button's own handler decides it:
       the save is written first when autosave is on, then a zone that has
       just been finished goes out to the map -- by way of a comic at the two
       points the story has one -- and anything else goes back to the hub, or
       to the ability screen when the fight was a level. */
    /* Proceed! is what offers the story's next note, if there is one for
       where the player has got to. */
    game_hub_note(g);
    if (g->options.autosave)
        save_write(&g->campaign, save_slot_path(g->campaign.slot));
    if (g->boss_beaten) {
        g->boss_beaten = 0;
        if (g->campaign.progress_battle == 9)
            game_play_cutscene(g, 1);
        else if (g->campaign.progress_battle == 38)
            game_play_cutscene(g, 2);
        else
            g->screen = SCREEN_MAP;
        return;
    }
    if (g->rewards.leveled) {
        g->stat_points_spent = 0;
        g->screen = SCREEN_TALENTS;
        return;
    }
    g->screen = SCREEN_ZONE;
}

/* ------------------------------------------------------- the victory rows */

/* The three experience rows the win frame stacks, in the order its fields
   come: two for the allies standing in the line, and the last -- the top of
   the panel -- hardwired to the player. A row whose member is not there sets
   itself invisible. */
#define WIN_XP_ROWS    3
#define WIN_PLAYER_ROW 2
/* The bar's full width, which the original scales by exp/100. */
#define WIN_BAR_WIDTH  95.1f
/* The fill's own two frames: how it looks, and how it looks on a level. */
#define WIN_BAR_PLAIN  "#1344@1"
#define WIN_BAR_LEVEL  "#1344@2"

/* Which row a piece of the frame belongs to: the three are stacked, so the
   band its y falls in says which. */
static int32_t win_part_row(float y)
{
    if (y < 230.0f)
        return WIN_PLAYER_ROW;
    return y < 320.0f ? 0 : 1;
}

/* Who is in a row, or -1 for the player's own. An ally row takes whoever
   stands in that place in the line -- and only if the story has actually
   handed them over, which is the same test the fight itself makes. The
   original builds this list (friendlySlotsFFTT) while it places the units, so
   a row is empty exactly when nobody fought in it. */
static int32_t win_row_ally(const Game *g, int32_t row)
{
    if (row == WIN_PLAYER_ROW || row >= SONNY_MAX_ALLIES)
        return -1;
    int32_t member = g->campaign.line[row];
    return campaign_has_friend(&g->campaign, member) ? member : -1;
}

static int win_row_shown(const Game *g, int32_t row)
{
    return row == WIN_PLAYER_ROW || win_row_ally(g, row) > 0;
}

static const char *win_row_name(const Game *g, int32_t row)
{
    if (row == WIN_PLAYER_ROW)
        return g->battle.units[PLAYER_SLOT].name[0]
             ? g->battle.units[PLAYER_SLOT].name : "Sonny";
    int32_t member = win_row_ally(g, row);
    return (member > 0 && member < SONNY_PARTY_COUNT)
         ? SONNY_PARTY[member].name : "";
}

/* An ally levels with the player -- the original scales them off the player's
   own level rather than keeping one of their own. */
static int32_t win_row_level(const Game *g, int32_t row)
{
    (void)row;
    return g->campaign.player.level;
}

/* The frame the portrait clip shows for a row. */
static const char *win_row_portrait(const Game *g, int32_t row)
{
    return row == WIN_PLAYER_ROW ? "mainPlayer" : win_row_name(g, row);
}

/* The win frame's own pieces, with the rows nobody is in left out and the
   player's bar cut to how far the fill has got. */
static void draw_win_parts(Game *g)
{
    for (int i = 0; i < SONNY_CLIP_PART_COUNT; i++) {
        const ClipPart *part = &SONNY_CLIP_PARTS[i];
        if (strcmp(part->screen, MENU_SCREEN) != 0
            || strcmp(part->owner, MENU_WIN) != 0 || part->width <= 0)
            continue;
        int in_row = strcmp(part->name, "@3") == 0
                  || strcmp(part->name, "avIn") == 0
                  || strcmp(part->name, "bar") == 0
                  || strcmp(part->name, "@16") == 0;
        int32_t row = in_row ? win_part_row(part->y) : -1;
        if (in_row && !win_row_shown(g, row))
            continue;

        /* Two pieces of a row are clips the frame points at a frame of: the
           portrait, which it points at whoever the row is for, and the fill,
           which has a second frame it switches to on a level. Pointing both
           at the portrait put a second little face under the row. */
        const char *chosen = NULL;
        if (part->frames) {
            if (!in_row)
                continue;
            if (strcmp(part->name, "avIn") == 0)
                chosen = win_row_portrait(g, row);
            else if (strcmp(part->name, "bar") == 0)
                chosen = (row == WIN_PLAYER_ROW && g->win_leveled)
                       ? WIN_BAR_LEVEL : WIN_BAR_PLAIN;
            else
                continue;
        }
        const char *name = chosen ? chosen
                                  : TextFormat("#%d", part->character);
        const Texture2D *tex = asset_texture(name, 1);
        if (!tex)
            continue;
        /* The fill is the one piece the screen drives: its width is the
           percentage, out of the 95.1 the frame gives it. */
        if (strcmp(part->name, "bar") == 0) {
            float shown = row == WIN_PLAYER_ROW ? g->win_xp
                                                : (float)g->campaign.player.xp;
            float fraction = shown / 100.0f;
            if (fraction < 0)
                fraction = 0;
            if (fraction > 1)
                fraction = 1;
            Rectangle box = placed_texture(tex, part->x, part->y,
                                           part->scale_x, part->scale_y,
                                           part->origin_x, part->origin_y);
            Rectangle src = {0, 0, tex->width * fraction, (float)tex->height};
            box.width = WIN_BAR_WIDTH * fraction * part->scale_x;
            DrawTexturePro(*tex, src, box, (Vector2){0, 0}, 0.0f, WHITE);
            continue;
        }
        draw_texture_placed(tex, part->x, part->y, part->scale_x,
                            part->scale_y, part->origin_x, part->origin_y,
                            WHITE);
    }
}

/* The tally after a fight, laid out from the menu clip's own "win" frame: the
   party's experience down the left, what the fight paid in the middle, and
   the bag on the right with whatever dropped beside it. Every line is one of
   the frame's own text fields, filled from the game's own text arrays. */
void screen_victory_draw(Game *g, Vector2 mouse)
{
    ClearBackground(BLACK);
    /* The hub is behind this too -- the frame only hides the scene
       (KrinScreen._visible = false), not the furniture under it. */
    draw_hub_panel(g, mouse);
    draw_win_parts(g);

    /* Three of these are wider than one line of their own box, and the frame
       lets them wrap. */
    draw_field_wrapped(win_field("@901"), NO_OFFSET, lang_text("VICTORY", 0));
    /* vb2 is set whether or not anything dropped; vb99 is the "nothing did"
       on top of it. */
    draw_field_wrapped(win_field("@899"), NO_OFFSET, lang_text("VICTORY", 1));
    if (g->dropped_count == 0)
        draw_field_wrapped(win_field("@904"), NO_OFFSET, lang_text("MENU", 12));

    draw_field(win_field("@895"), NO_OFFSET, lang_text("VICTORY", 2));
    draw_field(win_field("@896"), NO_OFFSET,
               TextFormat("%s%d", EURO, g->rewards.euros));
    draw_field(win_field("@897"), NO_OFFSET, lang_text("VICTORY", 3));
    /* Math.round, not a cast: 51.5 reads 52 in the original. */
    draw_field(win_field("@898"), NO_OFFSET,
               TextFormat("%d%%", (int32_t)floor(g->rewards.xp_percent + 0.5)));
    draw_field_wrapped(win_field("@900"), NO_OFFSET, lang_text("VICTORY", 4));
    draw_field(win_field("@23"), NO_OFFSET, lang_text("MENU", 13));
    draw_field(win_field("@959"), NO_OFFSET, lang_text("MENU", 15));
    draw_field(win_field("@1059"), NO_OFFSET, lang_text("MENU", 14));
    draw_field(win_field("@1052"), NO_OFFSET,
               TextFormat("%d", g->campaign.euros));
    /* The frame's own content, which the game never writes to. */
    draw_field(win_field("@1053"), NO_OFFSET, EURO);

    /* The experience rows. The frame stacks three clips: two bound to the
       allies standing in the line and one, the top, hardwired to the player.
       A clip whose member is not there sets itself invisible, which is why a
       lone Sonny gets one row and not three. */
    const Character *p = &g->campaign.player;
    for (int32_t row = 0; row < WIN_XP_ROWS; row++) {
        if (!win_row_shown(g, row))
            continue;
        draw_field(text_field_named(MENU_SCREEN, MENU_WIN, "@12", row),
                   NO_OFFSET, win_row_name(g, row));
        draw_field(text_field_named(MENU_SCREEN, MENU_WIN, "@13", row),
                   NO_OFFSET, TextFormat("%s%d", lang_text("MENU", 0),
                                         win_row_level(g, row)));
        /* Only the player's bar moves; an ally's shows what it already had. */
        float shown = row == WIN_PLAYER_ROW ? g->win_xp : (float)p->xp;
        draw_field(text_field_named(MENU_SCREEN, MENU_WIN, "@17", row),
                   NO_OFFSET,
                   TextFormat("%d%%", (int32_t)floor(shown + 0.5)));
    }

    for (int32_t i = 0; i < g->dropped_count; i++) {
        Rectangle r = drop_rect(i);
        draw_slot_art(MENU_WIN, "dropSlot", i);
        draw_item_icon(item_by_id(g->dropped[i]), r,
                       g->taken[i] ? (Color){150, 210, 150, 255} : WHITE);
        if (hit(r, mouse))
            DrawRectangleLinesEx(r, 1.0f, (Color){235, 200, 90, 255});
    }

    /* The bag alongside, so what has been taken can be seen going into it.
       Its squares show whether or not anything is in them; a drop square is
       only there while something is in it. */
    for (int32_t i = 0; i < MENU_BAG_SLOTS; i++) {
        draw_slot_art(MENU_WIN, "itemSlot", i);
        if (i < g->campaign.inventory_count)
            draw_item_icon(item_by_id(g->campaign.inventory[i]),
                           win_bag_rect(i), WHITE);
    }
}
