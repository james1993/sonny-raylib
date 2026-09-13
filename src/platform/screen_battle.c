/* The battle screen: the fight itself, drawn over the zone's backdrop with the
 * characters standing where the original stands them.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "assets.h"
#include "audio.h"
#include "game.h"

static int player_turn(const Game *g)
{
    const Battle *b = &g->battle;
    return b->phase == PHASE_DECLARE
        && b->units[PLAYER_SLOT].active
        && b->units[PLAYER_SLOT].teamSide == b->TeamMoveNow;
}

/* ------------------------------------------------------------------ layout */

/* The health and focus bar, where the original's root timeline places it
   (p1BAR..p6BAR): the two teams in columns near the top, ordered slot 5, 1, 3
   down the left and 6, 2, 4 down the right. The bar art is 201 x 28.75 at a
   scale of about 1.085, and the right-hand team's is mirrored, so its origin
   is its right edge. */
static Rectangle unit_rect(const Battle *b, int32_t slot)
{
    const StageBar *bar = stage_bar(slot);
    if (!bar) {
        int32_t row = (slot <= 2) ? 1 : (slot <= 4) ? 2 : 0;
        float x = (b->units[slot].teamSide == 1) ? 12.0f
                                                 : STAGE_W - 12.0f - 148.0f;
        return (Rectangle){x, 64.0f + row * 66.0f, 148.0f, 60.0f};
    }

    (void)b;
    float w = bar->width * bar->scale;
    float h = bar->height * bar->scale;
    /* The bar's own origin is its centre, so the placement point is where the
       middle of it goes. */
    return (Rectangle){bar->x - bar->origin_x * bar->scale,
                       bar->y - bar->origin_y * bar->scale, w, h};
}

/* Where a unit's model stands: the original's own stage layout, so position
   follows the slot -- not the speed order, which only decides who acts when. */
static Vector2 unit_stage_pos(const Battle *b, int32_t slot)
{
    (void)b;
    const StageSlot *s = stage_slot(slot);
    if (s)
        return (Vector2){s->x, s->y};
    return (Vector2){STAGE_W / 2.0f, STAGE_H / 2.0f};
}

/* Which animation a unit is playing, from its state and what is resolving. */
static const char *unit_animation(const Game *g, int32_t slot, int *loop)
{
    const Battle *b = &g->battle;
    const Unit *u = &b->units[slot];

    *loop = 1;
    if (!u->active)
        return (*loop = 0, "dead");
    if (u->STUN > 0)
        return "stun";

    if (b->phase == PHASE_RESOLVE && g->has_last) {
        if (g->last.caster == slot && g->last.moveID != 0) {
            *loop = 0;
            const AbilityDef *a = ability_by_id(g->last.moveID);
            if (a && a->delivery == DELIVER_MELEE)
                return "attack1";
            return "cast";
        }
        if (g->last.target == slot && g->last.amount > 0
            && g->last.kind == KIND_FULL_DAMAGE) {
            *loop = 0;
            return "hit";
        }
    }
    return "stand";
}

/* The impact graphic an ability names, played at the unit it landed on. */
static void draw_effect(const Game *g)
{
    if (!g->effect || g->effect_slot <= 0)
        return;
    int32_t frames = asset_frame_count(g->effect);
    if (frames <= 0 || g->effect_tick >= frames)
        return;

    Vector2 pos = unit_stage_pos(&g->battle, g->effect_slot);
    asset_draw_placed(g->effect, g->effect_tick + 1, pos, 1.0f, WHITE);
}

static void draw_doll(const Game *g, int32_t slot)
{
    const Unit *u = &g->battle.units[slot];
    if (u->LIFEU == 0)
        return;

    DollSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.gender = u->model_gender;
    spec.skin = u->model_skin;
    spec.hair = u->model_hair;
    for (int i = 0; i < 7; i++)
        spec.looks[i] = u->looks[i];

    int loop = 1;
    const char *animation = unit_animation(g, slot, &loop);
    int32_t frame = doll_animation_frame(animation, g->anim_tick / 2, loop);

    /* The right-hand team's containers are mirrored in the original. */
    const StageSlot *s = stage_slot(slot);
    int flip = s ? s->flip : (u->teamSide == 2);
    Color tint = u->active ? WHITE : (Color){255, 255, 255, 150};
    doll_draw(&spec, frame, unit_stage_pos(&g->battle, slot), 1.0f, flip, tint);
}

