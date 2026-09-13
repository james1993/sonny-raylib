/* The screens outside battle: the zone hub and everything it opens.
 *
 * Text comes from the game's own language arrays, so the menu reads exactly as
 * the original's does (SYSTEM[13..28]), as do the zone names, item names and
 * victory lines.
 */
#include <stdio.h>
#include <string.h>
#include "assets.h"
#include "audio.h"
#include "game.h"

/* ------------------------------------------------------------ zone hub */

typedef struct {
    const char *label;
    const char *hint;
    Screen      goes_to;
    int         action;     /* 0 screen, 1 battle, 2 save, 3 training */
} MenuEntry;

/* SYSTEM[13..28], in the order the original lists them. */
static MenuEntry MENU[] = {
    {NULL, NULL, SCREEN_MAP,       0},   /* World Map      */
    {NULL, NULL, SCREEN_BATTLE,    1},   /* Next Battle    */
    {NULL, NULL, SCREEN_SHOP,      0},   /* Item Store     */
    {NULL, NULL, SCREEN_INVENTORY, 0},   /* Inventory      */
    {NULL, NULL, SCREEN_TALENTS,   0},   /* Abilities      */
    {NULL, NULL, SCREEN_ZONE,      2},   /* Save Game      */
    {NULL, NULL, SCREEN_BATTLE,    3},   /* Training Fight */
};
#define MENU_COUNT ((int)(sizeof(MENU) / sizeof(MENU[0])))

static void menu_text(void)
{
    /* Filled once from the language table, by the original's own indices. */
    static int done;
    if (done)
        return;
    MENU[0].label = lang_text("SYSTEM", 27);   /* World Map */
    MENU[0].hint = lang_text("SYSTEM", 28);
    MENU[1].label = lang_text("SYSTEM", 13);
    MENU[1].hint = lang_text("SYSTEM", 14);
    MENU[2].label = lang_text("SYSTEM", 15);
    MENU[2].hint = lang_text("SYSTEM", 16);
    MENU[3].label = lang_text("SYSTEM", 17);
    MENU[3].hint = lang_text("SYSTEM", 18);
    MENU[4].label = lang_text("SYSTEM", 19);
    MENU[4].hint = lang_text("SYSTEM", 20);
    MENU[5].label = lang_text("SYSTEM", 21);
    MENU[5].hint = lang_text("SYSTEM", 22);
    MENU[6].label = lang_text("SYSTEM", 29);
    MENU[6].hint = lang_text("SYSTEM", 30);
    done = 1;
}

static Rectangle menu_rect(int i)
{
    return (Rectangle){60, 196 + i * 38, 260, 31};
}

/* A random training fight from the zone's own list, as the original picks one. */
static int32_t training_battle(Game *g)
{
    const ZoneDef *zone = campaign_zone(&g->campaign);
    if (!zone || zone->training_count == 0)
        return 0;
    int32_t index = (int32_t)rng_below(&g->rng, (uint32_t)zone->training_count);
    return zone->training[index];
}

void screen_zone_update(Game *g, Vector2 mouse)
{
    menu_text();
    g->hovered_item = -1;

    for (int i = 0; i < MENU_COUNT; i++) {
        Rectangle r = menu_rect(i);
        if (hit(r, mouse))
            g->hovered_item = i;

        int enabled = 1;
        if (MENU[i].action == 3 && training_battle(g) == 0)
            enabled = 0;
        if (MENU[i].action == 1 && campaign_complete(&g->campaign))
            enabled = 0;

        if (!draw_button(r, MENU[i].label ? MENU[i].label : "", mouse, enabled))
            continue;

        audio_play("Click3pickup");
        switch (MENU[i].action) {
        case 1:
            battle_screen_start(g, g->campaign.progress_battle);
            break;
        case 2:
            if (save_write(&g->campaign, SONNY_SAVE_PATH) == 0)
                game_notice(g, "Progress saved.");
            else
                game_notice(g, "Could not write the save file.");
            break;
        case 3: {
            int32_t id = training_battle(g);
            if (id)
                battle_screen_start(g, id);
            break;
        }
        default:
            g->screen = MENU[i].goes_to;
            break;
        }
        return;
    }
}

