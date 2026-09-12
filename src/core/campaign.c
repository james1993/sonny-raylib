#include <math.h>
#include <string.h>
#include "campaign.h"

void campaign_new(Campaign *c, int32_t class_id)
{
    memset(c, 0, sizeof(*c));
    character_new(&c->player, class_id);
    /* The first progress battle of zone 0. */
    c->progress_battle = SONNY_ZONES[0].first_battle;
    c->zone = 0;
}

const ZoneDef *campaign_zone(const Campaign *c)
{
    const ZoneDef *z = zone_of_battle(c->progress_battle);
    return z ? z : &SONNY_ZONES[0];
}

int campaign_complete(const Campaign *c)
{
    return c->progress_battle >= 38;
}

void campaign_advance(Campaign *c)
{
    c->progress_battle++;
    const ZoneDef *z = zone_of_battle(c->progress_battle);
    if (z)
        c->zone = z->zone;
}

int campaign_setup_battle(const Campaign *c, const BattleDef *def, Battle *out,
                          uint64_t seed)
{
    if (!def)
        return 0;

    battle_init(out, seed, 1);
    battle_place_character(out, 1, &c->player, "Sonny", 0);

    for (int32_t i = 0; i < SONNY_BATTLE_SLOTS; i++) {
        int32_t entry = def->players[i];
        int32_t slot = i + 2;
        if (entry == 0)
            continue;

        int32_t level = def->levels[i];
        if (level < 0)              /* "X": match the player */
            level = c->player.level;

        if (entry > 0) {
            const UnitTemplate *t = unit_template_by_id(entry);
            if (t)
                battle_place_enemy(out, slot, t, level, 1);
        } else {
            /* A negative entry selects a party ally: friendArrayX[entry + 2]. */
            int32_t ally = entry + 2;
            if (ally >= 0 && ally < SONNY_MAX_ALLIES && c->ally_present[ally])
                battle_place_character(out, slot, &c->allies[ally], NULL, 1);
        }
    }

    /* absoluteStart forces a team to move first by giving it an unbeatable
       average speed for the opening TeamSelect, as frame 212 does. */
    battle_team_select(out);
    if (def->absolute_start == 1 || def->absolute_start == 2) {
        out->TeamMove = def->absolute_start;
        out->TeamMoveNow = out->TeamMove;
        out->s1tm = (out->TeamMove == 1) ? 0 : 3;
        out->s2tm = (out->TeamMove == 1) ? 3 : 0;
        for (int32_t i = 1; i < SONNY_SLOTS; i++)
            if (out->units[i].active)
                out->brains[i].AIGoER =
                    (out->units[i].teamSide == out->TeamMoveNow);
    }
    out->PrevTeam = out->TeamMove;
    for (int32_t i = 1; i < SONNY_SLOTS; i++)
        battle_queue(out, i, i, 0, 0);
    return 1;
}

int32_t campaign_roll_drops(const BattleDef *def, Rng *rng, int32_t *out,
                            int32_t max)
{
    int32_t n = 0;

    for (int32_t i = 0; i < def->drop_count && n < max; i++) {
        int32_t roll = (int32_t)rng_below(rng, 100);
        if (def->drops[i].chance > roll)
            out[n++] = def->drops[i].item_id;
    }

    /* The rare pick is the original's floor(random(count * 100) / 100), which
       is a uniform draw over the table with a redundant scaling step. */
    for (int32_t i = 0; i < def->rare_dropper && n < max; i++) {
        if (def->rare_count <= 0)
            break;
        int32_t roll = (int32_t)rng_below(rng, def->rare_count * 100);
        int32_t index = roll / 100;
        if (index >= def->rare_count)
            index = def->rare_count - 1;
        out[n++] = def->rare[index];
    }
    return n;
}

BattleRewards campaign_award(Campaign *c, const Battle *b, Rng *rng)
{
    BattleRewards r;
    memset(&r, 0, sizeof(r));

    int32_t levels[SONNY_SLOTS];
    int32_t count = 0;
    int32_t player_team = b->units[1].teamSide;
    for (int32_t i = 1; i < SONNY_SLOTS; i++)
        if (b->units[i].LIFEU > 0 && b->units[i].teamSide != player_team)
            levels[count++] = b->units[i].plevel;

    r.enemy_rating = rewards_enemy_rating(levels, count);
    r.xp_percent = character_xp_gain(r.enemy_rating, c->player.level);
    r.euros = rewards_money(r.enemy_rating, rng);
    c->euros += r.euros;
    r.leveled = character_award_xp(&c->player, r.xp_percent);
    return r;
}
