/* The ability screen: the talent tree, the attributes and the action bar. */
#include "menu.h"

/* The four tips the screen picks between, which the original chooses from at
   random every time the menu is opened (43 + random(4)). */
#define SKILL_TIP_FIRST 43
/* The frame round the tip: how far outside the text field it sits. Square,
   like every other panel on the screen -- and drawn with two calls that have
   been in raylib since forever, because the rounded-rectangle outline moved
   between versions and this has to build against whatever the player has. */
#define TIP_FRAME_PAD   6.0f
#define SKILL_TIP_COUNT 4
/* And the four things it says instead when there are points to spend: what
   they are, how to spend them on the tree, how to spend them on an attribute,
   and the warning about the action bar. "< Click Here >" moves it on. */
#define SKILL_STEP_FIRST 7
#define SKILL_STEP_COUNT 4
#define SKILL_STEP_CLICK 11

/* Opening the skills screen. The original's box jumps to its walkthrough
   when there is anything to spend and otherwise picks one of its four tips,
   both decided as the clip loads. */
void screen_talents_open(Game *g)
{
    const Character *c = &g->campaign.player;
    int32_t points = character_unspent_skill_points(c)
                   + character_unspent_stat_points(c);
    g->skill_step = points > 0 ? 0 : -1;
    /* random(4), on the same dice as everything else -- raylib's own
       generator is seeded from the clock, which made the screen differ
       between two runs of the same seed. */
    g->skill_tip = (int32_t)rng_below(&g->rng, SKILL_TIP_COUNT);
}


/* ------------------------------------------------------------- talents */

/* The ability screen, which the menu clip carries on its "skills" frame: the
   tree down the left, who the character is and their attributes down the
   middle, and the combat action bar over the pool of everything they know on
   the right. Everything below is laid out from that frame.

   The frame's own script (sprite 1503, frame 25) is what says where the text
   comes from; the tree's node clip carries the rest, in its load handler. */
#define MENU_SKILLS "skills"

/* The button every tree node carries, which is both what the node answers in
   and, being the only thing that tells a node apart from the art, how big its
   box is. */
#define TREE_NODE_BUTTON 1418

/* The plus beside each attribute, by the button's own character: the handlers
   hang off these, and each adds to one of Krin.StatSets0. */
#define STAT_BUTTON_VITALITY 1451
#define STAT_BUTTON_STRENGTH 1448
#define STAT_BUTTON_MAGIC    1449
#define STAT_BUTTON_SPEED    1450

/* The swatches behind those buttons. They are one clip with a frame for each
   state: coloured while there are points to spend, and grey once the last one
   goes -- which the original only ever does part way through a visit, since
   opening the screen puts the clip back on its first frame. */
#define STAT_COLOUR_SLOT "statColor"
#define STAT_COLOUR_DEAD "#1444@8"

/* The ability pool, which the screen fills with one row per ability the
   character knows. The rows go in two columns, thirty-five apart down and
   twenty-five below the top of the pool, exactly as KrinCreateAbilityMatrix
   places them. */
#define POOL_SLOT      "talentPool"
#define POOL_ROW       "talenter"
#define POOL_ROW_STEP  35.0f
#define POOL_ROW_TOP   25.0f
#define POOL_COLUMN_A  5.0f
#define POOL_COLUMN_B  85.0f
/* Where the row's own orb sits in it. */
#define POOL_ROW_ORB_X 28.5f
#define POOL_ROW_ORB_Y 0.05f

/* The frame the orb clip shows for a slot with nothing assigned to it: the
   screen does gotoAndStop("Empty") on any such slot of the bar. */
#define EMPTY_ORB "Empty"

/* How wide an orb answers to the pointer, and how wide a pool row is. */
#define ORB_HIT        24.0f
#define POOL_ROW_WIDTH 64.0f

/* The orb inside a tree node sits a fraction above the node's own point. */
#define TREE_NODE_ORB_Y (-0.35f)

/* The branches the tree draws between a node and each of its prerequisites:
   a six-wide black line with a two-wide one over it, gold once the
   prerequisite is learned and near-black while it is not. */
#define TREE_LINE_BACK   6.0f
#define TREE_LINE_FRONT  2.0f
#define TREE_LINE_OPEN   (Color){0xFF, 0xCC, 0x00, 255}
#define TREE_LINE_SHUT   (Color){0x2B, 0x2B, 0x2B, 255}