static void draw_character_summary(const Game *g, Rectangle r)
{
    const Character *c = &g->campaign.player;
    DerivedStats d = character_derive(c);

    draw_panel(r, "Sonny");
    int y = (int)r.y + 26;
    const char *cls = c->class_template ? c->class_template->name : "?";
    DrawText(TextFormat("%s%d  %s", lang_text("MENU", 0), c->level, cls),
             (int)r.x + 10, y, 10, RAYWHITE);
    y += 18;

    /* Stat names come from the language table: Vitality, Strength, ... */
    const double values[5] = {d.life, d.strength, d.magic, d.speed, d.focus};
    for (int i = 0; i < 5; i++) {
        DrawText(TextFormat("%-10s %.0f", lang_text("SYSTEM", i), values[i]),
                 (int)r.x + 10, y, 10, (Color){200, 205, 215, 255});
        y += 14;
    }
    y += 6;
    DrawText(TextFormat("%s %.0f / %s %.0f", lang_text("SYSTEM", 5), d.per[0],
                        lang_text("SYSTEM", 6), d.def[0]),
             (int)r.x + 10, y, 10, (Color){150, 160, 175, 255});
    y += 18;
    DrawText(TextFormat("Euros %d", g->campaign.euros), (int)r.x + 10, y, 10,
             (Color){225, 200, 120, 255});
    y += 14;
    int32_t points = character_unspent_skill_points(&g->campaign.player);
    if (points > 0)
        DrawText(TextFormat("%d ability point%s to spend", points,
                            points == 1 ? "" : "s"),
                 (int)r.x + 10, y, 10, (Color){140, 210, 140, 255});
}

void screen_zone_draw(Game *g, Vector2 mouse)
{
    menu_text();
    const ZoneDef *zone = campaign_zone(&g->campaign);

    ClearBackground((Color){18, 20, 26, 255});
    /* The zone's own backdrop, dimmed, behind the menu. */
    const BattleDef *def = battle_def_by_id(g->campaign.progress_battle);
    if (def && def->zone_bg[0])
        asset_draw_placed(def->zone_bg, 1, (Vector2){400, 294.5f}, 1.0f,
                          (Color){255, 255, 255, 70});

    DrawText("SONNY", 22, 12, 22, (Color){210, 215, 225, 255});
    DrawText(TextFormat("%s%d", lang_text("SYSTEM", 9), zone->zone + 1), 60,
             120, 20, (Color){235, 200, 90, 255});
    DrawText(TextFormat("%s: %s", zone->name, zone->subtitle), 60, 146, 10,
             (Color){200, 205, 215, 255});
    DrawText(TextFormat("%s%d", lang_text("SYSTEM", 10),
                        g->campaign.progress_battle - 1),
             60, 166, 10, (Color){150, 160, 175, 255});

    for (int i = 0; i < MENU_COUNT; i++) {
        Rectangle r = menu_rect(i);
        int enabled = !(MENU[i].action == 3 && training_battle((Game *)g) == 0)
                   && !(MENU[i].action == 1
                        && campaign_complete(&g->campaign));
        draw_button(r, MENU[i].label ? MENU[i].label : "", mouse, enabled);
    }
    if (g->hovered_item >= 0 && g->hovered_item < MENU_COUNT)
        DrawText(MENU[g->hovered_item].hint ? MENU[g->hovered_item].hint : "",
                 60, 210 + MENU_COUNT * 42 + 8, 10,
                 (Color){170, 175, 185, 255});

    draw_character_summary(g, (Rectangle){STAGE_W - 260, 120, 220, 200});

    if (campaign_complete(&g->campaign))
        DrawText("The campaign is complete.", 60, 470, 12,
                 (Color){235, 200, 90, 255});
}

/* ----------------------------------------------------------- world map */

/* A zone is on the map once progress has reached the battle before its first
   -- the original's `progressLevelOn >= progressArray[i][0] - 1`. */
static int zone_unlocked(const Campaign *c, const ZoneDef *zone)
{
    return c->progress_battle >= zone->first_battle - 1;
}

static Rectangle zone_rect(int i)
{
    return (Rectangle){90 + i * 170, 240, 150, 96};
}

