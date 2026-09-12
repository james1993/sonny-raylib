/* raylib front end.
 *
 * The original is a Flash game with a fixed 800x575 stage at 30 fps (read out
 * of SONNY1.swf's header), so everything is drawn into a render texture at
 * that logical size and scaled with letterboxing. Layout work is then 1:1 with
 * reference screenshots from the original.
 *
 * Art and audio are not in yet: this draws the real battle -- real abilities,
 * real enemies, real numbers -- with placeholder graphics.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "raylib.h"
#include "../core/battle.h"

#define STAGE_W   800
#define STAGE_H   575
#define STAGE_FPS 30

#define PLAYER_SLOT   1
#define ABILITY_SLOTS 8
#define LOG_LINES     6
#define RESOLVE_FRAMES 14   /* frames each resolved move stays on screen */

typedef struct {
    Battle    battle;
    Character player;
    int32_t ability_ids[ABILITY_SLOTS];
    int32_t selected;          /* ability slot, -1 = none */
    int32_t hovered_unit;
    int32_t queued;            /* the human has declared this phase */
    int32_t resolve_timer;
    char    log[LOG_LINES][128];
    int32_t log_count;
    MoveEvent last;
    int32_t  has_last;
} Game;

static void logf_line(Game *g, const char *fmt, ...)
{
    va_list args;
    char line[128];
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    if (g->log_count == LOG_LINES) {
        for (int i = 0; i < LOG_LINES - 1; i++)
            memcpy(g->log[i], g->log[i + 1], sizeof(g->log[0]));
        g->log_count--;
    }
    snprintf(g->log[g->log_count++], sizeof(g->log[0]), "%s", line);
}

static void game_init(Game *g, uint64_t seed)
{
    memset(g, 0, sizeof(*g));
    Battle *b = &g->battle;

    battle_init(b, seed, PLAYER_SLOT);

    /* Sonny: a level 5 Dreadnaught with the starting stat bonus the original
       hands out (Krin.StatSets0 = [0,5,0,3,0]) and four points spent. */
    g->player.class_template = unit_template_by_id(1);
    g->player.level = 5;
    g->player.spent[1] = 5 + 2;
    g->player.spent[3] = 3 + 2;

    /* Sonny is human-driven; his ally and the enemies run the game's AI. */
    battle_place_character(b, PLAYER_SLOT, &g->player, "Sonny", 0);
    battle_place_enemy(b, 3, unit_template_by_id(5), 5, 1);
    battle_place_enemy(b, 2, unit_template_by_name("Zombie"), 4, 1);
    battle_place_enemy(b, 4, unit_template_by_name("ZPCI Assault"), 4, 1);

    /* The player's bar holds the class's own move list. */
    const UnitTemplate *t = g->player.class_template;
    for (int i = 0; i < ABILITY_SLOTS; i++)
        g->ability_ids[i] = (i < t->moves_a_count) ? t->moves_a[i] : 0;

    battle_team_select(b);
    b->PrevTeam = b->TeamMove;
    for (int32_t i = 1; i < SONNY_SLOTS; i++)
        battle_queue(b, i, i, 0, 0);

    g->selected = -1;
    g->hovered_unit = -1;
    logf_line(g, "Battle begins. Team %d is faster and acts first.",
              b->TeamMove);
}

static int player_turn(const Game *g)
{
    const Battle *b = &g->battle;
    return b->phase == PHASE_DECLARE
        && b->units[PLAYER_SLOT].active
        && b->units[PLAYER_SLOT].teamSide == b->TeamMoveNow;
}

/* ------------------------------------------------------------------ layout */

static Rectangle unit_rect(const Battle *b, int32_t slot)
{
    int32_t row = b->brains[slot].teamAdder;
    float x = (b->units[slot].teamSide == 1) ? 40.0f : STAGE_W - 40.0f - 150.0f;
    float y = 70.0f + row * 92.0f;
    return (Rectangle){x, y, 150.0f, 82.0f};
}

static Rectangle ability_rect(int i)
{
    return (Rectangle){22.0f + i * 62.0f, STAGE_H - 104.0f, 56.0f, 56.0f};
}

