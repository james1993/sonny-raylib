/* The tally after a fight is won. */
#include "menu.h"

/* ------------------------------------------------------------- victory */

void screen_victory_update(Game *g, Vector2 mouse)
{
    hud_buttons(g, mouse);
    /* The experience bar fills a thirtieth of what the fight paid each frame.
       If it reaches the end the player levels there and then -- the bar goes
       to full, stops, and the level (and with it a skill point and an
       attribute point) is granted. The original discards the overflow. */
    if (g->win_fill > 0) {
        g->win_fill--;
        g->win_xp += g->win_step;
        if (g->win_xp >= 100.0f) {
            g->win_leveled = 1;
            g->win_fill = 0;
            g->win_xp = 100.0f;
            g->rewards.leveled = campaign_apply_xp(&g->campaign,
                                                   100.0 - g->campaign.player.xp);
        } else if (g->win_fill == 0) {
            g->campaign.player.xp = g->win_xp;
        }
    }
    /* The ally rows run the same fill, and a level there is only the level:
       LevelStats[prx]++, the bar to full on its "level" frame, and the
       overflow thrown away. What the bar has reached is written back once the
       fill is over, so leaving before then leaves them where they were. */
    for (int32_t row = 0; row < 2; row++) {
        WinRow *a = &g->win_ally[row];
        if (a->member <= 0 || a->fill <= 0)
            continue;
        a->fill--;
        a->xp += a->step;
        if (a->xp >= 100.0f) {
            a->xp = 100.0f;
            a->fill = 0;
            a->leveled = 1;
            g->campaign.ally_level[a->member]++;
            g->campaign.ally_xp[a->member] = 0;
        } else if (a->fill == 0) {
            g->campaign.ally_xp[a->member] = a->xp;
        }
    }

    /* What is under the pointer says what it is, the same way it does in the
       bag: a drop names itself, and the bag's own squares do too. */
    for (int32_t i = 0; i < g->dropped_count; i++)
        if (hit(drop_rect(i), mouse))
            game_tooltip_item(g, item_by_id(g->dropped[i]), 0);
    for (int32_t i = 0; i < MENU_BAG_SLOTS; i++)
        if (hit(win_bag_rect(i), mouse))
            game_tooltip_item(g, item_by_id(
                g->campaign.inventory[i]), 0);

    /* Drops are chosen by clicking them, as VICTORY[1] instructs. The slot's
       own handler empties the drop as it fills the bag --

           Krin.itemArray[thereIsSlot] = Krin.dropArray[id];
           Krin.dropArray[id] = 0;
           inner.gotoAndStop(ITEMNAME[Krin.dropArray[id]]);

       -- so the square stays where it is and the icon in it goes. With no
       free bag slot the handler does nothing at all, and neither does this. */
    for (int32_t i = 0; i < g->dropped_count; i++) {
        Rectangle r = drop_rect(i);
        if (!hit(r, mouse) || !ui_clicked() || !g->dropped[i])
            continue;
        Campaign *c = &g->campaign;
        int32_t free_slot = bag_free_slot(c);
        if (free_slot < 0)
            break;
        c->inventory[free_slot] = g->dropped[i];
        g->dropped[i] = 0;
        audio_play("Click3pickup");
        break;
    }

    /* "Proceed!" is one of the frame's own fields, so its hit area is that
       field's box. */
    const TextField *proceed = win_field("@23");
    if (!proceed || !ui_clicked()
        || !CheckCollisionPointRec(mouse, BOX_OF(proceed)))
        return;

    /* Where the game goes from here, as the button's own handler decides it:
       the save is written first when autosave is on, then a zone that has
       just been finished goes out to the map -- by way of a comic at the two
       points the story has one -- and anything else goes back to the hub, or
       to the ability screen when the fight was a level. */
    /* Proceed! is what offers the story's next note, if there is one for
       where the player has got to. */
    game_hub_note(g);
    /* Always, where the original asks first. A run lost to a setting is the
       setting's fault. */
    save_write(&g->campaign, save_slot_path(g->campaign.slot));
    if (g->boss_beaten) {
        g->boss_beaten = 0;
        /* A comic at the points the story has one (SONNY_BOSS_COMICS), and
           otherwise out to the map. */
        for (int i = 0; i < SONNY_BOSS_COMIC_COUNT; i++)
            if (SONNY_BOSS_COMICS[i].at == g->campaign.progress_battle) {
                game_play_cutscene(g, cutscene_by_label(
                                          SONNY_BOSS_COMICS[i].comic));
                return;
            }
        g->screen = SCREEN_MAP;
        return;
    }
    if (g->rewards.leveled) {
        g->stat_points_spent = 0;
        screen_talents_open(g);
        g->screen = SCREEN_TALENTS;
        return;
    }
    g->screen = SCREEN_ZONE;
}

/* ------------------------------------------------------- the victory rows */