void screen_map_update(Game *g, Vector2 mouse)
{
    g->hovered_item = -1;
    for (int i = 0; i < SONNY_ZONE_COUNT; i++) {
        const ZoneDef *zone = &SONNY_ZONES[i];
        if (!zone_unlocked(&g->campaign, zone))
            continue;
        Rectangle r = zone_rect(i);
        if (hit(r, mouse))
            g->hovered_item = i;
        if (!hit(r, mouse) || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            continue;
        /* Travelling changes where you are, not how far you have got. */
        g->campaign.zone = zone->zone;
        audio_play("Click3pickup");
        game_notice(g, "%s", zone->name);
        g->screen = SCREEN_ZONE;
        return;
    }
    if (draw_button((Rectangle){STAGE_W - 120, STAGE_H - 44, 100, 28}, "Back",
                    mouse, 1))
        g->screen = SCREEN_ZONE;
}

void screen_map_draw(Game *g, Vector2 mouse)
{
    ClearBackground((Color){18, 20, 26, 255});
    DrawText(lang_text("SYSTEM", 27), 22, 16, 20, (Color){210, 215, 225, 255});

    /* The original draws a line from each unlocked zone back to the one
       before it, so the route reads as a path. */
    for (int i = 1; i < SONNY_ZONE_COUNT; i++) {
        if (!zone_unlocked(&g->campaign, &SONNY_ZONES[i]))
            continue;
        Rectangle a = zone_rect(i - 1);
        Rectangle b = zone_rect(i);
        DrawLineEx((Vector2){a.x + a.width, a.y + a.height / 2},
                   (Vector2){b.x, b.y + b.height / 2}, 2.0f,
                   (Color){90, 94, 110, 255});
    }

    for (int i = 0; i < SONNY_ZONE_COUNT; i++) {
        const ZoneDef *zone = &SONNY_ZONES[i];
        Rectangle r = zone_rect(i);
        if (!zone_unlocked(&g->campaign, zone)) {
            DrawRectangleLinesEx(r, 1.0f, (Color){44, 46, 54, 255});
            continue;
        }
        int here = (g->campaign.zone == zone->zone);
        DrawRectangleRec(r, (Color){28, 31, 39, 255});
        /* Each zone shows its own backdrop. */
        const BattleDef *def = battle_def_by_id(zone->first_battle);
        if (def && def->zone_bg[0]) {
            BeginScissorMode((int)r.x, (int)r.y, (int)r.width, (int)r.height);
            asset_draw_cover(def->zone_bg, 1, r, (Color){255, 255, 255, 150});
            EndScissorMode();
        }
        DrawRectangleLinesEx(r, (here || hit(r, mouse)) ? 2.0f : 1.0f,
                             here ? (Color){235, 200, 90, 255}
                                  : (Color){90, 94, 110, 255});
        DrawText(TextFormat("%s%d", lang_text("SYSTEM", 9), zone->zone + 1),
                 (int)r.x + 8, (int)r.y + 8, 10, (Color){235, 200, 90, 255});
        DrawText(zone->name, (int)r.x + 8, (int)(r.y + r.height - 26), 10,
                 RAYWHITE);
        DrawText(zone->subtitle, (int)r.x + 8, (int)(r.y + r.height - 14), 10,
                 (Color){170, 175, 185, 255});
    }

    if (g->hovered_item >= 0 && g->hovered_item < SONNY_ZONE_COUNT) {
        const ZoneDef *zone = &SONNY_ZONES[g->hovered_item];
        DrawText(TextFormat("%s%d to %s%d", lang_text("SYSTEM", 10),
                            zone->first_battle - 1, lang_text("SYSTEM", 10),
                            zone->last_battle - 1),
                 90, 370, 10, (Color){170, 175, 185, 255});
    }
    draw_button((Rectangle){STAGE_W - 120, STAGE_H - 44, 100, 28}, "Back",
                mouse, 1);
}

/* ------------------------------------------------------------- talents */

static Rectangle talent_rect(int32_t node)
{
    int col = node % 7;
    int row = node / 7;
    return (Rectangle){70 + col * 92, 110 + row * 92, 74, 74};
}

void screen_talents_update(Game *g, Vector2 mouse)
{
    Character *c = &g->campaign.player;
    g->hovered_item = -1;

    for (int32_t node = 0; node < SONNY_TALENT_COUNT; node++) {
        if (!hit(talent_rect(node), mouse))
            continue;
        g->hovered_item = node;
        if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            break;

        TalentError err = character_learn(c, node);
        switch (err) {
        case TALENT_OK:
            audio_play("Click2putdown");
            game_notice(g, "Learned rank %d.", c->rank[node]);
            /* A newly learned active talent goes on the first free slot. */
            if (!SONNY_TALENTS[node].passive) {
                for (int32_t i = 0; i < SONNY_MOVE_SLOTS; i++) {
                    if (c->move_matrix[i] == 0) {
                        c->move_matrix[i] = c->skill_adder[node];
                        break;
                    }
                }
            }
            break;
        case TALENT_NO_POINTS:
            game_notice(g, "%s", lang_text("MENU", 20));
            break;
        case TALENT_MAX_RANK:
            game_notice(g, "Already at its highest rank.");
            break;
        case TALENT_LEVEL_TOO_LOW:
            game_notice(g, "Needs level %d.",
                        character_talent_next_level(c, node));
            break;
        default:
            game_notice(g, "Its prerequisite is not learned yet.");
            break;
        }
        break;
    }

    if (draw_button((Rectangle){STAGE_W - 120, STAGE_H - 44, 100, 28}, "Back",
                    mouse, 1))
        g->screen = SCREEN_ZONE;
}

void screen_talents_draw(Game *g, Vector2 mouse)
{
    const Character *c = &g->campaign.player;

    ClearBackground((Color){18, 20, 26, 255});
    DrawText(lang_text("SYSTEM", 19), 22, 16, 20, (Color){210, 215, 225, 255});
    DrawText(TextFormat("%d points to spend",
                        character_unspent_skill_points(c)),
             22, 44, 10, (Color){140, 210, 140, 255});

    for (int32_t node = 0; node < SONNY_TALENT_COUNT; node++) {
        const TalentDef *t = &SONNY_TALENTS[node];
        Rectangle r = talent_rect(node);
        int32_t rank = c->rank[node];
        int learnable = character_can_learn(c, node) == TALENT_OK;

        DrawRectangleRec(r, (Color){34, 38, 48, 255});
        /* The ability's own icon, at the rank currently held. */
        const AbilityDef *a = ability_by_id(t->ability_id
                                            + (rank > 0 ? rank - 1 : 0));
        Rectangle inner = {r.x + 3, r.y + 3, r.width - 6, r.height - 20};
        Color tint = rank > 0 ? WHITE : (Color){130, 130, 140, 255};
        if (a && !asset_draw_fit(a->icon, 1, inner, tint))
            DrawText(a->icon, (int)r.x + 5, (int)r.y + 8, 10, tint);

        DrawRectangleLinesEx(r, hit(r, mouse) ? 2.0f : 1.0f,
                             learnable ? (Color){140, 210, 140, 255}
                             : rank > 0 ? (Color){225, 200, 120, 255}
                                        : (Color){80, 84, 96, 255});
        DrawText(TextFormat("%d/%d", rank, t->max_rank), (int)r.x + 5,
                 (int)(r.y + r.height - 14), 10,
                 rank > 0 ? RAYWHITE : (Color){140, 145, 155, 255});
        if (t->passive)
            DrawText("P", (int)(r.x + r.width - 12), (int)r.y + 4, 10,
                     (Color){150, 190, 240, 255});
    }

    /* What the hovered talent does, in the game's own words. */
    if (g->hovered_item >= 0 && g->hovered_item < SONNY_TALENT_COUNT) {
        const TalentDef *t = &SONNY_TALENTS[g->hovered_item];
        int32_t rank = c->rank[g->hovered_item];
        Rectangle box = {22, STAGE_H - 130, STAGE_W - 160, 78};
        draw_panel(box, NULL);
        const AbilityDef *a = ability_by_id(t->ability_id
                                            + (rank > 0 ? rank - 1 : 0));
        if (a) {
            DrawText((a->name && a->name[0]) ? a->name : a->icon,
                     (int)box.x + 8, (int)box.y + 8, 10,
                     (Color){235, 200, 90, 255});
            DrawText(a->tooltip, (int)box.x + 8, (int)box.y + 26, 10,
                     (Color){200, 205, 215, 255});
        }
        DrawText(TextFormat("Next rank at level %d",
                            character_talent_next_level(c, g->hovered_item)),
                 (int)box.x + 8, (int)box.y + 56, 10,
                 (Color){150, 160, 175, 255});
    }
    draw_button((Rectangle){STAGE_W - 120, STAGE_H - 44, 100, 28}, "Back",
                mouse, 1);
}

/* ----------------------------------------------------------- inventory */

static Rectangle slot_rect(int i)
{
    return (Rectangle){70, 110 + i * 44, 300, 38};
}

static Rectangle bag_rect(int i)
{
    return (Rectangle){420, 110 + (i % 8) * 34 + (i / 8) * 0, 320, 30};
}

/* An item can go in a slot when the slot matches and the requirements pass.
   The original's slot numbering starts at 2, so slot index = slot - 2. */
static int32_t item_slot_index(const ItemDef *item)
{
    return item && item->slot >= 2 ? item->slot - 2 : -1;
}

void screen_inventory_update(Game *g, Vector2 mouse)
{
    Campaign *c = &g->campaign;
    g->hovered_item = -1;

    for (int32_t i = 0; i < c->inventory_count && i < 8; i++) {
        Rectangle r = bag_rect(i);
        if (hit(r, mouse))
            g->hovered_item = 100 + i;
        if (!hit(r, mouse) || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            continue;

        const ItemDef *item = item_by_id(c->inventory[i]);
        int32_t slot = item_slot_index(item);
        if (slot < 0 || slot >= SONNY_EQUIP_SLOTS) {
            game_notice(g, "That cannot be equipped.");
            break;
        }
        if (!character_can_equip(&c->player, item)) {
            game_notice(g, "%s%d needed.", lang_text("MENU", 0),
                        item->level_req);
            break;
        }
        /* Swap: what was worn goes back to the bag. */
        int32_t worn = c->player.equip[slot];
        c->player.equip[slot] = item->id;
        c->inventory[i] = worn;
        if (worn == 0) {
            for (int32_t k = i; k < c->inventory_count - 1; k++)
                c->inventory[k] = c->inventory[k + 1];
            c->inventory_count--;
        }
        audio_play("Click2putdown");
        game_notice(g, "Equipped %s.", item->name);
        break;
    }

    for (int i = 0; i < SONNY_EQUIP_SLOTS; i++) {
        Rectangle r = slot_rect(i);
        if (hit(r, mouse))
            g->hovered_item = i;
        if (!hit(r, mouse) || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            continue;
        if (c->player.equip[i] == 0)
            continue;
        if (c->inventory_count >= (int32_t)(sizeof(c->inventory)
                                            / sizeof(c->inventory[0]))) {
            game_notice(g, "The bag is full.");
            break;
        }
        c->inventory[c->inventory_count++] = c->player.equip[i];
        c->player.equip[i] = 0;
        audio_play("Click3pickup");
        break;
    }

    if (draw_button((Rectangle){STAGE_W - 120, STAGE_H - 44, 100, 28}, "Back",
                    mouse, 1))
        g->screen = SCREEN_ZONE;
}

void screen_inventory_draw(Game *g, Vector2 mouse)
{
    const Campaign *c = &g->campaign;

    ClearBackground((Color){18, 20, 26, 255});
    DrawText(lang_text("SYSTEM", 17), 22, 16, 20, (Color){210, 215, 225, 255});
    DrawText("Worn", 70, 92, 10, (Color){150, 160, 175, 255});
    DrawText("Carried", 420, 92, 10, (Color){150, 160, 175, 255});

    for (int i = 0; i < SONNY_EQUIP_SLOTS; i++) {
        Rectangle r = slot_rect(i);
        const ItemDef *item = item_by_id(c->player.equip[i]);
        DrawRectangleRec(r, (Color){30, 33, 41, 255});
        DrawRectangleLinesEx(r, hit(r, mouse) ? 2.0f : 1.0f,
                             (Color){80, 84, 96, 255});
        /* ITEMSS names the slot kinds, in the original's own order. */
        DrawText(lang_text("ITEMSS", i), (int)r.x + 8, (int)r.y + 12, 10,
                 (Color){140, 145, 155, 255});
        if (item && item->id != 0)
            DrawText(item->name, (int)r.x + 110, (int)r.y + 12, 10, RAYWHITE);
    }

    for (int32_t i = 0; i < c->inventory_count && i < 8; i++) {
        Rectangle r = bag_rect(i);
        const ItemDef *item = item_by_id(c->inventory[i]);
        DrawRectangleRec(r, (Color){30, 33, 41, 255});
        DrawRectangleLinesEx(r, hit(r, mouse) ? 2.0f : 1.0f,
                             (Color){80, 84, 96, 255});
        if (item)
            DrawText(item->name, (int)r.x + 8, (int)r.y + 9, 10, RAYWHITE);
    }

    /* The hovered item's own description. */
    const ItemDef *shown = NULL;
    if (g->hovered_item >= 100) {
        int32_t i = g->hovered_item - 100;
        if (i < c->inventory_count)
            shown = item_by_id(c->inventory[i]);
    } else if (g->hovered_item >= 0 && g->hovered_item < SONNY_EQUIP_SLOTS) {
        shown = item_by_id(c->player.equip[g->hovered_item]);
    }
    if (shown && shown->id != 0) {
        Rectangle box = {70, STAGE_H - 140, STAGE_W - 200, 90};
        draw_panel(box, shown->name);
        DrawText(shown->tooltip, (int)box.x + 8, (int)box.y + 26, 10,
                 (Color){200, 205, 215, 255});
        int y = (int)box.y + 44;
        const char *labels[5] = {lang_text("SYSTEM", 0), lang_text("SYSTEM", 1),
                                 lang_text("SYSTEM", 2), lang_text("SYSTEM", 3),
                                 lang_text("SYSTEM", 4)};
        for (int i = 0; i < 5; i++) {
            if (shown->stat[i] == 0)
                continue;
            DrawText(TextFormat("%s +%.0f", labels[i], shown->stat[i]),
                     (int)box.x + 8 + i * 110, y, 10,
                     (Color){140, 210, 140, 255});
        }
    }
    draw_button((Rectangle){STAGE_W - 120, STAGE_H - 44, 100, 28}, "Back",
                mouse, 1);
}

/* ---------------------------------------------------------------- shop */

/* What the store offers: everything the character could wear at this level,
   which is how the original stocks a zone's store from the item table. */
static int32_t shop_stock(const Campaign *c, const ItemDef **out, int32_t max)
{
    int32_t n = 0;
    for (int i = 0; i < SONNY_ITEM_COUNT && n < max; i++) {
        const ItemDef *item = &SONNY_ITEMS[i];
        if (item->slot < 2 || item->price <= 0)
            continue;
        /* A couple of table entries have no name in the language file; the
           original never shows those either. */
        if (!item->name || !item->name[0])
            continue;
        if (item->level_req > c->player.level)
            continue;
        if (item->level_req + 4 < c->player.level)
            continue;   /* long outgrown */
        out[n++] = item;
    }
    return n;
}

static Rectangle stock_rect(int i)
{
    return (Rectangle){70 + (i / 10) * 340, 110 + (i % 10) * 34, 320, 30};
}

void screen_shop_update(Game *g, Vector2 mouse)
{
    Campaign *c = &g->campaign;
    const ItemDef *stock[20];
    int32_t count = shop_stock(c, stock, 20);
    g->hovered_item = -1;

    for (int32_t i = 0; i < count; i++) {
        Rectangle r = stock_rect(i);
        if (hit(r, mouse))
            g->hovered_item = i;
        if (!hit(r, mouse) || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            continue;

        if (c->euros < stock[i]->price) {
            game_notice(g, "Not enough euros.");
            break;
        }
        if (c->inventory_count >= (int32_t)(sizeof(c->inventory)
                                            / sizeof(c->inventory[0]))) {
            game_notice(g, "The bag is full.");
            break;
        }
        c->euros -= stock[i]->price;
        c->inventory[c->inventory_count++] = stock[i]->id;
        audio_play("Click2putdown");
        game_notice(g, "Bought %s.", stock[i]->name);
        break;
    }

    if (draw_button((Rectangle){STAGE_W - 120, STAGE_H - 44, 100, 28}, "Back",
                    mouse, 1))
        g->screen = SCREEN_ZONE;
}

void screen_shop_draw(Game *g, Vector2 mouse)
{
    const Campaign *c = &g->campaign;
    const ItemDef *stock[20];
    int32_t count = shop_stock(c, stock, 20);

    ClearBackground((Color){18, 20, 26, 255});
    DrawText(lang_text("SYSTEM", 15), 22, 16, 20, (Color){210, 215, 225, 255});
    DrawText(TextFormat("Euros %d", c->euros), 22, 44, 10,
             (Color){225, 200, 120, 255});

    for (int32_t i = 0; i < count; i++) {
        Rectangle r = stock_rect(i);
        const ItemDef *item = stock[i];
        int affordable = c->euros >= item->price;
        DrawRectangleRec(r, (Color){30, 33, 41, 255});
        DrawRectangleLinesEx(r, hit(r, mouse) ? 2.0f : 1.0f,
                             (Color){80, 84, 96, 255});
        DrawText(item->name, (int)r.x + 8, (int)r.y + 9, 10,
                 affordable ? RAYWHITE : (Color){140, 110, 110, 255});
        DrawText(TextFormat("%d", item->price), (int)(r.x + r.width - 44),
                 (int)r.y + 9, 10,
                 affordable ? (Color){225, 200, 120, 255}
                            : (Color){140, 110, 110, 255});
    }

    if (g->hovered_item >= 0 && g->hovered_item < count) {
        const ItemDef *item = stock[g->hovered_item];
        Rectangle box = {70, STAGE_H - 130, STAGE_W - 200, 76};
        draw_panel(box, item->name);
        DrawText(item->tooltip, (int)box.x + 8, (int)box.y + 26, 10,
                 (Color){200, 205, 215, 255});
        DrawText(TextFormat("%s%d", lang_text("MENU", 0), item->level_req),
                 (int)box.x + 8, (int)box.y + 48, 10,
                 (Color){150, 160, 175, 255});
    }
    draw_button((Rectangle){STAGE_W - 120, STAGE_H - 44, 100, 28}, "Back",
                mouse, 1);
}

/* ------------------------------------------------------------- victory */

void screen_victory_update(Game *g, Vector2 mouse)
{
    /* Drops are chosen by clicking them, as VICTORY[1] instructs. */
    for (int32_t i = 0; i < g->dropped_count; i++) {
        Rectangle r = {70 + i * 150, 190, 140, 34};
        if (!hit(r, mouse) || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            continue;
        if (g->taken[i])
            continue;
        Campaign *c = &g->campaign;
        if (c->inventory_count >= (int32_t)(sizeof(c->inventory)
                                            / sizeof(c->inventory[0]))) {
            game_notice(g, "The bag is full.");
            break;
        }
        c->inventory[c->inventory_count++] = g->dropped[i];
        g->taken[i] = 1;
        audio_play("Click3pickup");
        break;
    }

    if (draw_button((Rectangle){STAGE_W / 2 - 60, STAGE_H - 90, 120, 30},
                    "Continue", mouse, 1))
        g->screen = SCREEN_ZONE;
}

void screen_victory_draw(Game *g, Vector2 mouse)
{
    ClearBackground((Color){18, 20, 26, 255});
    DrawText(lang_text("VICTORY", 0), 70, 90, 12, (Color){235, 200, 90, 255});
    if (g->dropped_count > 0)
        DrawText(lang_text("VICTORY", 1), 70, 112, 10,
                 (Color){200, 205, 215, 255});

    for (int32_t i = 0; i < g->dropped_count; i++) {
        Rectangle r = {70 + i * 150, 190, 140, 34};
        const ItemDef *item = item_by_id(g->dropped[i]);
        DrawRectangleRec(r, g->taken[i] ? (Color){26, 30, 26, 255}
                                        : (Color){30, 33, 41, 255});
        DrawRectangleLinesEx(r, hit(r, mouse) ? 2.0f : 1.0f,
                             g->taken[i] ? (Color){120, 180, 120, 255}
                                         : (Color){80, 84, 96, 255});
        if (item)
            DrawText(item->name, (int)r.x + 8, (int)r.y + 11, 10,
                     g->taken[i] ? (Color){140, 210, 140, 255} : RAYWHITE);
    }

    DrawText(TextFormat("%s %d", lang_text("VICTORY", 2), g->rewards.euros),
             70, 280, 10, (Color){225, 200, 120, 255});
    DrawText(TextFormat("%s %.1f%%", lang_text("VICTORY", 3),
                        g->rewards.xp_percent),
             70, 300, 10, (Color){150, 200, 255, 255});
    if (g->rewards.leveled)
        DrawText(TextFormat("%s%d!", lang_text("MENU", 0),
                            g->campaign.player.level),
                 70, 322, 12, (Color){140, 210, 140, 255});

    DrawText(lang_text("VICTORY", 4), 70, STAGE_H - 120, 10,
             (Color){170, 175, 185, 255});
    draw_button((Rectangle){STAGE_W / 2 - 60, STAGE_H - 90, 120, 30},
                "Continue", mouse, 1);
}
