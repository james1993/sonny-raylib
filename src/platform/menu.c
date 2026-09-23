/* The pieces the menu screens share; see menu.h. */
#include "menu.h"

/* The row of six along the bottom of the character screen: everyone the story
   has handed over, the ones not in the fighting line dimmed, and a frame
   round whoever is being looked at. The portrait clip is the same one the
   speech box uses, pointed at a frame by number. */
#define AVATAR_DIM   "#1314"
#define AVATAR_FRAME "#1317"

/* One of the hub's fields, by the name the frame gives it. */
const TextField *hub_field(const char *name)
{
    return chrome_field(HUB_SCREEN, name);
}

int menu_close_pressed(Vector2 mouse)
{
    return screen_button_pressed(HUB_SCREEN, MENU_BUTTON_CLOSE, mouse);
}

/* One of the victory frame's fields, by the name the clip gives it. */
const TextField *win_field(const char *name)
{
    return text_field_named(MENU_SCREEN, MENU_WIN, name, 0);
}


/* Whoever the sheet is turned to: the player, or one of the party built from
   the table. */
const Character *menu_character(Game *g, Character *scratch)
{
    if (g->menu_member <= 0)
        return &g->campaign.player;
    campaign_ally(&g->campaign, g->menu_member, scratch);
    return scratch;
}

/* Whether this item may go in that equipment row. The original checks three
   things: the row takes that kind of item -- a row's kind is its own index
   plus two -- the item is for this class or for any, and the character is
   high enough level for it. Carrying nothing always passes, which is how a
   row is emptied. */
int item_fits(const Character *c, int32_t item_id, int32_t row)
{
    if (item_id == 0)
        return 1;
    const ItemDef *item = item_by_id(item_id);
    if (!item || item->slot != row + 2)
        return 0;
    int32_t class_id = c->class_template ? c->class_template->id : 0;
    if (item->class_req != 0 && item->class_req != class_id)
        return 0;
    return item->level_req <= c->level;
}

/* A slot the pointer pressed: what it holds and what the pointer is carrying
   change places, which is all any of these slots ever does. */
void swap_carried(Game *g, int32_t *slot)
{
    audio_play(g->carried_item == 0 ? "Click3pickup" : "Click2putdown");
    int32_t held = *slot;
    *slot = g->carried_item;
    g->carried_item = held;
}

/* The same, for an equipment row of whoever the sheet is turned to: what
   comes off stops counting and what goes on starts, which is the only way
   anything ever enters the running total a character carries. */
void swap_equipped(Game *g, int32_t row)
{
    Campaign *c = &g->campaign;
    int32_t member = g->menu_member;
    int32_t *worn = (member <= 0) ? &c->player.equip[row]
                                  : &c->ally_equip[member][row];
    double *sets = (member <= 0) ? c->player.stat_sets
                                 : c->ally_stat_sets[member];
    const ItemDef *off = item_by_id(*worn);
    swap_carried(g, worn);
    const ItemDef *on = item_by_id(*worn);
    for (int32_t i = 0; i < SONNY_STATS; i++) {
        if (off)
            sets[i] -= off->stat[i];
        if (on)
            sets[i] += on->stat[i];
    }
    /* The same handler moves PerSetsN and DefSetsN for whoever the sheet is
       turned to, the party as much as the player. */
    double *per = (member <= 0) ? c->player.per_sets : c->ally_per_sets[member];
    double *def = (member <= 0) ? c->player.def_sets : c->ally_def_sets[member];
    for (int32_t e = 0; e < SONNY_ELEMENTS; e++) {
        if (off) {
            per[e] -= off->per[e];
            def[e] -= off->def[e];
        }
        if (on) {
            per[e] += on->per[e];
            def[e] += on->def[e];
        }
    }
}

/* A square of the bag. The original's handler is an unconditional swap of
   whatever is in the square with what the pointer is carrying --

       itemHolder = Krin.itemArray[id];
       Krin.itemArray[id] = Krin.mouseItem;
       Krin.mouseItem = itemHolder;

   -- so an item goes down in the square it was dropped on rather than in the
   first free one, dropping onto an occupied square picks that one up in
   exchange, and the gap an item leaves stays a gap. */
void swap_bag(Game *g, int32_t index)
{
    if (index < 0 || index >= SONNY_BAG_SLOTS)
        return;
    swap_carried(g, &g->campaign.inventory[index]);
}

/* The lowest square with nothing in it, which is where the original puts an
   item the player did not place: loot taken off the victory screen and a
   purchase off a store's shelf. -1 when the bag is full. */
int32_t bag_free_slot(const Campaign *c)
{
    for (int32_t i = 0; i < SONNY_BAG_SLOTS; i++)
        if (c->inventory[i] == 0)
            return i;
    return -1;
}

/* A frame's numbered slot, or an empty box -- which nothing is under and
   nothing is drawn in -- for a number the frame does not have. */