/* The bar sits where the original's does: its pieces (the backing, the
   selected-move icon, the clock) are all placed at (400, 508) on the root
   timeline, so the eight slots are laid out symmetrically about that. */
#define BAR_CENTER_X 400.0f
#define BAR_CENTER_Y 508.0f
#define BAR_SLOT     46.0f

static Rectangle ability_rect(int i)
{
    float total = ABILITY_SLOTS * BAR_SLOT;
    return (Rectangle){BAR_CENTER_X - total / 2 + i * BAR_SLOT,
                       BAR_CENTER_Y - BAR_SLOT / 2, BAR_SLOT - 4,
                       BAR_SLOT - 4};
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
    ui_text(TextFormat("%s %d/%d", label, cur, max), (int)r.x + 4,
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

    DrawRectangleRec(r, (Color){20, 22, 28, u->active ? 200 : 120});
    DrawRectangleLinesEx(r, (is_acting || is_target) ? 2.0f : 1.0f, frame);

    ui_text(u->name, (int)r.x + 6, (int)r.y + 2, 10,
             u->active ? RAYWHITE : GRAY);
    ui_text(TextFormat("Lv%d", u->plevel), (int)(r.x + r.width - 30),
             (int)r.y + 2, 10, (Color){150, 155, 165, 255});

    draw_bar((Rectangle){r.x + 6, r.y + 14, r.width - 12, 8}, u->LIFEN,
             u->LIFEU, (Color){170, 55, 60, 255}, "HP");
    draw_bar((Rectangle){r.x + 6, r.y + 23, r.width - 12, 8}, u->FOCUSN,
             u->FOCUSU, (Color){60, 105, 180, 255}, "FP");

    /* Active buffs, with their remaining turns. */
    int shown = 0;
    for (int32_t i = 0; i < SONNY_MAX_BUFFS && shown < 8; i++) {
        if (u->BUFFARRAYK[i].CD == 0)
            continue;
        const BuffDef *def = buff_find(SONNY_BUFFS, SONNY_BUFF_COUNT,
                                       u->BUFFARRAYK[i].buffId);
        Rectangle box = {r.x + 6 + shown * 16, r.y + r.height + 2, 14, 12};
        /* Buff icons are named by the buff key. The permanent passive buffs
           have no icon frame in the original either, so those fall back to a
           coloured block. */
        if (!asset_draw_fit(u->BUFFARRAYK[i].buffId, 1, box, WHITE)) {
            Color c = def && def->change[1] < 0 ? (Color){175, 80, 80, 255}
                                                : (Color){110, 165, 110, 255};
            DrawRectangleRec(box, c);
        }
        if (u->BUFFARRAYK[i].CD > 0)
            ui_text(TextFormat("%d", u->BUFFARRAYK[i].CD),
                     (int)box.x + 3, (int)box.y + 2, 10, RAYWHITE);
        shown++;
    }
    if (u->SHIELD > 0)
        ui_text(TextFormat("shield %d", u->SHIELD), (int)r.x + 6,
                 (int)r.y + 68, 10, (Color){120, 190, 235, 255});
    if (u->STUN > 0)
        ui_text("stunned", (int)r.x + (int)r.width - 48, (int)r.y + 68, 10,
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
        if (!usable) {
            DrawRectangleLinesEx(slot, 1.0f, (Color){80, 84, 96, 255});
            continue;
        }

        Color text = affordable ? RAYWHITE : (Color){130, 100, 100, 255};
        Color tint = affordable ? WHITE : (Color){150, 120, 120, 255};
        /* The real icon, by the name the ability itself carries. */
        Rectangle inner = {slot.x + 2, slot.y + 2, slot.width - 4,
                           slot.height - 4};
        if (!asset_draw_fit(a->icon, 1, inner, tint)) {
            const char *label = (a->name && a->name[0]) ? a->name : a->icon;
            ui_text(label, (int)slot.x + 4, (int)slot.y + 18, 10, text);
        }
        DrawRectangleLinesEx(slot, g->selected == i ? 2.0f : 1.0f,
                             g->selected == i ? (Color){235, 200, 90, 255}
                                              : (Color){80, 84, 96, 255});
        ui_text(TextFormat("%d", i + 1), (int)slot.x + 3, (int)slot.y + 2, 10,
                 (Color){235, 235, 245, 255});
        ui_text(TextFormat("%d", a->focus_cost),
                 (int)(slot.x + slot.width - 12), (int)slot.y + 2, 10,
                 (Color){150, 200, 255, 255});
    }
}

static void draw_tooltip(const Game *g)
{
    if (g->selected < 0)
        return;
    const AbilityDef *a = ability_by_id(g->ability_ids[g->selected]);
    if (!a || !a->tooltip[0])
        return;

    Rectangle box = {22, 404, STAGE_W - 44, 40};
    DrawRectangleRec(box, (Color){28, 30, 38, 240});
    DrawRectangleLinesEx(box, 1.0f, (Color){80, 84, 96, 255});
    ui_text((a->name && a->name[0]) ? a->name : a->icon, (int)box.x + 6,
             (int)box.y + 5, 10, (Color){235, 200, 90, 255});
    ui_text(a->tooltip, (int)box.x + 6, (int)box.y + 20, 10,
             (Color){200, 205, 215, 255});
}

/* The battle backdrop: the roster names a sky and a zone graphic, drawn in
   that order as the original layers them. */
static void draw_backdrop(const Game *g)
{
    /* The battle screen's own origin, which everything inside it is placed
       relative to. */
    Vector2 screen = {400.0f, 294.5f};
    int drew = 0;

    if (g->def && g->def->sky_bg[0])
        drew |= asset_draw_placed(g->def->sky_bg, 1, screen, 1.0f, WHITE);
    if (g->def && g->def->zone_bg[0])
        drew |= asset_draw_placed(g->def->zone_bg, 1, screen, 1.0f, WHITE);
    if (!drew)
        ClearBackground((Color){24, 26, 32, 255});

    /* Keep the bars legible over the art: they sit in a band across the top. */
    DrawRectangle(0, 0, STAGE_W, 120, (Color){12, 13, 17, 150});
    DrawRectangle(0, (int)(BAR_CENTER_Y - 34), STAGE_W,
                  STAGE_H - (int)(BAR_CENTER_Y - 34), (Color){12, 13, 17, 190});
}

/* --------------------------------------------------------------- dialogue */

/* Show the next line whose turn and sequence have come up, if its speaker is
   still alive. Returns 1 while a line is on screen, which holds the fight. */
static int speech_update(Game *g)
{
    if (g->speech) {
        if (--g->speech_timer > 0)
            return 1;
        g->speech = NULL;
    }
    if (!g->def || g->speech_index >= g->def->speech_count)
        return 0;

    const Speech *next = &g->def->speeches[g->speech_index];
    if (next->turn != g->turn_counter || next->sequence != g->speech_seq)
        return 0;

    g->speech_index++;
    g->speech_seq++;
    /* A line from a unit that is already dead is skipped, not shown. */
    if (next->speaker < 1 || next->speaker >= SONNY_SLOTS
        || !g->battle.units[next->speaker].active)
        return 0;

    g->speech = next;
    g->speech_timer = (int32_t)(next->seconds * STAGE_FPS);
    if (next->voice_over && next->voice_over[0])
        audio_play(next->voice_over);
    return 1;
}

static void draw_speech(const Game *g)
{
    if (!g->speech)
        return;
    const Unit *speaker = &g->battle.units[g->speech->speaker];

    /* The original parks the box on the speaker's side of the screen, at
       (21.8, 460.7) for the left team and (473.3, 460.7) for the right. Its
       art rises from that origin -- the ability bar sits at y 508, so a box
       drawn downward from 460 would cover it. */
    float x = (speaker->teamSide == 2) ? 473.3f : 21.8f;
    float y = 460.7f;
    float height = 52;
    Rectangle box = {x, y - height, 305, height};

    DrawRectangleRec(box, (Color){16, 18, 24, 235});
    DrawRectangleLinesEx(box, 1.0f, (Color){120, 124, 140, 255});
    ui_text(speaker->name, (int)box.x + 8, (int)box.y + 6, 10,
             (Color){235, 200, 90, 255});

    /* Wrap the line to the box. */
    const char *text = g->speech->say;
    char line[96];
    int start = 0, last_space = -1, row = 0;
    for (int i = 0; text[i] && row < 2; i++) {
        int len = i - start + 1;
        if (text[i] == ' ')
            last_space = i;
        if (len < 52 && text[i + 1])
            continue;
        int end = (text[i + 1] && last_space > start) ? last_space : i + 1;
        int count = end - start;
        if (count > (int)sizeof(line) - 1)
            count = (int)sizeof(line) - 1;
        memcpy(line, text + start, count);
        line[count] = 0;
        ui_text(line, (int)box.x + 8, (int)box.y + 20 + row * 11, 10,
                 (Color){215, 220, 230, 255});
        row++;
        if (row >= 2)
            break;
        start = (end == last_space) ? end + 1 : end;
        last_space = -1;
        i = start - 1;
    }
}

static void draw_battle(const Game *g)
{
    const Battle *b = &g->battle;

    ClearBackground((Color){24, 26, 32, 255});
    draw_backdrop(g);
    ui_text("SONNY", 330, 8, 18, (Color){210, 215, 225, 255});

    const char *state = b->phase == PHASE_OVER
        ? (b->winCondition == 1 ? "victory"
           : b->winCondition == 0 ? "defeat" : "draw")
        : player_turn(g) ? "your move" : "enemy phase";
    ui_text(TextFormat("Round %d   %s", b->round, state), 330, 30, 10,
             (Color){150, 155, 165, 255});

    /* The models first, then the information panels over them. */
    for (int32_t slot = 1; slot < SONNY_SLOTS; slot++)
        draw_doll(g, slot);
    for (int32_t slot = 1; slot < SONNY_SLOTS; slot++)
        draw_unit(g, slot);
    draw_effect(g);

    draw_ability_bar(g);
    draw_tooltip(g);
    draw_speech(g);

    for (int32_t i = 0; i < g->log_count; i++)
        ui_text(g->log[i], 200, 320 + i * 13, 10,
                 (Color){160, 165, 175, 255});

    if (player_turn(g)) {
        const char *hint = (g->selected < 0)
            ? "pick an ability (1-8 or click), then click a target"
            : "click a target";
        ui_text(hint, 22, STAGE_H - 32, 10, (Color){235, 200, 90, 255});
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

/* What a resolved move sounds and looks like: the ability's own effect sound
   and impact graphic, then the target's hit grunt or death cry. */
static void present(Game *g, const MoveEvent *e)
{
    const Battle *b = &g->battle;
    const AbilityDef *a = ability_by_id(e->moveID);

    g->effect = NULL;
    g->effect_slot = 0;
    g->effect_tick = 0;

    if (!a || e->moveID == 0 || e->missed)
        return;

    if (a->sound && a->sound[0])
        audio_play(a->sound);
    if (a->model && a->model[0] && asset_frame_count(a->model) > 0) {
        g->effect = a->model;
        g->effect_slot = e->target;
    }

    const Unit *target = &b->units[e->target];
    if (e->target_died) {
        if (target->voice_die[0])
            audio_play(target->voice_die);
    } else if (e->kind == KIND_FULL_DAMAGE && e->amount > 0) {
        /* The original picks one of three at random. */
        int32_t pick = GetRandomValue(0, 2);
        if (target->voice_hit[pick][0])
            audio_play(target->voice_hit[pick]);
    }
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
        game_log(g, "%s waits.", b->units[e->caster].name);
        return;
    }
    if (e->missed) {
        game_log(g, "%s cannot use %s.", b->units[e->caster].name, name);
        return;
    }
    switch (e->kind) {
    case KIND_HEAL:
        game_log(g, "%s heals %s for %d.%s", b->units[e->caster].name,
                  b->units[e->target].name, e->amount,
                  e->pierced ? " (critical)" : "");
        break;
    case KIND_FOCUS:
        game_log(g, "%s restores %d focus to %s.", b->units[e->caster].name,
                  e->amount, b->units[e->target].name);
        break;
    default:
        game_log(g, "%s hits %s with %s for %d.%s", b->units[e->caster].name,
                  b->units[e->target].name, name, e->amount,
                  e->pierced ? " (pierced)" : "");
        break;
    }
    if (e->target_died)
        game_log(g, "%s falls.", b->units[e->target].name);
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
            present(g, &e);
            g->resolve_timer = RESOLVE_FRAMES;
        } else if (b->phase != PHASE_OVER) {
            battle_end_phase(b);
            /* A completed phase advances the dialogue's turn counter and
               resets its within-turn sequence. */
            g->turn_counter++;
            g->speech_seq = 0;
        }
    }
}