/* One of the skills frame's fields, by the name the frame gives it. */
static const TextField *skill_field(const char *name, int32_t occurrence)
{
    return text_field_named(MENU_SCREEN, MENU_SKILLS, name, occurrence);
}

static Rectangle talent_rect(int32_t node)
{
    const TalentSlot *slot = talent_slot(node);
    const StageButton *box = stage_button(MENU_SKILLS, TREE_NODE_BUTTON, 0);
    if (!slot || !box)
        return (Rectangle){0, 0, 0, 0};
    return (Rectangle){slot->x - box->width / 2, slot->y - box->height / 2,
                       box->width, box->height};
}

/* The rank readout over a node: the rank and the tier it tops out at, each
   drawn twice -- black, then white a pixel up and to the left. The frame lays
   an identical set over every node, but in the tree's own stacking order
   rather than the nodes' own, so a node takes the set laid nearest to it. */
#define TREE_RANK_FIELDS 4

static const TextField *nearest_node_field(const TalentSlot *slot,
                                           int32_t which)
{
    int32_t best = -1;
    float nearest = 0;
    for (int32_t i = 0; i < SONNY_TALENT_SLOT_COUNT; i++) {
        const TextField *f = text_field(MENU_SCREEN, MENU_SKILLS,
                                        i * TREE_RANK_FIELDS);
        if (!f)
            break;
        float dx = f->x - slot->x, dy = f->y - slot->y;
        float away = dx * dx + dy * dy;
        if (best < 0 || away < nearest) {
            best = i;
            nearest = away;
        }
    }
    if (best < 0)
        return NULL;
    return text_field(MENU_SCREEN, MENU_SKILLS,
                      best * TREE_RANK_FIELDS + which);
}

/* The same, worked out once per node: the layout never moves, and the search
   is every set against every node. */
static const TextField *node_field(const TalentSlot *slot, int32_t which)
{
    static const TextField *found[SONNY_TALENT_MAX][TREE_RANK_FIELDS];
    static char looked[SONNY_TALENT_MAX];
    int32_t node = (int32_t)(slot - SONNY_TALENT_SLOTS);
    if (node < 0 || node >= SONNY_TALENT_MAX || which < 0
        || which >= TREE_RANK_FIELDS)
        return nearest_node_field(slot, which);
    if (!looked[node]) {
        for (int32_t w = 0; w < TREE_RANK_FIELDS; w++)
            found[node][w] = nearest_node_field(slot, w);
        looked[node] = 1;
    }
    return found[node][which];
}

/* What a node's orb shows. A node that grants a move shows that move's icon,
   at the rank currently held -- or the first rank's while nothing is learned,
   which is what the tree's own load handler points the orb at. A passive
   shows its buff's icon instead, which the orb clip has its own frame for. */
static const char *node_icon(const Character *c, int32_t node)
{
    const TalentDef *t = &SONNY_TALENTS[node];
    if (t->passive)
        return t->buff_name;
    int32_t rank = c->rank[node];
    const AbilityDef *a = ability_by_id(t->ability_id
                                        + (rank > 0 ? rank - 1 : 0));
    return a ? a->icon : NULL;
}

/* The eight slots of the combat action bar, which are the same ring the fight
   puts round a unit -- here the menu places it flat on the screen, so each
   orb is taken from the selector's own pieces by name. */
static const SlotPiece *bar_slot(int32_t slot)
{
    char name[16];
    snprintf(name, sizeof(name), "thing%d", (int)slot);
    for (int32_t i = 0; ; i++) {
        const SlotPiece *piece = slot_piece(MENU_SKILLS, "selector", i);
        if (!piece)
            return NULL;
        if (strcmp(piece->name, name) == 0)
            return piece;
    }
}

/* Where the pool's `index`-th row goes, in stage coordinates. */
static Vector2 pool_row(int32_t index)
{
    const MenuSlot *pool = menu_slot(MENU_SKILLS, POOL_SLOT);
    Vector2 at = {0, 0};
    if (!pool)
        return at;
    at.x = pool->x + (index % 2 ? POOL_COLUMN_B : POOL_COLUMN_A);
    at.y = pool->y + POOL_ROW_TOP + (index / 2) * POOL_ROW_STEP;
    return at;
}

/* The four attributes the screen offers, in the order the frame stacks their
   rows: each is one of SYSTEM's names, one of the character's derived
   numbers, and the button that buys another point of it. */