Rectangle named_slot(const char *menu, const char *prefix, int i)
{
    char name[32];
    snprintf(name, sizeof(name), "%s%d", prefix, i);
    const MenuSlot *slot = menu_slot(menu, name);
    if (!slot)
        return (Rectangle){0, 0, 0, 0};
    return (Rectangle){slot->x - MENU_SLOT_SIZE / 2,
                       slot->y - MENU_SLOT_SIZE / 2,
                       MENU_SLOT_SIZE, MENU_SLOT_SIZE};
}

/* The equipment slots and the bag, as the inventory frame places them. */
Rectangle slot_rect(int i)
{
    return named_slot(MENU_INVENTORY, "playerSlot", i);
}

Rectangle bag_rect(int i)
{
    return named_slot(MENU_INVENTORY, "itemSlot", i);
}

/* What a fight dropped, and the bag again -- the victory frame lays both out
   itself, on a different grid to the inventory's. */
Rectangle drop_rect(int i)
{
    return named_slot(MENU_WIN, "dropSlot", i);
}

Rectangle win_bag_rect(int i)
{
    return named_slot(MENU_WIN, "itemSlot", i);
}

/* A slot's own empty square, drawn where the frame places it. The engine
   fills the slot itself, but the square under it is the original's. */
void draw_slot_art(const char *menu, const char *prefix, int i)
{
    char name[32];
    /* A slot the frame numbers, or -- with i below zero -- one it names
       outright, like the two bands of element bars. */
    if (i < 0)
        snprintf(name, sizeof(name), "%s", prefix);
    else
        snprintf(name, sizeof(name), "%s%d", prefix, i);
    const MenuSlot *slot = menu_slot(menu, name);
    if (!slot || slot->width <= 0)
        return;
    Art art;
    if (!asset_art(TextFormat("#%d", slot->character), 1, &art))
        return;
    draw_art_placed(&art, slot->x, slot->y, slot->scale, slot->scale,
                    slot->origin_x, slot->origin_y, WHITE);
}

/* An item's picture: a frame of the clip every slot shows its contents
   through, labelled with the item's name and drawn at its own size on the
   middle of the slot, as the original attaches it. */
void draw_item_icon(const ItemDef *item, Rectangle r, Color tint)
{
    if (!item || item->id == 0)
        return;
    Vector2 middle = {r.x + r.width / 2, r.y + r.height / 2};
    if (!asset_draw_placed(item->name, 1, middle, 1.0f, tint))
        ui_text(item->name, (int)r.x + 2, (int)(r.y + r.height / 2 - 4), 9,
                tint);
}

void draw_party_row(Game *g, const char *menu, Vector2 mouse)
{
    Campaign *c = &g->campaign;
    for (int32_t i = 0; i < SONNY_PARTY_SIZE; i++) {
        const MenuSlot *slot = menu_slot(menu, TextFormat("fa%d", i));
        if (!slot || !campaign_has_friend(c, i))
            continue;
        Vector2 at = {slot->x, slot->y};
        /* gotoAndStop(friendArray[i]) by number: the first place is the
           player, and nothing moves the clip off him. */
        asset_draw_placed(TextFormat("#1312@%d", i == 0 ? 6 : i), 1, at,
                          slot->scale, WHITE);
        /* The player is always in the fight, so the dim only ever goes over
           the other five -- updateFriends starts its loop at one. */
        int in_line = (i == 0 || c->line[0] == i || c->line[1] == i);
        if (!in_line)
            asset_draw_placed(AVATAR_DIM, 1, at, slot->scale, WHITE);
        if (g->menu_member == i)
            asset_draw_placed(AVATAR_FRAME, 1, at, slot->scale, WHITE);

        Rectangle box = {at.x - slot->width / 2, at.y - slot->height / 2,
                         slot->width, slot->height};
        if (!hit(box, mouse))
            continue;
        game_tooltip(g, SONNY_PARTY[i].name,
                     lang_text("MENU", i != 0 ? 48 : 49));
        if (!ui_clicked())
            continue;
        /* Holding shift moves someone in or out of the fighting line; a
           plain press turns the sheet over to them. */
        if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
            if (i == 0)
                continue;       /* the player is always in it */
            if (c->line[0] == i)
                c->line[0] = 0;
            else if (c->line[1] == i)
                c->line[1] = 0;
            else if (c->line[0] == 0)
                c->line[0] = i;
            else
                c->line[1] = i;
        } else {
            g->menu_member = i;
        }
    }
}

/* What an equipment row says about itself. A row with something in it says
   what the item is, the way any item does; an empty one names what belongs
   in it -- the original reads ITEMSS by the row's own number rather than
   falling back to the item's words. */
void tooltip_equip_row(Game *g, const Character *who, int32_t row)
{
    const ItemDef *worn = item_by_id(who->equip[row]);
    if (worn && worn->slot >= 2) {
        game_tooltip_item(g, worn, 0);
        return;
    }
    game_tooltip(g, worn ? worn->name : "", lang_text("ITEMSS", row));
}
