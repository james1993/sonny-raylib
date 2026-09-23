/* The character screen: gear, attributes, element bands and the bag. */
#include "menu.h"

/* ----------------------------------------------------------- inventory */

















/* An item's icon is a frame label on the icon sprite, named for the item. */


/* The AI Mode selector under an ally's gear: sprite 1355, a frame per mode
   with that mode's bar lit, over three invisible buttons -- 1350 for the
   first mode, 1352 for the second and 1351 for the third, left to right. Its
   clip hides itself while the sheet is turned to the player and says
   "AI Mode: " and the mode's name out of MENU[39..41]. */
#define AI_MODE_PART  "@1033"
#define AI_MODE_ART   "#1355@%d"
#define AI_MODE_FIELD "worder"
#define AI_MODE_WORDS 39
static const int32_t AI_MODE_BUTTONS[SONNY_AI_MODES] = {1350, 1352, 1351};

void screen_inventory_update(Game *g, Vector2 mouse)
{
    hud_buttons(g, mouse);
    Character scratch;
    const Character *who = menu_character(g, &scratch);
    g->hovered_item = -1;

    for (int32_t i = 0; i < MENU_BAG_SLOTS; i++) {
        Rectangle r = bag_rect(i);
        if (!hit(r, mouse))
            continue;
        g->hovered_item = HOVER_BAG + i;
        if (ui_clicked())
            swap_bag(g, i);
        break;
    }

    for (int i = 0; i < SONNY_EQUIP_SLOTS; i++) {
        Rectangle r = slot_rect(i);
        if (!hit(r, mouse))
            continue;
        g->hovered_item = HOVER_EQUIP + i;
        /* A row only takes what belongs in it, and the row keeps what it has
           if the carried item does not fit. */
        if (ui_clicked() && item_fits(who, g->carried_item, i))
            swap_equipped(g, i);
        break;
    }

    /* The AI Mode buttons, which only an ally's sheet shows. */
    for (int32_t m = 0; g->menu_member > 0 && m < SONNY_AI_MODES; m++) {
        if (screen_button_pressed(MENU_INVENTORY, AI_MODE_BUTTONS[m], mouse)) {
            campaign_set_ai_mode(&g->campaign, g->menu_member, m + 1);
            break;
        }
    }

    if (menu_close_pressed(mouse))
        g->screen = SCREEN_ZONE;
}

/* One of the two bands of element bars.
 *
 * Each band is four bars per element, stacked at the same place and all
 * tinted that element's colour. Three of them are driven; the fourth, "ybar",
 * is never resized and is the dark trough the others stand in. What each one
 * means comes out of the frame's own script, where the heights are set from
 * the character's piercing or defense against the baseline every character of
 * that level starts at:
 *
 *     yut_per2[e] = PerSets[e] + ceil(25 + 5 * level);
 *     bar._height  = saveH * ((1 - (25 - per/base*25)/100 - 0.78125)/0.78125);
 *     gbar._height = (saveH/100) * (15 * (per/base));
 *     xbar._height = saveH * ((1 - 25*(1 - 25*(per/base)/200)/100
 *                              - 0.78125)/0.78125);
 *
 * which are the three numbers a hit turns on: what the damage comes to when
 * it pierces, the chance of piercing, and what it comes to when it does not.
 * Defense has its own three. They are ratios against the baseline, so a
 * character with no gear reads the same at every level -- the port had them
 * scaled against a flat two hundred instead, which climbed with the level
 * whether or not anything had actually changed.
 *
 * The tints are the placements' own colour transforms, except that the
 * script's setRGB lands on different objects: on the clip itself for gbar,
 * which replaces its transform, and on the clip's `inner` for the other
 * three, which leaves theirs in place. */
#define BAR_TROUGH_MUL (26.0f / 256.0f)    /* ybar */
#define BAR_PIERCED_MUL (77.0f / 256.0f)   /* bar */
#define BAR_PLAIN_ADD  100                 /* xbar */

static float bar_fraction(float value)
{
    if (!(value > 0.0f))        /* also catches a NaN from a zero baseline */
        return 0.0f;
    return value > 1.0f ? 1.0f : value;
}