/* The three experience rows the win frame stacks, in the order its fields
   come: two for the allies standing in the line, and the last -- the top of
   the panel -- hardwired to the player. A row whose member is not there sets
   itself invisible. */
#define WIN_XP_ROWS    3
#define WIN_PLAYER_ROW 2
/* The bar's full width, which the original scales by exp/100. */
#define WIN_BAR_WIDTH  XP_BAR_WIDTH
/* The fill's own two frames: how it looks, and how it looks on a level. */
#define WIN_BAR_PLAIN  XP_BAR_PLAIN
#define WIN_BAR_LEVEL  XP_BAR_LEVEL

/* Which row a piece of the frame belongs to: the three are stacked, so the
   band its y falls in says which. */
static int32_t win_part_row(float y)
{
    if (y < 230.0f)
        return WIN_PLAYER_ROW;
    return y < 320.0f ? 0 : 1;
}

/* Who is in a row, or -1 for the player's own. An ally row takes whoever
   stands in that place in the line -- and only if the story has actually
   handed them over, which is the same test the fight itself makes. The
   original builds this list (friendlySlotsFFTT) while it places the units, so
   a row is empty exactly when nobody fought in it. */
static int32_t win_row_ally(const Game *g, int32_t row)
{
    if (row == WIN_PLAYER_ROW || row < 0 || row >= 2)
        return -1;
    return g->win_ally[row].member;
}

static int win_row_shown(const Game *g, int32_t row)
{
    return row == WIN_PLAYER_ROW || win_row_ally(g, row) > 0;
}

static const char *win_row_name(const Game *g, int32_t row)
{
    if (row == WIN_PLAYER_ROW)
        return g->battle.units[PLAYER_SLOT].name[0]
             ? g->battle.units[PLAYER_SLOT].name : "Sonny";
    int32_t member = win_row_ally(g, row);
    return (member > 0 && member < SONNY_PARTY_COUNT)
         ? SONNY_PARTY[member].name : "";
}

/* Everyone's own level: LevelStats[prx], which for an ally is theirs and
   goes up on this screen when their bar fills. */
static int32_t win_row_level(const Game *g, int32_t row)
{
    int32_t member = win_row_ally(g, row);
    if (member > 0 && member < SONNY_PARTY_SIZE)
        return g->campaign.ally_level[member];
    return g->campaign.player.level;
}

/* How far a row's bar has got, and whether it filled. */
static float win_row_xp(const Game *g, int32_t row)
{
    return row == WIN_PLAYER_ROW ? g->win_xp : g->win_ally[row].xp;
}

static int win_row_leveled(const Game *g, int32_t row)
{
    return row == WIN_PLAYER_ROW ? g->win_leveled : g->win_ally[row].leveled;
}

/* The frame the portrait clip shows for a row. */
static const char *win_row_portrait(const Game *g, int32_t row)
{
    return row == WIN_PLAYER_ROW ? "mainPlayer" : win_row_name(g, row);
}

/* The win frame's own pieces, with the rows nobody is in left out and the
   player's bar cut to how far the fill has got. */
static void draw_win_parts(Game *g)
{
    int32_t row0, rows = clip_part_rows(MENU_SCREEN, &row0);
    for (int32_t i = row0; i < row0 + rows; i++) {
        const ClipPart *part = &SONNY_CLIP_PARTS[i];
        if (strcmp(part->screen, MENU_SCREEN) != 0
            || strcmp(part->owner, MENU_WIN) != 0 || part->width <= 0)
            continue;
        int in_row = strcmp(part->name, "@3") == 0
                  || strcmp(part->name, "avIn") == 0
                  || strcmp(part->name, "bar") == 0
                  || strcmp(part->name, "@16") == 0;
        int32_t row = in_row ? win_part_row(part->y) : -1;
        if (in_row && !win_row_shown(g, row))
            continue;

        /* Two pieces of a row are clips the frame points at a frame of: the
           portrait, which it points at whoever the row is for, and the fill,
           which has a second frame it switches to on a level. Pointing both
           at the portrait put a second little face under the row. */
        const char *chosen = NULL;
        if (part->frames) {
            if (!in_row)
                continue;
            if (strcmp(part->name, "avIn") == 0)
                chosen = win_row_portrait(g, row);
            else if (strcmp(part->name, "bar") == 0)
                chosen = win_row_leveled(g, row) ? WIN_BAR_LEVEL
                                                 : WIN_BAR_PLAIN;
            else
                continue;
        }
        const char *name = chosen ? chosen
                                  : TextFormat("#%d", part->character);
        Art art;
        if (!asset_art(name, 1, &art))
            continue;
        /* The fill is the one piece the screen drives: its width is the
           percentage, out of the 95.1 the frame gives it. */
        if (strcmp(part->name, "bar") == 0) {
            float fraction = win_row_xp(g, row) / 100.0f;
            if (fraction < 0)
                fraction = 0;
            if (fraction > 1)
                fraction = 1;
            Rectangle box = placed_art(&art, part->x, part->y,
                                       part->scale_x, part->scale_y,
                                       part->origin_x, part->origin_y);
            box.width = WIN_BAR_WIDTH * fraction * part->scale_x;
            DrawTexturePro(*art.texture, art.source, box, (Vector2){0, 0},
                           0.0f, WHITE);
            continue;
        }
        draw_art_placed(&art, part->x, part->y, part->scale_x,
                        part->scale_y, part->origin_x, part->origin_y,
                        WHITE);
    }
}

