/* The zone hub: the scene with its markers, the bar along the bottom that
   every menu is laid over, and the story's notes. */
#include "menu.h"

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
    return screen_button_pressed(HUB_SCREEN, character, mouse);
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
    int32_t row0, rows = button_rows(label, &row0);
    for (int32_t i = row0; i < row0 + rows; i++) {
        const StageButton *b = &SONNY_BUTTONS[i];
        if (strcmp(b->screen, label) != 0 || !zone_button(b->character))
            continue;
        if (index-- == 0)
            return b;
    }
    return NULL;
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

/* What each of the bar's buttons calls itself, as SYSTEM keeps them: a name
   and the line under it, in pairs. The bar is under every menu, so these
   answer from every screen the bar shows on. */
static void hub_tooltips(Game *g, Vector2 mouse)
{
    static const struct { int32_t character; int32_t say; } NAMED[] = {
        {HUB_BUTTON_INVENTORY, 17}, {HUB_BUTTON_SKILLS, 19},
        {HUB_BUTTON_SAVE, 21}, {HUB_BUTTON_OPTIONS, 23},
        {HUB_BUTTON_RESPEC, 32}, {HUB_BUTTON_MAP, 27},
    };
    for (size_t i = 0; i < sizeof(NAMED) / sizeof(NAMED[0]); i++) {
        const StageButton *b = stage_button(HUB_SCREEN, NAMED[i].character, 0);
        if (b && CheckCollisionPointRec(mouse, BOX_OF(b)))
            game_tooltip(g, lang_text("SYSTEM", NAMED[i].say),
                         lang_text("SYSTEM", NAMED[i].say + 1));
    }
}

/* The markers on the scene name themselves the same way. Unlike the bar,
   these belong to the scene: opening a menu does KrinScreen._visible = false,
   which takes the whole scene and every marker on it away, so they answer on
   the hub and nowhere else. */
static void zone_marker_tooltips(Game *g, Vector2 mouse)
{
    for (int32_t i = 0; ; i++) {
        const StageButton *b = zone_marker_button(&g->campaign, i);
        if (!b)
            break;
        if (!CheckCollisionPointRec(mouse, BOX_OF(b)))
            continue;
        const ZoneButton *marker = zone_button(b->character);
        if (marker->kind == MARKER_SCENERY)
            continue;           /* the original gives scenery no tooltip */
        int32_t say = marker->kind == MARKER_SHOP ? 15
                    : marker->kind == MARKER_TRAINING ? 29 : 13;
        game_tooltip(g, lang_text("SYSTEM", say),
                     lang_text("SYSTEM", say + 1));
    }
}

/* The bar's own buttons, wherever the bar is.
 *
 * Every menu the hub opens is a clip laid over the Navigation frame, so the
 * row of buttons along the bottom is still there and still live underneath
 * it -- the inventory does not stop the map button being the map button. The
 * port had them answering on the hub alone, which left them drawn but dead on
 * every screen that opens over it. */