static void draw_element_bar(const SlotPiece *piece, Color tint, float fraction)
{
    Art art;
    if (fraction <= 0.0f || !piece
        || !asset_art(TextFormat("#%d", piece->character), 1, &art))
        return;
    Rectangle box = placed_art(&art, piece->x, piece->y, piece->scale_x,
                               piece->scale_y, piece->origin_x,
                               piece->origin_y);
    /* _height is set on a clip whose registration point is its foot, so a
       bar grows up from the bottom of the trough. */
    float full = box.height;
    box.height = full * fraction;
    /* A bar the formulas leave a fraction of a pixel tall is nothing on the
       screen in the original, where a defense band at the baseline comes to
       fifteen thousandths of a pixel. Rounding it up to a lit row put a line
       under a band that should be empty. */
    if (box.height < 0.5f)
        return;
    box.y += full - box.height;
    DrawTexturePro(*art.texture, art.source, box, (Vector2){0, 0}, 0.0f, tint);
}

static void draw_element_bars(const char *menu, const char *slot,
                              const double *values, double baseline,
                              int piercing)
{
    for (int i = 0; i < SONNY_ELEMENT_DEF_COUNT; i++) {
        /* The band's pieces come in stacking order, eight of each: the
           trough, then the three the screen drives. */
        const SlotPiece *trough = slot_piece(menu, slot, i);
        const SlotPiece *pierced = slot_piece(menu, slot,
                                              SONNY_ELEMENT_DEF_COUNT + i);
        const SlotPiece *chance = slot_piece(menu, slot,
                                             2 * SONNY_ELEMENT_DEF_COUNT + i);
        const SlotPiece *plain = slot_piece(menu, slot,
                                            3 * SONNY_ELEMENT_DEF_COUNT + i);
        if (!trough || !pierced || !chance || !plain)
            return;

        uint32_t rgb = SONNY_ELEMENT_DEFS[i].colour;
        Color colour = {(unsigned char)(rgb >> 16), (unsigned char)(rgb >> 8),
                        (unsigned char)rgb, 255};
        double v = values[i];
        double ratio = baseline > 0 ? v / baseline : 0;

        double fb, fg, fx;
        if (piercing) {
            fb = (1 - (25 - ratio * 25) / 100 - 0.78125) / 0.78125;
            fg = 15 * ratio / 100;
            fx = (1 - 25 * (1 - 25 * ratio / 200) / 100 - 0.78125) / 0.78125;
        } else {
            fb = (v - baseline) / 100;
            fg = (ratio * 25 * 0.875 - 21.800000000000008) / 100;
            fx = v > 0 ? 0.15 - 0.15 / ratio : 0;
        }

        draw_element_bar(trough, (Color){
            (unsigned char)(colour.r * BAR_TROUGH_MUL),
            (unsigned char)(colour.g * BAR_TROUGH_MUL),
            (unsigned char)(colour.b * BAR_TROUGH_MUL), 255}, 1.0f);
        draw_element_bar(pierced, (Color){
            (unsigned char)(colour.r * BAR_PIERCED_MUL),
            (unsigned char)(colour.g * BAR_PIERCED_MUL),
            (unsigned char)(colour.b * BAR_PIERCED_MUL), 255},
            bar_fraction((float)fb));
        draw_element_bar(chance, colour, bar_fraction((float)fg));
        /* The one the original does not clamp, so neither does this beyond
           keeping it off the screen when it comes out negative. */
        draw_element_bar(plain, (Color){
            (unsigned char)(colour.r + BAR_PLAIN_ADD > 255 ? 255
                            : colour.r + BAR_PLAIN_ADD),
            (unsigned char)(colour.g + BAR_PLAIN_ADD > 255 ? 255
                            : colour.g + BAR_PLAIN_ADD),
            (unsigned char)(colour.b + BAR_PLAIN_ADD > 255 ? 255
                            : colour.b + BAR_PLAIN_ADD), 255},
            (float)(fx > 0 ? fx : 0));
    }
}

/* One of the inventory frame's fields, by the name the clip gives it. */
/* The inventory's own clip, drawn piece by piece rather than through
   draw_clip_parts, because one thing on it is the screen's to drive: the
   experience bar's fill, which the original sets the width of every frame --

       _width = _root.Krin.ExpSets[MenuPlayerSelect] / 100 * 95.1;

   The fill is not one of the frame's graphics at all. It is a named slot,
   "bar", which is why draw_clip_parts never had it and the bar came out as
   an empty recess. So the frame's pieces are walked in the order the table
   keeps them -- which is the order the clip stacks them, panels over the
   backdrop and the bar's track over its recess -- and the slot is dropped in
   just under the track, which is where its depth puts it.

   Choosing the next piece by depth instead does not work: a piece nested
   inside a container carries that container's inner depth, so the numbers
   repeat and are only meaningful beside their siblings. Ordering by them
   dropped the panels and left the screen as bare backdrop. */
#define INVENTORY_XP_TRACK "@1031"