static const struct {
    const char *label, *value;
    int32_t     button;
    int32_t     stat;
} SKILL_ATTRIBUTES[4] = {
    {"@573", "@577", STAT_BUTTON_VITALITY, 0},
    {"@574", "@578", STAT_BUTTON_STRENGTH, 1},
    {"@575", "@579", STAT_BUTTON_MAGIC,    2},
    {"@576", "@580", STAT_BUTTON_SPEED,    3},
};

void screen_talents_draw(Game *g, Vector2 mouse)
{
    (void)mouse;
    const Character *c = &g->campaign.player;

    ClearBackground(BLACK);
    /* The hub stays behind the menu, as it does in the original. */
    draw_hub_panel(g, mouse);
    draw_clip_parts(MENU_SCREEN, MENU_SKILLS, NO_OFFSET, NULL, NULL, WHITE);

    /* The four headings, which the frame reads out of the language table. */
    draw_field(skill_field("@603", 0), NO_OFFSET, lang_text("K_TITLE1", 0));
    draw_field(skill_field("@601", 0), NO_OFFSET, lang_text("K_TITLE2", 0));
    draw_field(skill_field("@602", 0), NO_OFFSET, lang_text("K_TITLE3", 0));
    draw_field(skill_field("@604", 0), NO_OFFSET, lang_text("MENU", 16));

    /* Who the character is, and what they have to spend. */
    draw_field(skill_field("@583", 0), NO_OFFSET, lang_text("NAVTITLE2", 3));
    draw_field(skill_field("@584", 0), NO_OFFSET,
               TextFormat("%s%d %s", lang_text("MENU", 0), c->level,
                          lang_text("CLASS", c->class_template
                                    ? c->class_template->id - 1 : 0)));
    draw_field(skill_field("@581", 0), NO_OFFSET,
               TextFormat("%s:", lang_text("SKLOAD", 0)));
    draw_field(skill_field("@585", 0), NO_OFFSET,
               TextFormat("%d", character_unspent_skill_points(c)));
    draw_field(skill_field("@582", 0), NO_OFFSET,
               TextFormat("%s:", lang_text("SPLOAD", 0)));
    draw_field(skill_field("@586", 0), NO_OFFSET,
               TextFormat("%d", character_unspent_stat_points(c)));
    /* The tip. Every tree node carries a rank readout whose own fields are
       called "@1" to "@4" as well, so the screen's own is the one after
       them. */
    /* Opening the screen with points in hand starts the original's
       walkthrough; with none it shows one of its tips. */
    const TextField *tip = skill_field("@1", SONNY_TALENT_SLOT_COUNT);
    /* A frame round it, which is a departure: the original lets the tip float
       loose in the middle of the panel with nothing to say it is one thing
       rather than a stray line under the points. Amber, because that is the
       colour this game highlights in -- the zone bar, the hovered slot -- and
       over a wash of black so the text lifts off the panel behind it. */
    if (tip) {
        Rectangle frame = {tip->x - TIP_FRAME_PAD, tip->y - TIP_FRAME_PAD,
                           tip->width + TIP_FRAME_PAD * 2,
                           tip->height + TIP_FRAME_PAD * 2};
        DrawRectangleRec(frame, (Color){0, 0, 0, 70});
        DrawRectangleLinesEx(frame, 1.0f, (Color){235, 200, 90, 130});
    }
    if (g->skill_step >= 0 && g->skill_step < SKILL_STEP_COUNT) {
        draw_field_wrapped(tip, NO_OFFSET,
                           TextFormat("%s\n\n%s",
                                      lang_text("MENU", SKILL_STEP_FIRST
                                                + g->skill_step),
                                      lang_text("MENU", SKILL_STEP_CLICK)));
    } else {
        draw_field_wrapped(tip, NO_OFFSET,
                           lang_text("MENU", SKILL_TIP_FIRST + g->skill_tip));
    }

    /* The attributes: the swatches behind the row of plus buttons, then each
       row's name, number and button. */
    const MenuSlot *swatch = menu_slot(MENU_SKILLS, STAT_COLOUR_SLOT);
    if (swatch && swatch->width > 0) {
        Art art;
        if (asset_art(g->stat_points_spent
                      ? STAT_COLOUR_DEAD
                      : TextFormat("#%d", swatch->character), 1, &art))
            draw_art_placed(&art, swatch->x, swatch->y, swatch->scale,
                            swatch->scale, swatch->origin_x,
                            swatch->origin_y, WHITE);
    }
    DerivedStats stats = character_derive(c);
    const double shown[4] = {stats.life, stats.strength, stats.magic,
                             stats.speed};
    for (int i = 0; i < 4; i++) {
        draw_button_art(stage_button(MENU_SKILLS, SKILL_ATTRIBUTES[i].button,
                                     0), WHITE);
        draw_field(skill_field(SKILL_ATTRIBUTES[i].label, 0), NO_OFFSET,
                   TextFormat("%s:", lang_text("SYSTEM",
                                               SKILL_ATTRIBUTES[i].stat)));
        draw_field(skill_field(SKILL_ATTRIBUTES[i].value, 0), NO_OFFSET,
                   TextFormat("%d", (int32_t)shown[i]));
    }

    /* The tree. The branches go in first, under the nodes, and each is drawn
       twice: a thick black line with a thin coloured one over it, gold once
       the prerequisite it comes from is learned. */
    for (int32_t node = 0; node < SONNY_TALENT_COUNT; node++) {
        const TalentSlot *from = talent_slot(node);
        if (!from)
            continue;
        for (int32_t i = 0; i < SONNY_TALENTS[node].prereq_count; i++) {
            int32_t prereq = SONNY_TALENTS[node].prereq[i];
            const TalentSlot *to = prereq >= 0 ? talent_slot(prereq) : NULL;
            if (!to)
                continue;
            Vector2 a = {from->x, from->y}, b = {to->x, to->y};
            DrawLineEx(a, b, TREE_LINE_BACK, BLACK);
            DrawLineEx(a, b, TREE_LINE_FRONT,
                       c->rank[prereq] > 0 ? TREE_LINE_OPEN : TREE_LINE_SHUT);
        }
    }
    for (int32_t node = 0; node < SONNY_TALENT_COUNT; node++) {
        const TalentSlot *slot = talent_slot(node);
        if (!slot)
            continue;
        Vector2 centre = {slot->x, slot->y + TREE_NODE_ORB_Y};
        /* A node with nothing spent on it wears the same black disc the ring
           puts over a move that cannot be used. */
        draw_orb(node_icon(c, node), centre, slot->scale,
                 c->rank[node] > 0 ? 0 : ORB_DIM_TALENT, 0);
        /* The rank over it, which the tree hides unless the space bar is
           down. */
        if (!IsKeyDown(KEY_SPACE))
            continue;
        const char *rank = TextFormat("%d", c->rank[node]);
        const char *tier = TextFormat("%d", SONNY_TALENTS[node].max_rank);
        draw_field(node_field(slot, 0), NO_OFFSET, rank);
        draw_field(node_field(slot, 1), NO_OFFSET, tier);
        draw_field(node_field(slot, 2), NO_OFFSET, rank);
        draw_field(node_field(slot, 3), NO_OFFSET, tier);
    }

    /* The combat action bar: the eight slots of the loadout, each an orb, and
       a slot with nothing on it showing the ring's own empty frame. */
    for (int32_t i = 0; i < SONNY_MOVE_SLOTS; i++) {
        const SlotPiece *piece = bar_slot(i);
        if (!piece)
            continue;
        const AbilityDef *a = ability_by_id(c->move_matrix[i]);
        Vector2 centre = {piece->x, piece->y};
        draw_orb((a && a->id != 0) ? a->icon : EMPTY_ORB, centre,
                 piece->scale_x, 0, 0);
    }

    /* The pool below it: one row per ability the character knows, two to a
       line, inside the pool's own box. */
    const MenuSlot *pool = menu_slot(MENU_SKILLS, POOL_SLOT);
    if (pool && pool->width > 0) {
        Rectangle box = placed_rect(pool->x, pool->y, pool->scale, pool->scale,
                                    pool->width, pool->height, pool->origin_x,
                                    pool->origin_y);
        int32_t known[SONNY_TALENT_MAX + SONNY_MOVE_SLOTS];
        int32_t count = character_known_abilities(c, known,
                                                  (int32_t)(sizeof(known)
                                                            / sizeof(known[0])));
        draw_slot_art(MENU_SKILLS, POOL_SLOT, -1);
        render_scissor(box);
        for (int32_t i = 0; i < count; i++) {
            Vector2 at = pool_row(i);
            draw_clip_parts(MENU_SCREEN, POOL_ROW, at, NULL, NULL, WHITE);
            /* The original walks the list of known abilities with for..in,
               which hands back an array's indices last to first, so the row
               the pool fills first is the last ability learned. */
            const AbilityDef *a = ability_by_id(known[count - 1 - i]);
            Vector2 centre = {at.x + POOL_ROW_ORB_X, at.y + POOL_ROW_ORB_Y};
            draw_orb(a ? a->icon : NULL, centre, 1.0f, 0, 0);
        }
        EndScissorMode();
    }
}

