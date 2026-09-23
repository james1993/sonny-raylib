/* What to decode ahead, by where the player is.
 *
 * The original is one SWF, loaded whole before the title screen, so nothing
 * it shows ever waits on a file. Here every picture is read the first time it
 * is drawn, and the big ones -- a zone's backdrop, a comic's panel -- take the
 * best part of a frame to inflate. So each screen names what is likely to come
 * next and the decoder works through it while the player is looking at
 * something else. It is only ever a head start: see loader.h.
 *
 * The lists are kept short on purpose. The texture cache holds 192 MB, and a
 * prefetch that pushes out what is on the screen now costs more than it saves.
 */
#include <stdio.h>
#include <string.h>
#include "assets.h"
#include "game.h"

/* How far ahead of the playhead a comic is read. A panel is up to ten
   megabytes on the card, so this is a second of comic, not the whole of it. */
#define COMIC_LOOKAHEAD 8

/* The frames the hub and the battlefield are laid out on. */
#define HUB_SCREEN    "Navigation"
#define BATTLE_SCREEN "KRINBATTLESCENE"

/* Every picture a root frame places, as draw_screen_chrome would ask. */
static void warm_screen(const char *screen)
{
    int32_t first, n = stage_chrome_rows(screen, &first);
    for (int32_t i = first; i < first + n; i++)
        if (SONNY_STAGE_CHROME[i].width > 0)
            assets_prefetch(TextFormat("#%d", SONNY_STAGE_CHROME[i].character),
                            1);
}

/* The zone's own scene, which the hub is drawn over. */
static void warm_zone(const Game *g)
{
    const ZoneDef *zone = campaign_zone(&g->campaign);
    if (zone && zone->zone < SONNY_ZONE_LABEL_COUNT)
        assets_prefetch(SONNY_ZONE_SCENES[zone->zone], 1);
}

/* The two backdrops of the fight the story marker would start. */
static void warm_next_fight(const Game *g)
{
    const BattleDef *def = battle_def_by_id(
        campaign_story_battle(&g->campaign, NULL));
    if (!def)
        return;
    if (def->zone_bg[0])
        assets_prefetch(def->zone_bg, 1);
    if (def->sky_bg[0])
        assets_prefetch(def->sky_bg, 1);
}

/* Every effect and projectile anyone in this fight could play. */
static void warm_fight(const Game *g)
{
    const Battle *b = &g->battle;
    for (int32_t slot = 1; slot < SONNY_SLOTS; slot++) {
        if (!b->units[slot].active)
            continue;
        const Brain *br = &b->brains[slot];
        for (int32_t k = 0; k < br->movesA_count + br->movesD_count; k++) {
            int32_t id = k < br->movesA_count ? br->movesA[k]
                                              : br->movesD[k - br->movesA_count];
            const AbilityDef *a = ability_by_id(id);
            if (!a)
                continue;
            if (a->model && a->model[0])
                assets_prefetch_all(a->model);
            if (a->projectile && a->projectile[0])
                assets_prefetch_all(a->projectile);
        }
    }
    for (int32_t i = 0; i < ABILITY_SLOTS; i++) {
        const AbilityDef *a = ability_by_id(g->bv.ability_ids[i]);
        if (!a)
            continue;
        if (a->model && a->model[0])
            assets_prefetch_all(a->model);
        if (a->projectile && a->projectile[0])
            assets_prefetch_all(a->projectile);
    }
}

static void warm_comic(const Game *g)
{
    const char *art = TextFormat("#%d", cutscene_clip(g->cutscene));
    char name[16];
    snprintf(name, sizeof(name), "%s", art);
    for (int32_t f = 1; f <= COMIC_LOOKAHEAD; f++)
        assets_prefetch(name, g->cutscene_frame + f);
}

void game_prefetch(const Game *g)
{
    switch (g->screen) {
    case SCREEN_TITLE:
    case SCREEN_START:
    case SCREEN_SLOTS:
    case SCREEN_CLASS:
    case SCREEN_MANUAL:
        /* The menus sit idle until a slot is picked; the hub is where every
           way out of them goes. */
        warm_screen(HUB_SCREEN);
        warm_zone(g);
        break;
    case SCREEN_ZONE:
        warm_screen(BATTLE_SCREEN);
        warm_next_fight(g);
        break;
    case SCREEN_VICTORY:
    case SCREEN_MAP:
    case SCREEN_LOST:
        warm_screen(HUB_SCREEN);
        warm_zone(g);
        break;
    case SCREEN_BATTLE:
        warm_fight(g);
        break;
    case SCREEN_CUTSCENE:
        warm_comic(g);
        break;
    default:
        break;
    }
}