/* The selector's lit bars. The export carries the frame's own text field
   baked in -- the design-time "Agressive" -- so only the part of the picture
   above the field is drawn, and the words go on as text. */
static void draw_ai_mode(const ClipPart *part, int32_t mode)
{
    Art art;
    if (!asset_art(TextFormat(AI_MODE_ART, (int)mode), 1, &art))
        return;
    Rectangle box = placed_art(&art, part->x, part->y, part->scale_x,
                               part->scale_y, part->origin_x, part->origin_y);
    const TextField *words = text_field_var(MENU_SCREEN, AI_MODE_FIELD);
    float keep = words ? words->y - box.y : box.height;
    if (keep <= 0 || keep > box.height)
        keep = box.height;
    Rectangle src = art.source;
    src.height *= keep / box.height;
    box.height = keep;
    DrawTexturePro(*art.texture, src, box, (Vector2){0, 0}, 0.0f, WHITE);
}

static void draw_inventory_clip(const Game *g, const Character *who)
{
    const MenuSlot *bar = menu_slot(MENU_INVENTORY, XP_BAR_SLOT);
    float fraction = who->xp / 100.0f;
    if (fraction < 0.0f)
        fraction = 0.0f;
    if (fraction > 1.0f)
        fraction = 1.0f;

    int32_t row0, rows = clip_part_rows(MENU_SCREEN, &row0);
    for (int32_t i = row0; i < row0 + rows; i++) {
        const ClipPart *part = &SONNY_CLIP_PARTS[i];
        if (strcmp(part->screen, MENU_SCREEN) != 0
            || strcmp(part->owner, MENU_INVENTORY) != 0 || part->width <= 0)
            continue;

        /* The fill goes under the track, which is the piece above it. */
        if (bar && fraction > 0.0f
            && strcmp(part->name, INVENTORY_XP_TRACK) == 0) {
            Art fill;
            if (asset_art(XP_BAR_PLAIN, 1, &fill)) {
                Rectangle box = placed_art(&fill, bar->x, bar->y, bar->scale,
                                           bar->scale, bar->origin_x,
                                           bar->origin_y);
                /* _width scales the clip from its registration point, which
                   is its left edge, so the whole picture goes into a narrower
                   box rather than being cut off at the fill's end. */
                box.width = XP_BAR_WIDTH * fraction * bar->scale;
                DrawTexturePro(*fill.texture, fill.source, box,
                               (Vector2){0, 0}, 0.0f, WHITE);
            }
        }

        if (strcmp(part->name, AI_MODE_PART) == 0) {
            if (g->menu_member > 0)
                draw_ai_mode(part,
                             g->campaign.ally_ai_mode[g->menu_member]);
            continue;
        }

        Art art;
        if (!asset_art(TextFormat("#%d", part->character), 1, &art))
            continue;
        draw_art_placed(&art, part->x, part->y, part->scale_x, part->scale_y,
                        part->origin_x, part->origin_y, WHITE);
    }
}

static const TextField *inv_field(const char *name)
{
    return text_field_named(MENU_SCREEN, MENU_INVENTORY, name, 0);
}

/* The character sheet, laid out from the menu clip's "inventory" frame: who
   the character is and what they are wearing on the left, their stats and
   their piercing and defense in the middle, and the bag on the right. */