/* The tally after a fight, laid out from the menu clip's own "win" frame: the
   party's experience down the left, what the fight paid in the middle, and
   the bag on the right with whatever dropped beside it. Every line is one of
   the frame's own text fields, filled from the game's own text arrays. */
void screen_victory_draw(Game *g, Vector2 mouse)
{
    ClearBackground(BLACK);
    /* The hub is behind this too -- the frame only hides the scene
       (KrinScreen._visible = false), not the furniture under it. */
    draw_hub_panel(g, mouse);
    draw_win_parts(g);

    /* Three of these are wider than one line of their own box, and the frame
       lets them wrap. */
    draw_field_wrapped(win_field("@901"), NO_OFFSET, lang_text("VICTORY", 0));
    /* vb2 is set whether or not anything dropped; vb99 is the "nothing did"
       on top of it. */
    draw_field_wrapped(win_field("@899"), NO_OFFSET, lang_text("VICTORY", 1));
    if (g->dropped_count == 0)
        draw_field_wrapped(win_field("@904"), NO_OFFSET, lang_text("MENU", 12));

    draw_field(win_field("@895"), NO_OFFSET, lang_text("VICTORY", 2));
    draw_field(win_field("@896"), NO_OFFSET,
               TextFormat("%s%d", EURO, g->rewards.euros));
    draw_field(win_field("@897"), NO_OFFSET, lang_text("VICTORY", 3));
    /* Math.round, not a cast: 51.5 reads 52 in the original. */
    draw_field(win_field("@898"), NO_OFFSET,
               TextFormat("%d%%", (int32_t)floor(g->rewards.xp_percent + 0.5)));
    draw_field_wrapped(win_field("@900"), NO_OFFSET, lang_text("VICTORY", 4));
    draw_field(win_field("@23"), NO_OFFSET, lang_text("MENU", 13));
    draw_field(win_field("@959"), NO_OFFSET, lang_text("MENU", 15));
    draw_field(win_field("@1059"), NO_OFFSET, lang_text("MENU", 14));
    draw_field(win_field("@1052"), NO_OFFSET,
               TextFormat("%d", g->campaign.euros));
    /* The frame's own content, which the game never writes to. */
    draw_field(win_field("@1053"), NO_OFFSET, EURO);

    /* The experience rows. The frame stacks three clips: two bound to the
       allies standing in the line and one, the top, hardwired to the player.
       A clip whose member is not there sets itself invisible, which is why a
       lone Sonny gets one row and not three. */
    for (int32_t row = 0; row < WIN_XP_ROWS; row++) {
        if (!win_row_shown(g, row))
            continue;
        draw_field(text_field_named(MENU_SCREEN, MENU_WIN, "@12", row),
                   NO_OFFSET, win_row_name(g, row));
        draw_field(text_field_named(MENU_SCREEN, MENU_WIN, "@13", row),
                   NO_OFFSET, TextFormat("%s%d", lang_text("MENU", 0),
                                         win_row_level(g, row)));
        draw_field(text_field_named(MENU_SCREEN, MENU_WIN, "@17", row),
                   NO_OFFSET,
                   TextFormat("%d%%",
                              (int32_t)floor(win_row_xp(g, row) + 0.5)));
    }

    /* A drop that has been taken is zeroed, and the square it was in stays:
       only the ones the fight never filled are hidden, and that happens once
       as the frame opens. */
    for (int32_t i = 0; i < g->dropped_count; i++) {
        Rectangle r = drop_rect(i);
        draw_slot_art(MENU_WIN, "dropSlot", i);
        if (g->dropped[i])
            draw_item_icon(item_by_id(g->dropped[i]), r, WHITE);
        if (hit(r, mouse))
            DrawRectangleLinesEx(r, 1.0f, (Color){235, 200, 90, 255});
    }

    /* The bag alongside, so what has been taken can be seen going into it.
       Its squares show whether or not anything is in them; a drop square is
       only there while something is in it. */
    for (int32_t i = 0; i < MENU_BAG_SLOTS; i++) {
        draw_slot_art(MENU_WIN, "itemSlot", i);
        if (g->campaign.inventory[i])
            draw_item_icon(item_by_id(g->campaign.inventory[i]),
                           win_bag_rect(i), WHITE);
    }
}