/* Put what the pointer is carrying on one of the eight slots, the way
   addMoveForPlayer does: carrying nothing empties the slot, and a move can
   only take as many slots at once as its own number allows. */
static void place_on_bar(Game *g, int32_t slot)
{
    Character *c = &g->campaign.player;
    if (g->carrying == 0) {
        c->move_matrix[slot] = 0;
        return;
    }
    const AbilityDef *a = ability_by_id(g->carrying);
    int32_t already = 0;
    for (int32_t i = 0; i < SONNY_MOVE_SLOTS; i++)
        if (c->move_matrix[i] == g->carrying)
            already++;
    if (a && already < a->bar_copies)
        c->move_matrix[slot] = g->carrying;
    else
        game_notice(g, "%s", lang_text("SKILLERROR", 0));
    g->carrying = 0;
}

/* What a node in the tree says about itself. The original puts four things
   on it: the rank it stands at out of its tier with the move's name, what the
   move does at the rank it is now, what it costs, and what the next tier
   would do and the level it wants -- which is the part that says where a
   point goes.

   A passive node reads all of that out of the BUFFSAY table rather than off
   the buff it grants: BUFFSAY[name] is what it is called and
   BUFFSAY[name + rank] is what that rank of it does, with the ranks numbered
   from zero. The port was asking the buff table for a key ending in 0, which
   no buff has, so four of the passives said nothing at all.

   A node with nothing spent on it does not describe its first tier either.
   The original replaces the whole line:

       if(Krin.talentMainArray[krink] == 0)
          thing2.toolTip = KrinLang[...].SKILLTALENTTIP2;   */
