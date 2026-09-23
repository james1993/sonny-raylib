/* The stores. */
#include "menu.h"

/* ---------------------------------------------------------------- shop */

/* The store, which the menu clip carries on its "shop" frame: a picture of
   the place and what its keeper says on the left, the character and what they
   are wearing in the middle with the stock under it, and the bag on the
   right. The frame's own script (sprite 1503, frame 16) is what says where
   the text comes from.

   Which store it is comes from the marker that opened it: every zone's scene
   has one, and the shopId its button sets picks both the stock and the line
   the keeper says. */
#define MENU_SHOP "shop"

/* The slot that buys back what is dropped into it. */
#define SHOP_RECYCLER 1395

/* How many slots the store's grid has, and the bag's on this frame. */
#define SHOP_STOCK_SLOTS SONNY_SHOP_SLOTS

static const TextField *shop_field(const char *name)
{
    return text_field_named(MENU_SCREEN, MENU_SHOP, name, 0);
}

/* Where the store's picture goes, and which of its frames this store is. */
static const ClipPart *shop_picture(void)
{
    for (int32_t i = 0; ; i++) {
        const ClipPart *part = clip_part(MENU_SCREEN, MENU_SHOP, i);
        if (!part)
            return NULL;
        if (part->frames > 0)
            return part;
    }
}

static Rectangle shop_stock_rect(int32_t i)
{
    return named_slot(MENU_SHOP, "dropSlot", i);
}

static Rectangle shop_bag_rect(int32_t i)
{
    return named_slot(MENU_SHOP, "itemSlot", i);
}

static Rectangle shop_equip_rect(int32_t i)
{
    return named_slot(MENU_SHOP, "playerSlot", i);
}

void screen_shop_draw(Game *g, Vector2 mouse)
{
    const Campaign *c = &g->campaign;
    const ShopDef *shop = shop_for_button(g->shop_button);

    ClearBackground(BLACK);
    draw_hub_panel(g, mouse);
    draw_clip_parts(MENU_SCREEN, MENU_SHOP, NO_OFFSET, NULL, NULL, WHITE);
    /* The two buttons in the purse strip -- the store's own euro sign and the
       recycler -- are art the frame keeps inside the buttons themselves. */
    int32_t row0, rows = button_rows(MENU_SHOP, &row0);
    for (int32_t i = row0; i < row0 + rows; i++)
        if (strcmp(SONNY_BUTTONS[i].screen, MENU_SHOP) == 0)
            draw_button_art(&SONNY_BUTTONS[i], WHITE);

    /* The picture of the place. Its clip has a frame per store and the
       screen points it at shopId + 1. */
    const ClipPart *picture = shop_picture();
    if (picture && shop) {
        Art art;
        if (asset_art(TextFormat("#%d@%d", picture->character, shop->id + 1),
                      1, &art))
            draw_art_placed(&art, picture->x, picture->y,
                            picture->scale_x, picture->scale_y,
                            picture->origin_x, picture->origin_y, WHITE);
    }

    draw_field_wrapped(shop_field("@895"), NO_OFFSET,
                       shop ? lang_text("SHOP", shop->id) : "");
    draw_field(shop_field("@896"), NO_OFFSET, lang_text("MENU", 6));
    draw_field(shop_field("@1059"), NO_OFFSET, lang_text("MENU", 14));
    draw_field(shop_field("@1052"), NO_OFFSET, TextFormat("%d", c->euros));
    draw_field(shop_field("@1053"), NO_OFFSET, EURO);

    /* The character, stood where the frame stands them, in what they wear. */
    const MenuSlot *doll = menu_slot(MENU_SHOP, "chest");
    if (doll) {
        DollSpec spec;
        memset(&spec, 0, sizeof(spec));
        spec.gender = "M";
        spec.skin = "ONE";
        spec.hair = "ONE";
        char looks[SONNY_EQUIP_SLOTS][24];
        for (int i = 0; i < SONNY_EQUIP_SLOTS; i++) {
            const ItemDef *item = item_by_id(c->player.equip[i]);
            snprintf(looks[i], sizeof(looks[i]), "%s",
                     (item && item->looks) ? item->looks : "");
            spec.looks[i] = looks[i];
        }
        doll_draw(&spec, doll_animation_frame("stand", g->bv.anim_tick / 3, 1),
                  (Vector2){doll->x - 6.0f, doll->y + 10.0f}, 1.3f, 0, WHITE);
    }

    draw_party_row(g, MENU_SHOP, mouse);

    for (int i = 0; i < SONNY_EQUIP_SLOTS; i++) {
        Rectangle r = shop_equip_rect(i);
        draw_slot_art(MENU_SHOP, "playerSlot", i);
        draw_item_icon(item_by_id(c->player.equip[i]), r, WHITE);
        if (hit(r, mouse))
            DrawRectangleLinesEx(r, 1.0f, (Color){235, 200, 90, 255});
    }

    /* The stock. A slot the store leaves empty is hidden outright, which is
       what the frame's script does with a zero. */
    for (int32_t i = 0; i < SHOP_STOCK_SLOTS; i++) {
        int32_t id = shop ? shop->item[i] : 0;
        if (id == 0)
            continue;
        Rectangle r = shop_stock_rect(i);
        draw_slot_art(MENU_SHOP, "dropSlot", i);
        draw_item_icon(item_by_id(id), r, WHITE);
        if (hit(r, mouse))
            DrawRectangleLinesEx(r, 1.0f, (Color){235, 200, 90, 255});
    }

    for (int32_t i = 0; i < MENU_BAG_SLOTS; i++) {
        Rectangle r = shop_bag_rect(i);
        draw_slot_art(MENU_SHOP, "itemSlot", i);
        if (c->inventory[i])
            draw_item_icon(item_by_id(c->inventory[i]), r, WHITE);
        if (hit(r, mouse))
            DrawRectangleLinesEx(r, 1.0f, (Color){235, 200, 90, 255});
    }

    /* What the pointer is on, in the game's own words. The store has all
       three kinds of square on it at once -- what it sells, what the player
       is carrying, and what they are wearing -- so each is read from its own
       range. */
    if (g->hovered_item >= HOVER_EQUIP) {
        /* The store dresses the player rather than an ally -- its doll is
           drawn from the same rows. */
        tooltip_equip_row(g, &c->player, g->hovered_item - HOVER_EQUIP);
    } else if (g->hovered_item >= HOVER_BAG) {
        int32_t i = g->hovered_item - HOVER_BAG;
        game_tooltip_item(g, item_by_id(c->inventory[i]), 0);
    } else if (g->hovered_item >= 0 && shop
               && g->hovered_item < SHOP_STOCK_SLOTS) {
        /* The store's stock names its price first; nothing else does. */
        const ItemDef *stock = item_by_id(shop->item[g->hovered_item]);
        if (stock && stock->id != 0)
            game_tooltip_item(g, stock, stock->price);
    }
}