void hud_buttons(Game *g, Vector2 mouse)
{
    /* PauseForScreen: a note holds the hub still, and nothing on it answers
       until it is clicked away. */
    if (game_hub_note_up(g))
        return;
    hub_tooltips(g, mouse);
    if (hub_pressed(HUB_BUTTON_INVENTORY, mouse)) {
        audio_play("Click3pickup");
        g->screen = SCREEN_INVENTORY;
    } else if (hub_pressed(HUB_BUTTON_SKILLS, mouse)) {
        audio_play("Click3pickup");
        /* Opening the screen puts the menu clip back on its first frame, so
           the swatches beside the attributes start coloured again. */
        g->stat_points_spent = 0;
        screen_talents_open(g);
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
    hud_buttons(g, mouse);
    zone_marker_tooltips(g, mouse);
    /* The markers on the scene. Which one was pressed decides what happens,
       and for a store it is also what says which store. */
    for (int32_t i = 0; ui_clicked(); i++) {
        const StageButton *b = zone_marker_button(&g->campaign, i);
        if (!b)
            break;
        if (!CheckCollisionPointRec(mouse, BOX_OF(b)))
            continue;
        const ZoneButton *marker = zone_button(b->character);
        audio_play("Click3pickup");
        switch (marker->kind) {
        case MARKER_SHOP:
            g->shop_button = b->character;
            g->screen = SCREEN_SHOP;
            return;
        case MARKER_SCENERY:
            /* Scenery says a line about itself over the same panel the
               story's notes use, and flags itself as found:

                   _root.krinNavHideUI(n);
                   _root.updateBgElementClicked(e);  */
            g->hub_note = marker->say;
            g->hub_note_nav = 1;
            if (marker->element >= 0 && marker->element < SONNY_SCENERY)
                g->campaign.stats.scenery[marker->element] = 1;
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
            /* The story hands someone over here, in the marker's own
               handler, as the fight is started -- not on the way back from
               the one before it:

                   on(press) {
                      if(Krin.progressLevelOn == 15)
                         Krin.friendArray = [0,1,-1,-1,-1,-1];

               Doing it when progress moved put Veradux on the victory screen
               and in the bag a whole fight before he turns up. */
            campaign_story_joins(&g->campaign);
            int boss = 0;
            int32_t pick = campaign_story_battle(&g->campaign, &boss);
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

}



/* ------------------------------------------------------ the hub's welcome */

/* The story stops the player on the hub at seven points to explain something.
   Krin.progressSpeech is keyed by how far the story has got, and each entry
   is a title and a body out of NAVTITLE2 and NAVTEXT2 (SONNY_HUB_NOTES); the
   fade behind it holds the hub still until it is clicked away. Proceed! on
   the victory screen is what offers one, and winning a fight re-arms the
   offer. */
void game_hub_note(Game *g)
{
    if (g->hub_note_done)
        return;
    g->hub_note_done = 1;
    g->hub_note = -1;
    g->hub_note_nav = 0;
    for (int i = 0; i < SONNY_HUB_NOTE_COUNT; i++)
        if (SONNY_HUB_NOTES[i].at == g->campaign.progress_battle)
            g->hub_note = SONNY_HUB_NOTES[i].say;
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
    /* The same two fields either way. The story's notes come out of
       NAVTITLE2/NAVTEXT2 and a piece of scenery out of NAVTITLE/NAVTEXT,
       which is the only difference between krinNavTutSpeech and
       krinNavHideUI. */
    draw_field_wrapped(chrome_field(HUB_SCREEN, "@1241"), NO_OFFSET,
                       lang_text(g->hub_note_nav ? "NAVTITLE" : "NAVTITLE2",
                                 g->hub_note));
    draw_field_wrapped(chrome_field(HUB_SCREEN, "@1240"), NO_OFFSET,
                       lang_text(g->hub_note_nav ? "NAVTEXT" : "NAVTEXT2",
                                 g->hub_note));
    /* The clip's own frame sets this from the text table; what it was
       authored with is placeholder. */
    draw_field(text_field_var(HUB_SCREEN, "texter"), NO_OFFSET,
               lang_text("SYSTEM", 8));
    const StageButton *close = stage_button(HUB_SCREEN, HUB_NOTE_CLOSE, 0);
    if (close)
        draw_button_state(close, CheckCollisionPointRec(mouse,
                              BOX_OF(close)), WHITE);
}

/* The furniture along the bottom of the hub: the panels, the row of buttons
   and their icons, the bar that says how far through the zone the player is,
   and the three lines beside it. Every menu the hub opens is a clip laid over
   this, so it stays on screen behind all of them -- which is why the original
   never shows a bare menu with nothing under it. */
void draw_hub_panel(Game *g, Vector2 mouse)
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
        Art track;
        if (asset_art(BAR_TRACK, 1, &track))
            draw_art_placed(&track, bar->x, bar->y, bar->scale_x,
                            bar->scale_y, track.offset.x, track.offset.y,
                            WHITE);
        /* The fill is lit: its placement carries a glow, so what is drawn is
           the fill with the halo already under it. Cutting it to the fraction
           cuts the halo with it, which is what scaling the clip does. */
        Art fill;
        if (asset_art(BAR_FILL, 1, &fill) && total > 0) {
            float fraction = done / total;
            Rectangle box = placed_art(&fill,
                                       bar->x + BAR_FILL_X * bar->scale_x,
                                       bar->y, bar->scale_x, bar->scale_y,
                                       fill.offset.x, fill.offset.y);
            /* The glow's margin is outside the art, so only the art's own
               width takes the fraction. In units, because that is what the
               frame's own width is in; the source rectangle then goes back
               through the art's own scale to reach its pixels. */
            float edge = (fill.size.x - BAR_FILL_WIDTH) / 2.0f;
            float shown = edge * 2 + BAR_FILL_WIDTH * fraction;
            box.width *= shown / fill.size.x;
            Rectangle src = {0, 0, fill.source.width * shown / fill.size.x,
                             fill.source.height};
            DrawTexturePro(*fill.texture, src, box, (Vector2){0, 0}, 0.0f,
                           WHITE);
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
            /* Flash steps a clip once per stage frame, so the markers are
               clocked off frames rather than off the wall clock -- which
               also keeps two captures of the same frame the same. */
            frame = 1 + (int32_t)(g->frame * SONNY_MARKER_FPS / STAGE_FPS)
                        % SONNY_MARKER_FRAMES;
        for (int32_t i = 0; ; i++) {
            const ZoneMarker *m = zone_marker(label, i);
            if (!m)
                break;
            /* Two of these are squashed by their placement, so the two scales
               are kept apart. */
            Art art;
            if (!asset_art(m->style, frame, &art))
                continue;
            draw_art_placed(&art, scene.x + m->x, scene.y + m->y,
                            m->scale_x, m->scale_y,
                            art.offset.x, art.offset.y, WHITE);
        }
    }

    draw_hub_panel(g, mouse);
    draw_hub_note(g, mouse);
}