static void talent_tooltip(Game *g, const Character *c, int32_t node)
{
    const TalentDef *t = &SONNY_TALENTS[node];
    int32_t rank = c->rank[node];
    /* bobJimJohn: the tier whose words are shown, which is the one below the
       rank, and the first tier while nothing is spent. */
    int32_t at = rank > 0 ? rank - 1 : 0;

    const char *name = "";
    const char *body = "";
    const char *third = "";
    if (t->passive) {
        name = lang_say("BUFFSAY", t->buff_name);
        body = lang_say("BUFFSAY", TextFormat("%s%d", t->buff_name, at));
        third = lang_text("SKILLAURA", 0);
    } else {
        const AbilityDef *now = ability_by_id(t->ability_id + at);
        if (now) {
            name = now->name;
            body = now->tooltip;
            /* The third line of a move's tip is what it costs, which
               addNewMove() writes as it registers the move. */
            third = now->cost_text;
        }
    }
    /* Nothing spent: the node says so instead of describing a tier. */
    if (rank == 0)
        body = lang_text("SKILLTALENTTIP2", 0);
    game_tooltip(g, TextFormat("(%d/%d)  %s", rank, t->max_rank, name), body);
    if (third && third[0])
        game_tooltip_line(g, third);

    if (rank >= t->max_rank) {
        game_tooltip_line(g, lang_text("SKILLTALENTTIP3", 0));
        return;
    }
    /* The tier a point would buy, and the level it asks for. */
    int32_t wants = t->level_min + t->level_scale * rank;
    const char *next = "";
    if (t->passive) {
        next = lang_say("BUFFSAY", TextFormat("%s%d", t->buff_name, rank));
    } else {
        const AbilityDef *up = ability_by_id(t->ability_id + rank);
        if (up)
            next = up->tooltip;
    }
    game_tooltip_line(g, TextFormat("%s%d): %s",
                                    lang_text("SKILLTALENTTIP", 0), wants,
                                    next));
}

