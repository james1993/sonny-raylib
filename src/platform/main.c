/* raylib front end.
 *
 * The original is a Flash game with a fixed stage size, so everything is drawn
 * into a render texture at that logical resolution and then scaled with
 * integer-friendly letterboxing. That keeps layout work 1:1 with reference
 * screenshots from the original -- the whole point of a replica.
 *
 * STAGE_W/STAGE_H/STAGE_FPS below are placeholders until they are read out of
 * the game's SWF header (which stores exactly these three values).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "raylib.h"
#include "../core/combat.h"

#define STAGE_W   700
#define STAGE_H   500
#define STAGE_FPS 30

typedef struct {
    Combat combat;
    int    selected_ability;
    int    hovered_unit;
} Game;

static int32_t roster_add(Combat *c, const char *name, Side side, int32_t hp,
                          int32_t spd, int32_t def, int32_t ai)
{
    int32_t i = c->unit_count++;
    Combatant *u = &c->units[i];
    memset(u, 0, sizeof(*u));
    snprintf(u->name, SONNY_NAME_LEN, "%s", name);
    u->side = side;
    u->base.hp_max = hp;
    u->base.speed = spd;
    u->base.defense = def;
    u->base.focus_max = 100;
    u->ai_script_id = ai;
    u->ability_count = 4;
    return i;
}

/* Placeholder encounter so the shell is runnable before real data exists. */
static void game_init(Game *g)
{
    memset(g, 0, sizeof(*g));
    roster_add(&g->combat, "Sonny", SIDE_PLAYER, 120, 12, 4, -1);
    roster_add(&g->combat, "Ally", SIDE_PLAYER, 100, 10, 3, -1);
    roster_add(&g->combat, "Enemy A", SIDE_ENEMY, 90, 11, 2, 0);
    roster_add(&g->combat, "Enemy B", SIDE_ENEMY, 90, 8, 2, 0);
    combat_begin(&g->combat, 20260912);
    g->selected_ability = -1;
    g->hovered_unit = -1;
}

static Rectangle unit_rect(const Combat *c, int32_t i)
{
    /* Players on the left column, enemies on the right, as in the original. */
    int slot = 0;
    for (int32_t j = 0; j < i; j++)
        if (c->units[j].side == c->units[i].side)
            slot++;
    float x = (c->units[i].side == SIDE_PLAYER) ? 60.0f : STAGE_W - 60.0f - 110.0f;
    float y = 90.0f + slot * 105.0f;
    return (Rectangle){x, y, 110.0f, 90.0f};
}

static void draw_bar(Rectangle r, int32_t cur, int32_t max, Color fill, const char *label)
{
    DrawRectangleRec(r, (Color){20, 20, 24, 255});
    if (max > 0 && cur > 0) {
        Rectangle f = r;
        f.width = r.width * ((float)cur / (float)max);
        DrawRectangleRec(f, fill);
    }
    DrawRectangleLinesEx(r, 1.0f, (Color){90, 90, 100, 255});
    DrawText(TextFormat("%s %d/%d", label, cur, max), (int)r.x + 3, (int)r.y + 1, 10, RAYWHITE);
}

static void draw_combat(const Game *g)
{
    const Combat *c = &g->combat;

    ClearBackground((Color){24, 26, 32, 255});
    DrawText("SONNY -- reimplementation shell", 12, 10, 20, (Color){200, 205, 215, 255});
    DrawText(TextFormat("Round %d   phase %d", c->round, (int)c->phase), 12, 34, 10,
             (Color){150, 155, 165, 255});

    int32_t active = combat_current_unit(c);

    for (int32_t i = 0; i < c->unit_count; i++) {
        const Combatant *u = &c->units[i];
        Rectangle r = unit_rect(c, i);
        Color frame = (i == active) ? (Color){235, 200, 90, 255} : (Color){70, 74, 86, 255};

        DrawRectangleRec(r, (Color){40, 44, 54, u->alive ? 255 : 120});
        DrawRectangleLinesEx(r, (i == active) ? 2.0f : 1.0f, frame);
        DrawText(u->name, (int)r.x + 5, (int)r.y + 5, 10, u->alive ? RAYWHITE : GRAY);

        draw_bar((Rectangle){r.x + 5, r.y + 22, r.width - 10, 12}, u->hp, u->base.hp_max,
                 (Color){170, 55, 60, 255}, "HP");
        draw_bar((Rectangle){r.x + 5, r.y + 38, r.width - 10, 12}, u->focus, u->base.focus_max,
                 (Color){60, 105, 180, 255}, "FP");

        for (int32_t b = 0; b < u->buff_count; b++)
            DrawRectangle((int)r.x + 5 + b * 14, (int)r.y + 56, 12, 12,
                          (Color){120, 170, 110, 255});
    }

    /* Ability bar: four slots plus the turn-order strip above it. */
    for (int32_t i = 0; i < c->unit_count; i++) {
        int32_t u = c->turn_order[i];
        DrawRectangle(12 + i * 26, STAGE_H - 118, 22, 14,
                      c->units[u].side == SIDE_PLAYER ? (Color){60, 105, 180, 255}
                                                      : (Color){150, 60, 60, 255});
    }
    for (int i = 0; i < 8; i++) {
        Rectangle slot = {12.0f + i * 52.0f, STAGE_H - 96.0f, 46.0f, 46.0f};
        DrawRectangleRec(slot, (Color){44, 48, 58, 255});
        DrawRectangleLinesEx(slot, g->selected_ability == i ? 2.0f : 1.0f,
                             g->selected_ability == i ? (Color){235, 200, 90, 255}
                                                      : (Color){80, 84, 96, 255});
    }
    DrawText("ability bar (icons + tooltips pending asset extraction)", 12, STAGE_H - 42, 10,
             (Color){130, 135, 145, 255});
}

int main(void)
{
    Game game;
    game_init(&game);

    SetTraceLogLevel(LOG_WARNING);
    InitWindow(STAGE_W * 2, STAGE_H * 2, "Sonny");
    SetTargetFPS(STAGE_FPS);
    RenderTexture2D stage = LoadRenderTexture(STAGE_W, STAGE_H);
    SetTextureFilter(stage.texture, TEXTURE_FILTER_POINT);

    /* One-shot render for headless verification: SONNY_SHOT=path. */
    const char *shot = getenv("SONNY_SHOT");
    int frames = 0;

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_SPACE))
            combat_advance_turn(&game.combat);
        for (int k = 0; k < 8; k++)
            if (IsKeyPressed(KEY_ONE + k))
                game.selected_ability = k;

        BeginTextureMode(stage);
        draw_combat(&game);
        EndTextureMode();

        float scale = (float)GetScreenHeight() / STAGE_H;
        float sw = STAGE_W * scale;
        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(stage.texture, (Rectangle){0, 0, (float)STAGE_W, -(float)STAGE_H},
                       (Rectangle){(GetScreenWidth() - sw) / 2.0f, 0, sw,
                                   (float)GetScreenHeight()},
                       (Vector2){0, 0}, 0.0f, WHITE);
        EndDrawing();

        if (shot && ++frames >= 2) {
            TakeScreenshot(shot);
            break;
        }
    }

    UnloadRenderTexture(stage);
    CloseWindow();
    return 0;
}