void screen_inventory_draw(Game *g, Vector2 mouse)
{
    const Campaign *c = &g->campaign;
    Character scratch;
    const Character *who = menu_character(g, &scratch);

    ClearBackground(BLACK);
    /* The hub's own furniture stays behind the menu, as it does in the
       original: the row of buttons and the zone's progress are still there. */
    draw_hub_panel(g, mouse);
    draw_inventory_clip(g, who);
    if (g->menu_member > 0) {
        int32_t mode = g->campaign.ally_ai_mode[g->menu_member];
        if (mode >= 1 && mode <= SONNY_AI_MODES)
            draw_field(text_field_var(MENU_SCREEN, AI_MODE_FIELD), NO_OFFSET,
                       TextFormat("AI Mode: %s",
                                  lang_text("MENU",
                                            AI_MODE_WORDS + mode - 1)));
    }

    draw_field(inv_field("@881"), NO_OFFSET,
               g->menu_member > 0 ? SONNY_PARTY[g->menu_member].name
                                  : lang_text("NAVTITLE2", 3));
    draw_field(inv_field("@882"), NO_OFFSET,
               TextFormat("%s%d %s", lang_text("MENU", 0), who->level,
                          /* Krin.ClassStats[0] is Krin.Class + 1, so the
                             name the class menu offered is one back. */
                          lang_text("CLASS", who->class_template
                                    ? who->class_template->id - 1 : 0)));

    /* The five stats. The frame gives each its own label and value field, in
       its own colour, and the names come from the game's text table. */
    static const char *const STAT_LABEL[5] = {"@883", "@884", "@885", "@886",
                                              "@887"};
    static const char *const STAT_VALUE[5] = {"@888", "@889", "@890", "@891",
                                              "@892"};
    DerivedStats stats = character_derive(who);
    const double shown[5] = {stats.life, stats.strength, stats.magic,
                             stats.speed, stats.focus};
    for (int i = 0; i < 5; i++) {
        draw_field(inv_field(STAT_LABEL[i]), NO_OFFSET,
                   TextFormat("%s:", lang_text("SYSTEM", i)));
        draw_field(inv_field(STAT_VALUE[i]), NO_OFFSET,
                   TextFormat("%d", (int32_t)shown[i]));
    }
    draw_field(inv_field("@893"), NO_OFFSET, lang_text("SYSTEM", 5));
    draw_field(inv_field("@894"), NO_OFFSET, lang_text("SYSTEM", 6));
    double baseline = ceil(25 + 5 * (double)who->level);
    draw_element_bars(MENU_INVENTORY, "perBarShow", stats.per, baseline, 1);
    draw_element_bars(MENU_INVENTORY, "defBarShow", stats.def, baseline, 0);

    draw_field(inv_field("@1027"), NO_OFFSET, lang_text("MENU", 17));
    draw_field(inv_field("@1032"), NO_OFFSET,
               TextFormat("%d%%", (int32_t)who->xp));
    draw_field(inv_field("@1059"), NO_OFFSET, lang_text("MENU", 14));
    draw_field(inv_field("@1052"), NO_OFFSET, TextFormat("%d", c->euros));
    draw_field(inv_field("@1053"), NO_OFFSET, EURO);

    /* The doll. The menu attaches each part at its own place rather than
       through the model sprite, so the model's own matrices do not carry
       over; it is drawn from the model, stood where the frame stands it. */
    const MenuSlot *doll = menu_slot(MENU_INVENTORY, "chest");
    if (doll) {
        DollSpec spec;
        memset(&spec, 0, sizeof(spec));
        spec.gender = "M";
        spec.skin = "ONE";
        spec.hair = "ONE";
        char looks[SONNY_EQUIP_SLOTS][24];
        for (int i = 0; i < SONNY_EQUIP_SLOTS; i++) {
            const ItemDef *item = item_by_id(who->equip[i]);
            snprintf(looks[i], sizeof(looks[i]), "%s",
                     (item && item->looks) ? item->looks : "");
            spec.looks[i] = looks[i];
        }
        doll_draw(&spec, doll_animation_frame("stand", g->bv.anim_tick / 3, 1),
                  (Vector2){doll->x - 6.0f, doll->y + 10.0f}, 1.3f, 0, WHITE);
    }

    for (int i = 0; i < SONNY_EQUIP_SLOTS; i++) {
        Rectangle r = slot_rect(i);
        draw_slot_art(MENU_INVENTORY, "playerSlot", i);
        draw_item_icon(item_by_id(who->equip[i]), r, WHITE);
        if (hit(r, mouse))
            DrawRectangleLinesEx(r, 1.0f, (Color){235, 200, 90, 255});
    }

    draw_party_row(g, MENU_INVENTORY, mouse);

    for (int32_t i = 0; i < MENU_BAG_SLOTS; i++) {
        Rectangle r = bag_rect(i);
        draw_slot_art(MENU_INVENTORY, "itemSlot", i);
        if (c->inventory[i])
            draw_item_icon(item_by_id(c->inventory[i]), r, WHITE);
        if (hit(r, mouse))
            DrawRectangleLinesEx(r, 1.0f, (Color){235, 200, 90, 255});
    }

    /* What the pointer is on, in the game's own words. */
    if (g->hovered_item >= HOVER_EQUIP) {
        /* A row is read off whoever the menu is showing, which is not always
           the player: the party row along the bottom swaps the doll and its
           slots for an ally's, and reading the player's rows here put one
           character's words over another's gear. */
        tooltip_equip_row(g, who, g->hovered_item - HOVER_EQUIP);
    } else if (g->hovered_item >= HOVER_BAG) {
        /* An empty square is item zero, whose own words are "This slot is
           empty." -- the original says that rather than nothing. */
        int32_t i = g->hovered_item - HOVER_BAG;
        game_tooltip_item(g, item_by_id(c->inventory[i]), 0);
    }
}
