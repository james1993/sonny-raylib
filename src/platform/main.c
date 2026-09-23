/* Entry point: the window, the stage render target, and the screen loop.
 *
 * Everything is drawn in the original's 800x575 stage coordinates, into a
 * render texture that is then letterboxed into the window -- so layout stays
 * 1:1 with reference screenshots whatever the window size. The texture is
 * kept at the size the stage occupies in the window rather than at 800x575,
 * which is what lets the frame be rasterised as finely as the player's window
 * can show it; see render.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "assets.h"
#include "audio.h"
#include "game.h"
#include "glow.h"
#include "loader.h"
#include "options.h"
#include "render.h"
#include "rlgl.h"

/* SONNY_INFO=1 says what the window, the monitor and the framebuffer
   actually came to. It is the only way to tell from outside whether the
   desktop is scaling one against the other, or has quietly given the window a
   size other than the one it was asked for -- both of which draw the stage at
   a size the window cannot show. Printed once the window has settled, because
   the size a window is asked for is not the size it has. */
static void print_window_info(void)
{
    Vector2 dpi = GetWindowScaleDPI();
    int monitor = GetCurrentMonitor();
    const char *name = GetMonitorName(monitor);
    StageFit look = stage_fit(GetRenderWidth(), GetRenderHeight());
    printf("monitors    %d, on %d (%s)\n", GetMonitorCount(), monitor,
           name ? name : "?");
    printf("monitor     %d x %d pixels\n", GetMonitorWidth(monitor),
           GetMonitorHeight(monitor));
    printf("dpi scale   %.3f x %.3f\n", dpi.x, dpi.y);
    printf("window      %d x %d at %d,%d\n", GetScreenWidth(),
           GetScreenHeight(), (int)GetWindowPosition().x,
           (int)GetWindowPosition().y);
    printf("framebuffer %d x %d\n", GetRenderWidth(), GetRenderHeight());
    printf("stage       %d x %d at %g,%g, %.4f px to the unit\n",
           look.w, look.h, look.x, look.y, look.scale);
    fflush(stdout);
}

/* The window's current frame, written where it was asked for. raylib's own
   TakeScreenshot puts the working directory in front of whatever it is
   given, so an absolute path came out as a path that does not exist. */
/* How long a frame may spend putting decoded art on the card: a tenth of the
   frame, so a head start never costs the frame it was meant to save. */
#define ASSET_PUMP_SECONDS 0.003

static void save_screenshot(const char *path)
{
    Image frame = LoadImageFromScreen();
    if (!ExportImage(frame, path))
        TraceLog(LOG_WARNING, "could not write the capture to %s", path);
    UnloadImage(frame);
}

static void game_start(Game *g, uint64_t seed)
{
    memset(g, 0, sizeof(*g));
    rng_seed(&g->rng, seed);
    rng_refill_krs(&g->rng);

    /* The original opens on its title screen, and nothing is loaded until a
       slot is picked. */
    campaign_new(&g->campaign, 1);
    g->options.sound = 1;
    g->screen = SCREEN_TITLE;
    g->selected = -1;
    g->hub_note = -1;
    g->hovered_unit = -1;
    g->ring_unit = -1;
}

/* The dice the whole game rolls on: every hit, miss, critical and drop, and
   what a fight pays out. The original has no seed of its own -- Flash seeds
   random() from the machine when the movie opens, so no two playthroughs
   roll the same -- and this stood on a constant, which made every playthrough
   identical down to the euro. The clock stands in for Flash's entropy; a seed
   given on the command line or in SONNY_SEED still pins it, which is what the
   playtest and the headless captures want. */
static uint64_t chosen_seed(void)
{
    if (OPTIONS.seeded)
        return OPTIONS.seed;
    return (uint64_t)time(NULL) * 1000003u + (uint64_t)clock();
}