static void draw_bar(Rectangle r, int32_t cur, int32_t max, Color fill,
                     const char *label)
{
    DrawRectangleRec(r, (Color){18, 18, 22, 255});
    if (max > 0 && cur > 0) {
        Rectangle f = r;
        f.width = r.width * ((float)cur / (float)max);
        if (f.width > r.width)
            f.width = r.width;
        DrawRectangleRec(f, fill);
    }
    DrawRectangleLinesEx(r, 1.0f, (Color){90, 90, 100, 255});
    DrawText(TextFormat("%s %d/%d", label, cur, max), (int)r.x + 4,
             (int)r.y + 1, 10, RAYWHITE);
}

static void draw_unit(const Game *g, int32_t slot)
{
    const Battle *b = &g->battle;
    const Unit *u = &b->units[slot];
    if (u->LIFEU == 0)
        return;

    Rectangle r = unit_rect(b, slot);
    int is_acting = (b->phase == PHASE_RESOLVE && g->has_last
                     && g->last.caster == slot);
    int is_target = (g->hovered_unit == slot);

    Color frame = is_acting ? (Color){235, 200, 90, 255}
                : is_target ? (Color){120, 200, 255, 255}
                            : (Color){70, 74, 86, 255};

    DrawRectangleRec(r, (Color){38, 42, 52, u->active ? 255 : 110});
    DrawRectangleLinesEx(r, (is_acting || is_target) ? 2.0f : 1.0f, frame);

    DrawText(u->name, (int)r.x + 6, (int)r.y + 5, 10,
             u->active ? RAYWHITE : GRAY);
    DrawText(TextFormat("Lv%d", u->plevel), (int)r.x + (int)r.width - 32,
             (int)r.y + 5, 10, (Color){150, 155, 165, 255});

    draw_bar((Rectangle){r.x + 6, r.y + 20, r.width - 12, 12}, u->LIFEN,
             u->LIFEU, (Color){170, 55, 60, 255}, "HP");
    draw_bar((Rectangle){r.x + 6, r.y + 35, r.width - 12, 12}, u->FOCUSN,
             u->FOCUSU, (Color){60, 105, 180, 255}, "FP");

    /* Active buffs, with their remaining turns. */
    int shown = 0;
    for (int32_t i = 0; i < SONNY_MAX_BUFFS && shown < 8; i++) {
        if (u->BUFFARRAYK[i].CD == 0)
            continue;
        const BuffDef *def = buff_find(SONNY_BUFFS, SONNY_BUFF_COUNT,
                                       u->BUFFARRAYK[i].buffId);
        Color c = def && def->change[1] < 0 ? (Color){175, 80, 80, 255}
                                            : (Color){110, 165, 110, 255};
        DrawRectangle((int)r.x + 6 + shown * 18, (int)r.y + 52, 16, 14, c);
        DrawText(TextFormat("%d", u->BUFFARRAYK[i].CD),
                 (int)r.x + 9 + shown * 18, (int)r.y + 54, 10, RAYWHITE);
        shown++;
    }
    if (u->SHIELD > 0)
        DrawText(TextFormat("shield %d", u->SHIELD), (int)r.x + 6,
                 (int)r.y + 68, 10, (Color){120, 190, 235, 255});
    if (u->STUN > 0)
        DrawText("stunned", (int)r.x + (int)r.width - 48, (int)r.y + 68, 10,
                 (Color){225, 200, 120, 255});
}

static void draw_ability_bar(const Game *g)
{
    for (int i = 0; i < ABILITY_SLOTS; i++) {
        Rectangle slot = ability_rect(i);
        const AbilityDef *a = ability_by_id(g->ability_ids[i]);
        int usable = a && g->ability_ids[i] != 0;
        int affordable = usable
            && g->battle.units[PLAYER_SLOT].FOCUSN >= a->focus_cost;

        DrawRectangleRec(slot, (Color){44, 48, 58, 255});
        DrawRectangleLinesEx(slot, g->selected == i ? 2.0f : 1.0f,
                             g->selected == i ? (Color){235, 200, 90, 255}
                                              : (Color){80, 84, 96, 255});
        if (!usable)
            continue;

        Color text = affordable ? RAYWHITE : (Color){130, 100, 100, 255};
        const char *label = (a->name && a->name[0]) ? a->name : a->icon;
        DrawText(TextFormat("%d", i + 1), (int)slot.x + 4, (int)slot.y + 3, 10,
                 (Color){150, 155, 165, 255});
        DrawText(label, (int)slot.x + 4, (int)slot.y + 18, 10, text);
        DrawText(TextFormat("%d fp", a->focus_cost), (int)slot.x + 4,
                 (int)slot.y + 42, 10, (Color){120, 160, 210, 255});
    }
}

