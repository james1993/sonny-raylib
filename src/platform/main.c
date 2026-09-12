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
#include "../core/formula.h"

/* Verified from SONNY1.swf's header. */
#define STAGE_W   800
#define STAGE_H   575
#define STAGE_FPS 30

#define MAX_UNITS 8

typedef struct {
    Unit    units[MAX_UNITS];
    int32_t unit_count;
    int32_t active;             /* whose turn it is */
    int32_t round;
    int     selected_ability;
    Rng     rng;
} Game;

static int32_t roster_add(Game *g, const char *name, int32_t side, int32_t level,
                          int32_t life, int32_t focus, double str, double mag,
                          double spd, double per, double def)
{
    int32_t i = g->unit_count++;
    Unit *u = &g->units[i];
    memset(u, 0, sizeof(*u));
    snprintf(u->name, SONNY_NAME_LEN, "%s", name);
    u->playerID = i + 1;
    u->teamSide = side;
    u->plevel = level;
    u->active = 1;
    u->LIFEU = u->LIFEN = life;
    u->FOCUSU = u->FOCUSN = focus;
    u->STRENGTHU = str;
    u->MAGICU = mag;
    u->SPEEDU = spd;
    for (int e = 0; e < SONNY_ELEMENTS; e++) {
        u->PERU[e] = per;
        u->DEFU[e] = def;
    }
    return i;
}

/* Placeholder roster: real units come from the extracted unit table. */
static void game_init(Game *g)
{
    memset(g, 0, sizeof(*g));
    rng_seed(&g->rng, 20260912);
    rng_refill_krs(&g->rng);
    roster_add(g, "Sonny", 0, 5, 120, 100, 24, 4, 16, 25, 25);
    roster_add(g, "Louis", 0, 5, 100, 100, 12, 8, 12, 25, 25);
    roster_add(g, "Zombie", 1, 4, 90, 100, 18, 4, 10, 25, 25);
    roster_add(g, "ZPCI Assault", 1, 4, 95, 100, 14, 2, 9, 25, 25);
    g->round = 1;
    g->active = 0;
    g->selected_ability = -1;
}

/* Fires the placeholder basic attack at the first living enemy, so the damage
   math can be exercised from the running build. */
static void game_attack(Game *g)
{
    Unit *caster = &g->units[g->active];
    for (int32_t i = 0; i < g->unit_count; i++) {
        Unit *t = &g->units[i];
        if (t->teamSide == caster->teamSide || !t->active)
            continue;
        AbilityCoefs a;
        memset(&a, 0, sizeof(a));
        a.element = ELEM_PHYSICAL;
        a.strength_coef = 1.0;
        a.hit_coef = 1.0;
        a.damage_coef = 1.0;
        DamageResult d = formula_full_damage(&g->rng, caster, t, &a);
        formula_apply_damage(t, d.damage, NULL);
        break;
    }
    do {
        g->active = (g->active + 1) % g->unit_count;
        if (g->active == 0)
            g->round++;
    } while (!g->units[g->active].active);
}

static Rectangle unit_rect(const Game *g, int32_t i)
{
    int slot = 0;
    for (int32_t j = 0; j < i; j++)
        if (g->units[j].teamSide == g->units[i].teamSide)
            slot++;
    float x = (g->units[i].teamSide == 0) ? 70.0f : STAGE_W - 70.0f - 120.0f;
    float y = 100.0f + slot * 115.0f;
    return (Rectangle){x, y, 120.0f, 100.0f};
}

static void draw_bar(Rectangle r, int32_t cur, int32_t max, Color fill, const char *label)
{
    DrawRectangleRec(r, (Color){20, 20, 24, 255});
    if (max > 0 && cur > 0) {
        Rectangle f = r;
        f.width = r.width * ((float)cur / (float)max);
        if (f.width > r.width)
            f.width = r.width;
        DrawRectangleRec(f, fill);
    }
    DrawRectangleLinesEx(r, 1.0f, (Color){90, 90, 100, 255});
    DrawText(TextFormat("%s %d/%d", label, cur, max), (int)r.x + 3, (int)r.y + 1, 10, RAYWHITE);
}

static void draw_battle(const Game *g)
{
    ClearBackground((Color){24, 26, 32, 255});
    DrawText("SONNY -- engine shell", 12, 10, 20, (Color){200, 205, 215, 255});
    DrawText(TextFormat("Round %d   turn: %s   [space] attack", g->round,
                        g->units[g->active].name),
             12, 34, 10, (Color){150, 155, 165, 255});

    for (int32_t i = 0; i < g->unit_count; i++) {
        const Unit *u = &g->units[i];
        Rectangle r = unit_rect(g, i);
        Color frame = (i == g->active) ? (Color){235, 200, 90, 255}
                                       : (Color){70, 74, 86, 255};

        DrawRectangleRec(r, (Color){40, 44, 54, u->active ? 255 : 120});
        DrawRectangleLinesEx(r, (i == g->active) ? 2.0f : 1.0f, frame);
        DrawText(u->name, (int)r.x + 5, (int)r.y + 5, 10,
                 u->active ? RAYWHITE : GRAY);
        DrawText(TextFormat("Lv %d", u->plevel), (int)r.x + 5, (int)r.y + 74, 10,
                 (Color){140, 145, 155, 255});

        draw_bar((Rectangle){r.x + 5, r.y + 22, r.width - 10, 12}, u->LIFEN,
                 u->LIFEU, (Color){170, 55, 60, 255}, "HP");
        draw_bar((Rectangle){r.x + 5, r.y + 38, r.width - 10, 12}, u->FOCUSN,
                 u->FOCUSU, (Color){60, 105, 180, 255}, "FP");
        if (u->SHIELD > 0)
            DrawText(TextFormat("shield %d", u->SHIELD), (int)r.x + 5,
                     (int)r.y + 56, 10, (Color){120, 170, 220, 255});
    }

    for (int i = 0; i < 8; i++) {
        Rectangle slot = {12.0f + i * 56.0f, STAGE_H - 100.0f, 50.0f, 50.0f};
        DrawRectangleRec(slot, (Color){44, 48, 58, 255});
        DrawRectangleLinesEx(slot, g->selected_ability == i ? 2.0f : 1.0f,
                             g->selected_ability == i ? (Color){235, 200, 90, 255}
                                                      : (Color){80, 84, 96, 255});
    }
    DrawText("ability bar (icons + tooltips pending asset extraction)", 12,
             STAGE_H - 42, 10, (Color){130, 135, 145, 255});
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
            game_attack(&game);
        for (int k = 0; k < 8; k++)
            if (IsKeyPressed(KEY_ONE + k))
                game.selected_ability = k;

        BeginTextureMode(stage);
        draw_battle(&game);
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