int main(int argc, char **argv)
{
    options_load(argc, argv);
    uint64_t seed = chosen_seed();

    SetTraceLogLevel(LOG_WARNING);
    /* The stage is the original's own 800 by 575 and every coordinate in the
       game is in it, but that is an authored size, not a window size: Flash
       scales the stage to whatever the player is given, letterboxing to keep
       the shape. So the window is resizable and the stage is scaled into it,
       and it opens at the largest whole multiple that leaves room on the
       monitor -- a window of exactly 800 by 575 is uncomfortably small on
       anything modern. */
    /* ALWAYS_RUN keeps the loop going while the window is minimised or in
       the background. Without it raylib stops stepping, and the music stream
       -- which is refilled from the loop, a buffer at a time -- runs dry and
       stutters as soon as the window is not in front. */
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT
                   | FLAG_WINDOW_ALWAYS_RUN);
    InitWindow(STAGE_W, STAGE_H, "Sonny");
    SetWindowMinSize(STAGE_W / 2, STAGE_H / 2);
    /* SONNY_WINDOW=WxH opens at exactly that size, which is how a capture is
       taken at something other than the stage's own. */
    if (OPTIONS.window_w > 0 && OPTIONS.window_h > 0) {
        SetWindowSize(OPTIONS.window_w, OPTIONS.window_h);
    } else {
        /* As much of the monitor as the stage's shape will take.
         *
         * The monitor is reported in its own pixels and the window is asked
         * for in the desktop's coordinates, and on a screen the desktop
         * scales -- which a 4K monitor almost always is -- those are not the
         * same number. Asking for a window the size of a 3840 by 2160 panel
         * on a desktop scaled to 150% asks for half as much again as the
         * screen can show, and the window opens with its right and bottom
         * off the edge. So the monitor is taken back into the desktop's own
         * coordinates first.
         *
         * The rest is headroom: a fifth of the height for a title bar, a
         * top bar and whatever else the desktop keeps there, and a cap at
         * four times the stage, which is past the point where there is
         * anything left to see. */
        Vector2 dpi = GetWindowScaleDPI();
        if (dpi.x <= 0.0f)
            dpi.x = 1.0f;
        if (dpi.y <= 0.0f)
            dpi.y = 1.0f;
        int monitor = GetCurrentMonitor();
        int room_w = (int)(GetMonitorWidth(monitor) / dpi.x);
        int room_h = (int)(GetMonitorHeight(monitor) / dpi.y);
        int w = room_w * 9 / 10;
        int h = room_h * 8 / 10;
        if (w > STAGE_W * 4)
            w = STAGE_W * 4;
        if (h > STAGE_H * 4)
            h = STAGE_H * 4;
        if (w * STAGE_H > h * STAGE_W)
            w = h * STAGE_W / STAGE_H;
        else
            h = w * STAGE_H / STAGE_W;
        if (w > STAGE_W && h > STAGE_H) {
            SetWindowSize(w, h);
            SetWindowPosition((room_w - w) / 2, (room_h - h) / 3);
        }
    }


    /* The right-hand team's containers carry a negative horizontal scale --
       that is how the original faces them the other way -- and a mirrored
       quad winds the opposite way, so the default backface culling throws it
       out. Nothing here is three-dimensional, so the culling only ever costs
       us the mirrored half of the screen. */
    rlDisableBackfaceCulling();
    SetTargetFPS(STAGE_FPS);
    assets_set_root(OPTIONS.assets);
    /* The saves live with the game, wherever it is run from. SONNY_SAVES
       puts them somewhere else -- a test run, say, that should not touch
       the player's own. */
    save_set_dir(OPTIONS.saves);
    assets_check_stamp();
    /* SONNY_SILENT keeps headless runs from opening an audio device. */
    if (!OPTIONS.silent)
        audio_init();
    ui_font_load();
    loader_start();
    assets_trace_loads(OPTIONS.trace);

    Game game;
    game_start(&game, seed);

    /* The stage's texture, at the size the stage is shown at. It is made on
       the first frame and again whenever the window changes shape. */
    RenderTexture2D stage = {0};
    int stage_w = 0, stage_h = 0;
    /* The stage is drawn into a texture and then blitted to the window, and
       the default blend runs the destination's alpha through the same
       formula as its colour: laying anything translucent over the stage eats
       a bite out of the alpha there (0.8 over opaque leaves 0.84), and the
       blit then multiplies the colour by it. Every soft black the game lays
       down -- the disc over an unusable orb, the speech box, a fade -- came
       out darker than it should for that reason alone. Blending the alpha
       with ONE instead keeps an opaque stage opaque. */
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA,
                              RL_ONE, RL_ONE_MINUS_SRC_ALPHA,
                              RL_FUNC_ADD, RL_FUNC_ADD);

    const char *shot = OPTIONS.shot;
    int steps = OPTIONS.steps;
    int every = OPTIONS.every;
    const char *want_screen = OPTIONS.screen;
    int frames = 0;
    int was_focused = 1;

    /* Start the run that far into the story, with whoever the story has
       handed over by then, so a screen that depends on it can be
       photographed without playing there. */
    if (OPTIONS.progress >= 0) {
        game.campaign.progress_battle = OPTIONS.progress;
        campaign_story_joins(&game.campaign);
    }

    if (want_screen) {
        if (strcmp(want_screen, "battle") == 0)
            /* SONNY_BATTLE picks which fight, for looking at something the
               opening one never shows. */
            battle_screen_start(&game, OPTIONS.battle >= 0
                                ? OPTIONS.battle
                                : game.campaign.progress_battle);
        else if (strcmp(want_screen, "talents") == 0) {
            screen_talents_open(&game);
            game.screen = SCREEN_TALENTS;
        }
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

    /* How long each frame's own work took -- everything up to handing the
       frame over, not the wait for the next one -- which is what shows a
       frame that stalled on a file. */
    FILE *frame_log = OPTIONS.frame_log ? fopen(OPTIONS.frame_log, "w") : NULL;

    while (!WindowShouldClose()) {
        double frame_began = GetTime();
        /* Full screen, the way anything else does it. */
        if (IsKeyPressed(KEY_F11)
            || ((IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT))
                && IsKeyPressed(KEY_ENTER)))
            ToggleBorderlessWindowed();

        /* A window that is not in front may have its buffer swaps throttled
           or stopped altogether by the compositor, and the music is refilled
           from this loop -- so waiting on the vertical blank is what makes it
           stutter. Out of focus the loop paces itself on the frame timer
           instead, and takes the blank back when the window returns. */
        int focused = IsWindowFocused();
        if (focused != was_focused) {
            was_focused = focused;
            if (focused)
                SetWindowState(FLAG_VSYNC_HINT);
            else
                ClearWindowState(FLAG_VSYNC_HINT);
        }

        /* The stage's texture is kept at the size the stage is shown at, so
           the frame is rasterised as finely as the window can show it and
           then laid down one texel to one pixel. The window can be resized
           at any moment, so this is checked every frame; when it changes, the
           glow's targets belong to the old size and are thrown away to be
           made again at the new one; the text bakes notice for themselves,
           the next time each size is asked for. */
        /* The framebuffer, not the "screen". raylib records the size a
           window was *asked* for, whatever the window manager then gives it:
           SetWindowSize writes it down and only a resize the desktop actually
           performs corrects it. Everything drawn to the window goes through a
           projection in framebuffer pixels, so a fit worked out from the
           asked-for size draws the stage larger than the window and the right
           and the bottom of it are never seen. */
        StageFit fit = stage_fit(GetRenderWidth(), GetRenderHeight());
        if (fit.w != stage_w || fit.h != stage_h) {
            if (stage.id != 0)
                UnloadRenderTexture(stage);
            stage = LoadRenderTexture(fit.w, fit.h);
            SetTextureFilter(stage.texture, TEXTURE_FILTER_POINT);
            stage_w = fit.w;
            stage_h = fit.h;
            render_set_scale(fit.w / (float)STAGE_W);
            glow_unload();
        }

        Vector2 mouse = OPTIONS.parked ? OPTIONS.mouse : stage_mouse();
        int synthetic = options_click(frames, &mouse);
        ui_set_synthetic_click(synthetic);
        audio_update();
        /* Whatever the decoder has finished goes on the card first, a few
           milliseconds' worth at most, and the screen then says what it
           will want next. */
        assets_pump(ASSET_PUMP_SECONDS);
        game_prefetch(&game);
        if (game.notice_timer > 0)
            game.notice_timer--;

        /* The tooltip only shows while something is under the pointer, so
           it is cleared before the screen has its say. */
        game.tip_title[0] = 0;
        game.tip_body[0] = 0;

        /* Anything needing a render target of its own goes first: ending one
           in raylib returns to the window, not to the target that was
           current, so none of this can happen inside the stage pass. */
        if (game.screen == SCREEN_BATTLE)
            battle_screen_prepare(&game);

        /* Update and draw go through the same call for each screen: several
           of them decide from the same button rectangles they draw. */
        BeginTextureMode(stage);
        /* The target is in window pixels; this puts the stage's own 800 by
           575 back over the whole of it, so every coordinate below is in the
           original's units however finely they are being drawn. */
        render_stage_projection();
        BeginBlendMode(BLEND_CUSTOM_SEPARATE);
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
        EndBlendMode();
        EndTextureMode();

        /* The stage into the window, and black on whichever side is over.
           The texture is already the size it is shown at, so this is one
           texel to one pixel at a whole offset -- nothing is resampled, and
           what was drawn is what appears. */
        if (frame_log)
            fprintf(frame_log, "%d %.3f %d\n", frames,
                    (GetTime() - frame_began) * 1000.0, (int)game.screen);
        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(stage.texture,
                       (Rectangle){0, 0, (float)fit.w, -(float)fit.h},
                       (Rectangle){fit.x, fit.y, (float)fit.w, (float)fit.h},
                       (Vector2){0, 0}, 0.0f, WHITE);
        EndDrawing();

        /* SONNY_SHOT_EVERY=N photographs every Nth frame instead of only the
           last, which is how an animation is looked at: one run, a strip of
           frames, rather than one run per frame. */
        frames++;
        game.frame++;
        if (frames == 3 && OPTIONS.info)
            print_window_info();
        if (shot && every > 0 && frames % every == 0)
            save_screenshot(TextFormat("%s_%04d.png", shot, frames));
        if (shot && frames >= (steps > 0 ? steps : 2)) {
            if (every <= 0)
                save_screenshot(shot);
            break;
        }
    }

    if (frame_log)
        fclose(frame_log);
    audio_shutdown();
    ui_font_unload();
    loader_stop();
    assets_unload_all();
    glow_unload();
    if (stage.id != 0)
        UnloadRenderTexture(stage);
    CloseWindow();
    return 0;
}