void screen_talents_update(Game *g, Vector2 mouse)
{
    hud_buttons(g, mouse);
    Character *c = &g->campaign.player;
    g->hovered_item = -1;

    /* The walkthrough steps on when its box is clicked, and gives way to a
       tip once it has said its piece. */
    const TextField *tip = skill_field("@1", SONNY_TALENT_SLOT_COUNT);
    if (g->skill_step >= 0 && tip && ui_clicked()
        && hit(BOX_OF(tip), mouse)) {
        g->skill_step++;
        if (g->skill_step >= SKILL_STEP_COUNT)
            g->skill_step = -1;
        return;
    }

    /* The action bar: pick an ability up off the pool or the tree, drop it on
       a slot. */
    for (int32_t i = 0; i < SONNY_MOVE_SLOTS; i++) {
        const SlotPiece *piece = bar_slot(i);
        if (!piece)
            continue;
        Rectangle box = {piece->x - ORB_HIT / 2, piece->y - ORB_HIT / 2,
                         ORB_HIT, ORB_HIT};
        if (!hit(box, mouse))
            continue;
        const AbilityDef *on = ability_by_id(c->move_matrix[i]);
        if (on && on->id != 0) {
            game_tooltip(g, on->name, on->tooltip);
            /* Every place a move names itself carries what it costs under
               it, which is slot 18 of the move's own table. */
            game_tooltip_line(g, on->cost_text);
        } else {
            game_tooltip(g, lang_text("SKILLNONE", 0),
                         lang_text("SKILLTUT", 0));
            game_tooltip_line(g, lang_text("SKILLTUT2", 0));
        }
        if (ui_clicked()) {
            audio_play("Click2putdown");
            place_on_bar(g, i);
            return;
        }
    }

    /* The pool below it: clicking a row picks that ability up. */
    {
        int32_t known[SONNY_TALENT_MAX + SONNY_MOVE_SLOTS];
        int32_t count = character_known_abilities(c, known,
                                                  (int32_t)(sizeof(known)
                                                            / sizeof(known[0])));
        for (int32_t i = 0; i < count; i++) {
            Vector2 at = pool_row(i);
            Rectangle box = {at.x, at.y - ORB_HIT / 2, POOL_ROW_WIDTH,
                             ORB_HIT};
            if (!hit(box, mouse))
                continue;
            const AbilityDef *a = ability_by_id(known[count - 1 - i]);
            if (!a)
                continue;
            /* A row in the pool also says how many places on the bar the
               move may take at once, which the pool is the only screen to
               mention. */
            game_tooltip(g, a->name,
                         TextFormat("%s%s%d%s", a->tooltip,
                                    lang_text("SKILLTHRS1", 0),
                                    a->bar_copies,
                                    lang_text("SKILLTHRS2", 0)));
            game_tooltip_line(g, a->cost_text);
            if (ui_clicked()) {
                audio_play("Click3pickup");
                g->carrying = a->id;
                return;
            }
        }
    }

    /* A point of an attribute, which the original buys one click at a time
       and stops offering the moment the last one is gone. */
    for (int i = 0; i < 4; i++) {
        const StageButton *b = stage_button(MENU_SKILLS,
                                            SKILL_ATTRIBUTES[i].button, 0);
        if (!b || !ui_clicked()
            || !hit(BOX_OF(b), mouse))
            continue;
        if (character_unspent_stat_points(c) <= 0)
            break;
        /* One point, into the record and into the running total, which is
           what the original's own handler does:

               _root.Krin.StatSets0[1] += 1;
               _root.Krin.statPoints--;
               yut_str2++;
               _root.Krin.STRENGTH += 1;

           and emphatically not a rebuild. A rebuild is the respec operation
           -- zero the record, fold in every worn item -- and the starting
           gear is worn without ever having passed through a slot, so it is
           not in the total. Rebuilding here dropped all of it in at once
           behind the point: a click on Speed moved the number by two and a
           click on Strength by four. */
        c->spent[SKILL_ATTRIBUTES[i].stat] += 1;
        c->stat_sets[SKILL_ATTRIBUTES[i].stat] += 1;
        c->spent_stat_points++;
        /* The swatches go grey as the last point goes, and stay that way
           until the screen is opened again. */
        if (character_unspent_stat_points(c) == 0)
            g->stat_points_spent = 1;
        return;
    }

    for (int32_t node = 0; node < SONNY_TALENT_COUNT; node++) {
        if (!hit(talent_rect(node), mouse))
            continue;
        g->hovered_item = node;
        talent_tooltip(g, c, node);
        if (!ui_clicked())
            break;

        TalentError err = character_learn(c, node);
        switch (err) {
        case TALENT_OK:
            audio_play("Click2putdown");
            /* And nothing else: the original's handler only upgrades a slot
               that already holds the tier below, which character_learn does,
               and adds the move to the pool to be picked up from. Putting a
               newly learned move straight on the bar is not something it
               ever does. */
            break;
        case TALENT_NO_POINTS:
            game_notice(g, "%s", lang_text("MENU", 20));
            break;
        default:
            break;
        }
        break;
    }

    if (menu_close_pressed(mouse))
        g->screen = SCREEN_ZONE;
}
