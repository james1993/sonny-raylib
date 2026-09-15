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
    for (int32_t i = 0; i < SONNY_PARTY_SIZE; i++)
        c->friends[i] = SONNY_PARTY_START[i];
    for (int32_t i = 0; i < SONNY_MAX_ALLIES; i++)
        c->line[i] = SONNY_PARTY_TEAM[i];
    /* Each of them starts wearing and carrying what the table gives them. */
    for (int32_t m = 0; m < SONNY_PARTY_SIZE && m < SONNY_PARTY_COUNT; m++) {
        for (int32_t i = 0; i < SONNY_EQUIP_SLOTS; i++)
            c->ally_equip[m][i] = SONNY_PARTY[m].equip[i];
        for (int32_t i = 0; i < SONNY_STATS; i++)
            c->ally_stat_sets[m][i] = SONNY_PARTY[m].stat[i];
    }
}

int campaign_has_friend(const Campaign *c, int32_t member)
{
    if (member < 0 || member >= SONNY_PARTY_SIZE)
        return 0;
    for (int32_t i = 0; i < SONNY_PARTY_SIZE; i++)
        if (c->friends[i] == member)
            return 1;
    return 0;
}

void campaign_ally(const Campaign *c, int32_t member, Character *out)
{
    memset(out, 0, sizeof(*out));
    if (member < 0 || member >= SONNY_PARTY_COUNT)
        return;
    const PartyMember *m = &SONNY_PARTY[member];
    /* ClassStats is the class id plus one, the way the class menu numbers
       them, so the template is the one before it. */
    out->class_template = unit_template_by_id(m->class_id);
    out->level = m->level;
    for (int32_t i = 0; i < SONNY_EQUIP_SLOTS; i++)
        out->equip[i] = c->ally_equip[member][i];
    for (int32_t i = 0; i < SONNY_STATS; i++)
        out->spent[i] = m->stat[i];
    for (int32_t e = 0; e < SONNY_ELEMENTS; e++) {
        out->per_sets[e] = m->per[e];
        out->def_sets[e] = m->def_[e];
    }
    /* Their running total is theirs, and it moves as their gear does. */
    for (int32_t i = 0; i < SONNY_STATS; i++)
        out->stat_sets[i] = c->ally_stat_sets[member][i];
}

void campaign_story_joins(Campaign *c)
{
    for (int i = 0; i < SONNY_PARTY_JOIN_COUNT; i++) {
        const PartyJoin *j = &SONNY_PARTY_JOINS[i];
        int reached = j->exact ? (c->progress_battle == j->at)
                               : (c->progress_battle >= j->at);
        if (!reached)
            continue;
        for (int32_t k = 0; k < SONNY_PARTY_SIZE; k++)
            c->friends[k] = j->friends[k];
    }
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
    /* The story hands people over at set points, which the original checks in
       the marker's own handler and on the map. */
    campaign_story_joins(c);
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
            /* A negative entry selects a party ally: friendArrayX[entry + 2]
               names which of the six stands there, and they only turn up if
               the story has handed them over. */
            int32_t place = entry + 2;
            if (place < 0 || place >= SONNY_MAX_ALLIES)
                continue;
            int32_t member = c->line[place];
            if (!campaign_has_friend(c, member))
                continue;
            Character ally;
            campaign_ally(c, member, &ally);
            battle_place_character(out, slot, &ally,
                                   SONNY_PARTY[member].name, 1);
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

/* One level, which is also the point in each pool: both budgets are the level
   less one, so the original's skillPoints++ and statPoints++ come for free. */
int32_t campaign_apply_xp(Campaign *c, double amount)
{
    return character_award_xp(&c->player, amount);
}

BattleRewards campaign_award(Campaign *c, const Battle *b, Rng *rng,
                             int apply)
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
    /* The experience is not granted here. The original's victory screen fills
       the bar towards it over thirty frames and levels up when the fill gets
       there, which is when the skill and attribute points arrive -- so the
       award belongs to the screen, and `apply` is for callers with no
       screen. */
    r.leveled = 0;
    if (apply)
        r.leveled = campaign_apply_xp(c, r.xp_percent);
    return r;
}
