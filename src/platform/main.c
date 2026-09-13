/* Entry point: the window, the fixed-stage render target, and the screen loop.
 *
 * Everything is drawn into a render texture at the original's 800x575 stage
 * size and scaled with letterboxing, so layout stays 1:1 with reference
 * screenshots whatever the window size.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "assets.h"
#include "audio.h"
#include "game.h"
#include "rlgl.h"

static void game_start(Game *g, uint64_t seed)
{
    memset(g, 0, sizeof(*g));
    rng_seed(&g->rng, seed);
    rng_refill_krs(&g->rng);

    /* The original opens on its title screen, and nothing is loaded until a
       slot is picked. */
    campaign_new(&g->campaign, 1);
    g->options.sound = 1;
    g->options.graphics = 1;
    g->options.quality = 1;
    g->options.autosave = 1;
    g->screen = SCREEN_TITLE;
    g->selected = -1;
    g->hovered_unit = -1;
    g->ring_unit = -1;
}

int main(int argc, char **argv)
{
    uint64_t seed = (argc > 1) ? strtoull(argv[1], NULL, 10) : 20260912;

    SetTraceLogLevel(LOG_WARNING);
    InitWindow(STAGE_W, STAGE_H, "Sonny");
    /* The right-hand team's containers carry a negative horizontal scale --
       that is how the original faces them the other way -- and a mirrored
       quad winds the opposite way, so the default backface culling throws it
       out. Nothing here is three-dimensional, so the culling only ever costs
       us the mirrored half of the screen. */
    rlDisableBackfaceCulling();
    SetTargetFPS(STAGE_FPS);
    assets_set_root(getenv("SONNY_ASSETS") ? getenv("SONNY_ASSETS") : ".");
    /* SONNY_SILENT keeps headless runs from opening an audio device. */
    if (!getenv("SONNY_SILENT"))
        audio_init();
    ui_font_load();

    Game game;
    game_start(&game, seed);

    RenderTexture2D stage = LoadRenderTexture(STAGE_W, STAGE_H);
    SetTextureFilter(stage.texture, TEXTURE_FILTER_POINT);

    /* Headless capture: SONNY_SHOT=path, SONNY_STEPS=frames, and
       SONNY_SCREEN picks which screen to open first. */
    const char *shot = getenv("SONNY_SHOT");
    int steps = getenv("SONNY_STEPS") ? atoi(getenv("SONNY_STEPS")) : 0;
    const char *want_screen = getenv("SONNY_SCREEN");
    int frames = 0;

    if (want_screen) {
        if (strcmp(want_screen, "battle") == 0)
            battle_screen_start(&game, game.campaign.progress_battle);
        else if (strcmp(want_screen, "talents") == 0)
            game.screen = SCREEN_TALENTS;
        else if (strcmp(want_screen, "inventory") == 0)
            game.screen = SCREEN_INVENTORY;
        else if (strcmp(want_screen, "shop") == 0) {
            /* The store the first zone's marker opens. */
            game.shop_button = 1212;
            game.screen = SCREEN_SHOP;
        }
        else if (strcmp(want_screen, "map") == 0)
            game.screen = SCREEN_MAP;
        else if (strcmp(want_screen, "victory") == 0)
            game.screen = SCREEN_VICTORY;
        else if (strcmp(want_screen, "title") == 0)
            game.screen = SCREEN_TITLE;
        else if (strcmp(want_screen, "start") == 0)
            game.screen = SCREEN_START;
        else if (strcmp(want_screen, "slots") == 0)
            game.screen = SCREEN_SLOTS;
        else if (strcmp(want_screen, "class") == 0)
            game.screen = SCREEN_CLASS;
        else if (strcmp(want_screen, "options") == 0)
            game.screen = SCREEN_OPTIONS;
        else if (strcmp(want_screen, "manual") == 0)
            game.screen = SCREEN_MANUAL;
        else if (strcmp(want_screen, "ending") == 0)
            game.screen = SCREEN_ENDING;
        else if (strcmp(want_screen, "intro") == 0)
            game_play_cutscene(&game, 0);
        else if (strcmp(want_screen, "settings") == 0)
            game.screen = SCREEN_SETTINGS;
        else if (strcmp(want_screen, "gameover") == 0)
            game.screen = SCREEN_GAMEOVER;
        else if (strcmp(want_screen, "hub") == 0)
            game.screen = SCREEN_ZONE;
    }

    while (!WindowShouldClose()) {
        Vector2 mouse = stage_mouse();
        audio_update();
        if (game.notice_timer > 0)
            game.notice_timer--;

        /* The tooltip only shows while something is under the pointer, so
           it is cleared before the screen has its say. */
        game.tip_title[0] = 0;
        game.tip_body[0] = 0;

        /* Update and draw go through the same call for each screen: several
           of them decide from the same button rectangles they draw. */
        BeginTextureMode(stage);
        switch (game.screen) {
        case SCREEN_BATTLE:
            battle_screen_update(&game, mouse, shot != NULL);
            battle_screen_draw(&game);
            break;
        case SCREEN_VICTORY:
            screen_victory_draw(&game, mouse);
            screen_victory_update(&game, mouse);
            break;
        case SCREEN_TALENTS:
            screen_talents_draw(&game, mouse);
            screen_talents_update(&game, mouse);
            break;
        case SCREEN_INVENTORY:
            screen_inventory_draw(&game, mouse);
            screen_inventory_update(&game, mouse);
            break;
        case SCREEN_SHOP:
            screen_shop_draw(&game, mouse);
            screen_shop_update(&game, mouse);
            break;
        case SCREEN_MAP:
            screen_map_draw(&game, mouse);
            screen_map_update(&game, mouse);
            break;
        case SCREEN_TITLE:
            screen_title_draw(&game, mouse);
            screen_title_update(&game, mouse);
            break;
        case SCREEN_START:
            screen_start_draw(&game, mouse);
            screen_start_update(&game, mouse);
            break;
        case SCREEN_SLOTS:
            screen_slots_draw(&game, mouse);
            screen_slots_update(&game, mouse);
            break;
        case SCREEN_CLASS:
            screen_class_draw(&game, mouse);
            screen_class_update(&game, mouse);
            break;
        case SCREEN_OPTIONS:
            screen_options_draw(&game, mouse);
            screen_options_update(&game, mouse);
            break;
        case SCREEN_MANUAL:
            screen_manual_draw(&game, mouse);
            screen_manual_update(&game, mouse);
            break;
        case SCREEN_ENDING:
            screen_ending_draw(&game, mouse);
            screen_ending_update(&game, mouse);
            break;
        case SCREEN_CUTSCENE:
            screen_cutscene_draw(&game, mouse);
            screen_cutscene_update(&game, mouse);
            break;
        case SCREEN_SETTINGS:
            screen_settings_draw(&game, mouse);
            screen_settings_update(&game, mouse);
            break;
        case SCREEN_LOST:
            screen_lost_draw(&game, mouse);
            screen_lost_update(&game, mouse);
            break;
        case SCREEN_GAMEOVER:
            screen_gameover_draw(&game, mouse);
            screen_gameover_update(&game, mouse);
            break;
        default:
            screen_zone_draw(&game, mouse);
            screen_zone_update(&game, mouse);
            break;
        }
        /* A message the game wants to show. In a fight the original runs
           these across the top of the battlefield, in KrinCombatText. */
        game_draw_tooltip(&game, mouse);
        if (game.notice_timer > 0)
            game_draw_notice(&game);
        EndTextureMode();

        float scale = (float)GetScreenHeight() / STAGE_H;
        float sw = STAGE_W * scale;
        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(stage.texture,
                       (Rectangle){0, 0, (float)STAGE_W, -(float)STAGE_H},
                       (Rectangle){(GetScreenWidth() - sw) / 2.0f, 0, sw,
                                   (float)GetScreenHeight()},
                       (Vector2){0, 0}, 0.0f, WHITE);
        EndDrawing();

        if (shot && ++frames >= (steps > 0 ? steps : 2)) {
            TakeScreenshot(shot);
            break;
        }
    }

    audio_shutdown();
    ui_font_unload();
    assets_unload_all();
    UnloadRenderTexture(stage);
    CloseWindow();
    return 0;
}
