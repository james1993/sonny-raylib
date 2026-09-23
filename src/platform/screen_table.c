/* Every screen the game has, in one place: what it is called, how it is
 * drawn and run, and what opening it straight from SONNY_SCREEN needs.
 *
 * A new screen is an enum value in game.h and a row here. The loop in main.c
 * goes through screen_run and nothing else, and SONNY_SCREEN finds a screen
 * by the name in its row.
 */
#include <string.h>
#include "game.h"
#include "options.h"

/* The fight's own signatures, brought into line with everyone else's. It
   runs before it draws -- what it draws is where the fight has got to this
   frame -- where the menus draw first and then answer the pointer, because
   several of them decide from the same boxes they have just drawn. */
static void battle_update(Game *g, Vector2 mouse)
{
    battle_screen_update(g, mouse, OPTIONS.shot != NULL);
}

static void battle_draw(Game *g, Vector2 mouse)
{
    (void)mouse;
    battle_screen_draw(g);
}

/* Opening a screen from outside, where the game would have set something up
   on the way there. */
static void open_battle(Game *g)
{
    /* SONNY_BATTLE picks which fight, for looking at something the opening
       one never shows. */
    battle_screen_start(g, OPTIONS.battle >= 0 ? OPTIONS.battle
                                               : g->campaign.progress_battle);
}

static void open_talents(Game *g)
{
    screen_talents_open(g);
}

static void open_shop(Game *g)
{
    /* The store the first zone's marker opens. */
    g->shop_button = 1212;
}

static void open_intro(Game *g)
{
    game_play_cutscene(g, 0);
}

static const ScreenDef SCREENS[SCREEN_COUNT] = {
    [SCREEN_TITLE]     = {"title", screen_title_draw, screen_title_update,
                          NULL, 0},
    [SCREEN_START]     = {"start", screen_start_draw, screen_start_update,
                          NULL, 0},
    [SCREEN_SLOTS]     = {"slots", screen_slots_draw, screen_slots_update,
                          NULL, 0},
    [SCREEN_CLASS]     = {"class", screen_class_draw, screen_class_update,
                          NULL, 0},
    [SCREEN_MANUAL]    = {"manual", screen_manual_draw, screen_manual_update,
                          NULL, 0},
    [SCREEN_SETTINGS]  = {"settings", screen_settings_draw,
                          screen_settings_update, NULL, 0},
    [SCREEN_CUTSCENE]  = {"intro", screen_cutscene_draw,
                          screen_cutscene_update, open_intro, 0},
    [SCREEN_ENDING]    = {"ending", screen_ending_draw, screen_ending_update,
                          NULL, 0},
    [SCREEN_LOST]      = {"lost", screen_lost_draw, screen_lost_update,
                          NULL, 0},
    [SCREEN_GAMEOVER]  = {"gameover", screen_gameover_draw,
                          screen_gameover_update, NULL, 0},
    [SCREEN_ZONE]      = {"hub", screen_zone_draw, screen_zone_update,
                          NULL, 0},
    [SCREEN_BATTLE]    = {"battle", battle_draw, battle_update, open_battle,
                          1},
    [SCREEN_VICTORY]   = {"victory", screen_victory_draw,
                          screen_victory_update, NULL, 0},
    [SCREEN_TALENTS]   = {"talents", screen_talents_draw,
                          screen_talents_update, open_talents, 0},
    [SCREEN_INVENTORY] = {"inventory", screen_inventory_draw,
                          screen_inventory_update, NULL, 0},
    [SCREEN_SHOP]      = {"shop", screen_shop_draw, screen_shop_update,
                          open_shop, 0},
    [SCREEN_MAP]       = {"map", screen_map_draw, screen_map_update,
                          NULL, 0},
};

const ScreenDef *screen_def(Screen screen)
{
    if (screen < 0 || screen >= SCREEN_COUNT || !SCREENS[screen].draw)
        return &SCREENS[SCREEN_ZONE];
    return &SCREENS[screen];
}

void screen_run(Game *g, Vector2 mouse)
{
    const ScreenDef *s = screen_def(g->screen);
    if (s->update_first) {
        s->update(g, mouse);
        s->draw(g, mouse);
    } else {
        s->draw(g, mouse);
        s->update(g, mouse);
    }
}

int screen_open_by_name(Game *g, const char *name)
{
    for (int i = 0; i < SCREEN_COUNT; i++) {
        if (!SCREENS[i].name || strcmp(SCREENS[i].name, name) != 0)
            continue;
        g->screen = (Screen)i;
        /* An opener may send the game somewhere else -- the intro is a comic,
           a fight is started -- so it has the last word. */
        if (SCREENS[i].open)
            SCREENS[i].open(g);
        return 1;
    }
    return 0;
}