static void draw_tooltip(const Game *g)
{
    if (g->selected < 0)
        return;
    const AbilityDef *a = ability_by_id(g->ability_ids[g->selected]);
    if (!a || !a->tooltip[0])
        return;

    Rectangle box = {22, STAGE_H - 150, STAGE_W - 44, 40};
    DrawRectangleRec(box, (Color){28, 30, 38, 240});
    DrawRectangleLinesEx(box, 1.0f, (Color){80, 84, 96, 255});
    DrawText((a->name && a->name[0]) ? a->name : a->icon, (int)box.x + 6,
             (int)box.y + 5, 10, (Color){235, 200, 90, 255});
    DrawText(a->tooltip, (int)box.x + 6, (int)box.y + 20, 10,
             (Color){200, 205, 215, 255});
}

static void draw_battle(const Game *g)
{
    const Battle *b = &g->battle;

    ClearBackground((Color){24, 26, 32, 255});
    DrawText("SONNY", 22, 12, 22, (Color){210, 215, 225, 255});

    const char *state = b->phase == PHASE_OVER
        ? (b->winCondition == 1 ? "victory"
           : b->winCondition == 0 ? "defeat" : "draw")
        : player_turn(g) ? "your move" : "enemy phase";
    DrawText(TextFormat("Round %d   %s", b->round, state), 22, 38, 10,
             (Color){150, 155, 165, 255});

    for (int32_t slot = 1; slot < SONNY_SLOTS; slot++)
        draw_unit(g, slot);

    draw_ability_bar(g);
    draw_tooltip(g);

    for (int32_t i = 0; i < g->log_count; i++)
        DrawText(g->log[i], 200, 330 + i * 13, 10,
                 (Color){160, 165, 175, 255});

    if (player_turn(g)) {
        const char *hint = (g->selected < 0)
            ? "pick an ability (1-8 or click), then click a target"
            : "click a target";
        DrawText(hint, 22, STAGE_H - 26, 10, (Color){235, 200, 90, 255});
    }
}

/* ------------------------------------------------------------------- input */

static int32_t unit_at(const Game *g, Vector2 p)
{
    for (int32_t slot = 1; slot < SONNY_SLOTS; slot++) {
        if (g->battle.units[slot].LIFEU == 0)
            continue;
        if (CheckCollisionPointRec(p, unit_rect(&g->battle, slot)))
            return slot;
    }
    return -1;
}

static void describe(Game *g, const MoveEvent *e)
{
    const Battle *b = &g->battle;
    const AbilityDef *a = ability_by_id(e->moveID);
    const char *name = (a && a->name && a->name[0]) ? a->name
                     : (a && a->icon[0]) ? a->icon : "pass";

    /* Empty slots queue a pass every phase; they are not participants. */
    if (e->caster <= 0 || b->units[e->caster].LIFEU == 0)
        return;
    if (e->moveID == 0) {
        logf_line(g, "%s waits.", b->units[e->caster].name);
        return;
    }
    if (e->missed) {
        logf_line(g, "%s cannot use %s.", b->units[e->caster].name, name);
        return;
    }
    switch (e->kind) {
    case KIND_HEAL:
        logf_line(g, "%s heals %s for %d.%s", b->units[e->caster].name,
                  b->units[e->target].name, e->amount,
                  e->pierced ? " (critical)" : "");
        break;
    case KIND_FOCUS:
        logf_line(g, "%s restores %d focus to %s.", b->units[e->caster].name,
                  e->amount, b->units[e->target].name);
        break;
    default:
        logf_line(g, "%s hits %s with %s for %d.%s", b->units[e->caster].name,
                  b->units[e->target].name, name, e->amount,
                  e->pierced ? " (pierced)" : "");
        break;
    }
    if (e->target_died)
        logf_line(g, "%s falls.", b->units[e->target].name);
}

