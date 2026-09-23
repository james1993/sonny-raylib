/* The world map. */
#include "menu.h"

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
        render_scissor(box);
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
void draw_slot_art(const char *menu, const char *prefix, int i);