/* ----------------------------------------------------------------- entry */

void battle_screen_start(Game *g, int32_t battle_id)
{
    const BattleDef *def = battle_def_by_id(battle_id);
    if (!def) {
        game_notice(g, "No such battle.");
        return;
    }

    /* The bar holds what the character actually knows. */
    int32_t known[SONNY_TALENT_MAX + 2];
    int32_t n = character_known_abilities(&g->campaign.player, known,
                                          SONNY_TALENT_MAX + 2);
    for (int i = 0; i < ABILITY_SLOTS; i++) {
        int32_t equipped = (i < SONNY_MOVE_SLOTS)
                         ? g->campaign.player.move_matrix[i] : 0;
        g->ability_ids[i] = equipped ? equipped : (i < n ? known[i] : 0);
    }

    g->def = def;
    g->seed = rng_next(&g->rng);
    campaign_setup_battle(&g->campaign, def, &g->battle, g->seed);

    /* Drops are rolled at the start of the battle, as the original does. */
    g->dropped_count = campaign_roll_drops(def, &g->rng, g->dropped,
                                           SONNY_MAX_DROPPED);
    memset(g->taken, 0, sizeof(g->taken));

    g->selected = -1;
    g->hovered_unit = -1;
    g->queued = 0;
    g->resolve_timer = 0;
    g->has_last = 0;
    g->effect = NULL;
    g->log_count = 0;
    g->screen = SCREEN_BATTLE;
    g->turn_counter = 0;
    g->speech_seq = 0;
    g->speech_index = 0;
    g->speech_timer = 0;
    g->speech = NULL;

    game_log(g, "%s%d. Team %d is faster and acts first.",
             lang_text("SYSTEM", 10), g->campaign.progress_battle - 1,
             g->battle.TeamMove);
    if (audio_ready())
        audio_music("BattleMusic1loopable");
}

void battle_screen_update(Game *g, Vector2 mouse, int headless)
{
    g->hovered_unit = unit_at(g, mouse);
    handle_input(g);

    if (headless && player_turn(g) && !g->queued) {
        battle_queue(&g->battle, PLAYER_SLOT, PLAYER_SLOT, 0, 0);
        g->queued = 1;
    }

    g->anim_tick++;
    if (g->effect)
        g->effect_tick++;

    /* Dialogue holds the fight, as speechDone does in the original. */
    if (speech_update(g))
        return;
    advance(g);

    /* When the fight ends, pay out and move on. */
    if (g->battle.phase == PHASE_OVER) {
        if (g->battle.winCondition == 1) {
            g->rewards = campaign_award(&g->campaign, &g->battle, &g->rng);
            /* Only a progress battle advances the campaign. */
            if (g->def && g->def->id == g->campaign.progress_battle)
                campaign_advance(&g->campaign);
            g->screen = SCREEN_VICTORY;
        } else {
            game_notice(g, "Defeated.");
            g->screen = SCREEN_ZONE;
        }
        if (audio_ready())
            audio_music(NULL);
    }
}

void battle_screen_draw(Game *g)
{
    draw_battle(g);
}