static void handle_input(Game *g)
{
    Battle *b = &g->battle;
    Vector2 mouse = GetMousePosition();
    float scale = (float)GetScreenHeight() / STAGE_H;
    Vector2 stage = {(mouse.x - (GetScreenWidth() - STAGE_W * scale) / 2) / scale,
                     mouse.y / scale};

    g->hovered_unit = unit_at(g, stage);

    if (!player_turn(g) || g->queued)
        return;

    for (int i = 0; i < ABILITY_SLOTS; i++)
        if (IsKeyPressed(KEY_ONE + i) && g->ability_ids[i] != 0)
            g->selected = i;

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        for (int i = 0; i < ABILITY_SLOTS; i++) {
            if (CheckCollisionPointRec(stage, ability_rect(i))
                && g->ability_ids[i] != 0) {
                g->selected = i;
                return;
            }
        }
        if (g->selected >= 0 && g->hovered_unit > 0) {
            const AbilityDef *a = ability_by_id(g->ability_ids[g->selected]);
            Unit *target = &b->units[g->hovered_unit];
            int enemy = target->teamSide != b->units[PLAYER_SLOT].teamSide;

            /* Respect the ability's own targeting flags. */
            int ok = target->active
                  && ((enemy && a->target_enemy)
                      || (!enemy && (a->target_ally
                                     || (a->target_self
                                         && g->hovered_unit == PLAYER_SLOT))));
            if (ok && b->units[PLAYER_SLOT].FOCUSN >= a->focus_cost) {
                battle_queue(b, PLAYER_SLOT, g->hovered_unit,
                             g->ability_ids[g->selected], 0);
                g->queued = 1;
                g->selected = -1;
            }
        }
    }
    if (IsKeyPressed(KEY_SPACE)) {   /* pass */
        battle_queue(b, PLAYER_SLOT, PLAYER_SLOT, 0, 0);
        g->queued = 1;
    }
}

static void advance(Game *g)
{
    Battle *b = &g->battle;

    if (b->phase == PHASE_OVER)
        return;

    if (b->phase == PHASE_DECLARE) {
        if (player_turn(g) && !g->queued)
            return;                 /* waiting on the human */
        battle_declare_phase(b);
        g->queued = 0;
        g->resolve_timer = 0;
        return;
    }

    if (b->phase == PHASE_RESOLVE) {
        if (g->resolve_timer > 0) {
            g->resolve_timer--;
            return;
        }
        MoveEvent e;
        if (battle_resolve_step(b, &e)) {
            g->last = e;
            g->has_last = 1;
            describe(g, &e);
            g->resolve_timer = RESOLVE_FRAMES;
        } else if (b->phase != PHASE_OVER) {
            battle_end_phase(b);
        }
    }
}

int main(int argc, char **argv)
{
    uint64_t seed = (argc > 1) ? strtoull(argv[1], NULL, 10) : 20260912;

    Game game;
    game_init(&game, seed);

    SetTraceLogLevel(LOG_WARNING);
    InitWindow(STAGE_W, STAGE_H, "Sonny");
    SetTargetFPS(STAGE_FPS);
    RenderTexture2D stage = LoadRenderTexture(STAGE_W, STAGE_H);
    SetTextureFilter(stage.texture, TEXTURE_FILTER_POINT);

    /* One-shot render for headless checks: SONNY_SHOT=path, SONNY_STEPS=n. */
    const char *shot = getenv("SONNY_SHOT");
    int steps = getenv("SONNY_STEPS") ? atoi(getenv("SONNY_STEPS")) : 0;
    int frames = 0;

    while (!WindowShouldClose()) {
        handle_input(&game);
        if (shot) {
            /* Headless: drive the AI ally and enemies, and pass for Sonny so
               the battle progresses without input. */
            if (player_turn(&game) && !game.queued) {
                battle_queue(&game.battle, PLAYER_SLOT, PLAYER_SLOT, 0, 0);
                game.queued = 1;
            }
            game.resolve_timer = 0;
        }
        advance(&game);

        BeginTextureMode(stage);
        draw_battle(&game);
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

    UnloadRenderTexture(stage);
    CloseWindow();
    return 0;
}