void screen_shop_update(Game *g, Vector2 mouse)
{
    hud_buttons(g, mouse);
    Campaign *c = &g->campaign;
    const ShopDef *shop = shop_for_button(g->shop_button);
    Character scratch;
    const Character *who = menu_character(g, &scratch);
    g->hovered_item = -1;

    /* Buying: a stock slot hands the item over for its price, and the store
       never runs out of it. */
    for (int32_t i = 0; shop && i < SHOP_STOCK_SLOTS; i++) {
        const ItemDef *item = item_by_id(shop->item[i]);
        if (!item || item->id == 0)
            continue;
        Rectangle r = shop_stock_rect(i);
        if (!hit(r, mouse))
            continue;
        g->hovered_item = i;
        if (!ui_clicked())
            break;
        if (c->euros < item->price) {
            game_notice(g, "%s", lang_text("MENU", 20));
            break;
        }
        int32_t free_slot = bag_free_slot(c);
        if (free_slot < 0)
            break;
        c->euros -= item->price;
        c->inventory[free_slot] = item->id;
        audio_play("Click2putdown");
        break;
    }

    for (int32_t i = 0; i < MENU_BAG_SLOTS; i++) {
        Rectangle r = shop_bag_rect(i);
        if (!hit(r, mouse))
            continue;
        g->hovered_item = HOVER_BAG + i;
        if (ui_clicked())
            swap_bag(g, i);
        break;
    }

    for (int i = 0; i < SONNY_EQUIP_SLOTS; i++) {
        Rectangle r = shop_equip_rect(i);
        if (!hit(r, mouse))
            continue;
        g->hovered_item = HOVER_EQUIP + i;
        if (ui_clicked() && item_fits(who, g->carried_item, i))
            swap_equipped(g, i);
        break;
    }

    /* The recycler: whatever the pointer is carrying goes in for a quarter of
       what it is worth, rounded up. It is the one place in the game that says
       what an item is worth to sell -- an item's own description never does,
       and the store's stock says only what it costs to buy -- so it says the
       figure while the pointer is over it holding something:

           t = MENU[4] + " EUR" + Math.ceil(KRINITEM[mouseItem][5] / 4)

       and the standing invitation when the hand is empty. */
    const StageButton *bin = stage_button(MENU_SHOP, SHOP_RECYCLER, 0);
    if (bin && CheckCollisionPointRec(mouse, BOX_OF(bin))) {
        const ItemDef *item = item_by_id(g->carried_item);
        if (item && item->id != 0)
            game_tooltip(g, lang_text("MENU", 3),
                         TextFormat("%s €%d", lang_text("MENU", 4),
                                    (item->price + 3) / 4));
        else
            game_tooltip(g, lang_text("MENU", 3), lang_text("MENU", 5));
        if (ui_clicked() && g->carried_item != 0) {
            if (item)
                c->euros += (item->price + 3) / 4;
            g->carried_item = 0;
            audio_play("Click2putdown");
        }
    }

    if (menu_close_pressed(mouse))
        g->screen = SCREEN_ZONE;
}
