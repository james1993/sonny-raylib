#include <math.h>
#include <stdio.h>
#include <string.h>
#include "character.h"

void character_new(Character *c, int32_t class_id)
{
    memset(c, 0, sizeof(*c));
    c->class_template = unit_template_by_id(class_id);
    c->level = 1;
    /* Krin.StatSets0 = [0,5,0,3,0] */
    c->spent[1] = 5;
    c->spent[3] = 3;
    /* The running total starts at the hand-spent bonus alone: the gear below
       is worn without ever having passed through a slot, so the original
       never folds it in. */
    for (int32_t i = 0; i < SONNY_STATS; i++)
        c->stat_sets[i] = c->spent[i];
    /* Krin.equipArray0 = [0,0,0,4,8,5,0]: the trousers, boots and pipe the
       story gives Sonny on the ship. */
    c->equip[3] = 4;
    c->equip[4] = 8;
    c->equip[5] = 5;
    c->move_matrix[0] = SONNY_START_SKILL1;
    c->move_matrix[1] = SONNY_START_SKILL2;
}

int32_t character_talent_next_level(const Character *c, int32_t node)
{
    if (node < 0 || node >= SONNY_TALENT_COUNT)
        return 0;
    const TalentDef *t = &SONNY_TALENTS[node];
    return t->level_min + t->level_scale * c->rank[node];
}

int32_t character_unspent_skill_points(const Character *c)
{
    return character_skill_points(c) - c->spent_skill_points;
}

void character_respec(Character *c)
{
    memset(c->rank, 0, sizeof(c->rank));
    memset(c->skill_adder, 0, sizeof(c->skill_adder));
    memset(c->buff_adder, 0, sizeof(c->buff_adder));
    memset(c->move_matrix, 0, sizeof(c->move_matrix));
    memset(c->spent, 0, sizeof(c->spent));
    character_rebuild_sets(c);
    c->spent_skill_points = 0;
    c->spent_stat_points = 0;
    c->move_matrix[0] = SONNY_START_SKILL1;
    c->move_matrix[1] = SONNY_START_SKILL2;
}

int32_t character_unspent_stat_points(const Character *c)
{
    return character_stat_points(c) - c->spent_stat_points;
}

TalentError character_can_learn(const Character *c, int32_t node)
{
    if (node < 0 || node >= SONNY_TALENT_COUNT)
        return TALENT_MISSING_PREREQ;
    const TalentDef *t = &SONNY_TALENTS[node];

    if (character_unspent_skill_points(c) <= 0)
        return TALENT_NO_POINTS;
    if (c->rank[node] == t->max_rank)
        return TALENT_MAX_RANK;
    if (c->level < character_talent_next_level(c, node))
        return TALENT_LEVEL_TOO_LOW;
    for (int32_t i = 0; i < t->prereq_count; i++) {
        int32_t p = t->prereq[i];
        /* -1 encodes "no prerequisite". */
        if (p < 0 || p >= SONNY_TALENT_MAX)
            continue;
        if (c->rank[p] == 0)
            return TALENT_MISSING_PREREQ;
    }
    return TALENT_OK;
}

TalentError character_learn(Character *c, int32_t node)
{
    TalentError err = character_can_learn(c, node);
    if (err != TALENT_OK)
        return err;

    const TalentDef *t = &SONNY_TALENTS[node];
    c->spent_skill_points++;
    c->rank[node]++;

    if (!t->passive) {
        int32_t upgraded = t->ability_id + (c->rank[node] - 1);
        /* A bar slot holding the previous rank follows the upgrade. */
        for (int32_t i = 0; i < SONNY_MOVE_SLOTS; i++)
            if (c->move_matrix[i] != 0
                && c->move_matrix[i] == c->skill_adder[node])
                c->move_matrix[i] = upgraded;
        c->skill_adder[node] = upgraded;
    } else {
        /* The key is the buff name with the rank appended, so rank 2 of
           REGENERATION is the buff "REGENERATION2". */
        snprintf(c->buff_adder[node], SONNY_NAME_LEN, "%s%d", t->buff_name,
                 c->rank[node]);
    }
    return TALENT_OK;
}

int32_t character_known_abilities(const Character *c, int32_t *out, int32_t max)
{
    int32_t n = 0;
    if (n < max)
        out[n++] = SONNY_START_SKILL1;
    if (n < max)
        out[n++] = SONNY_START_SKILL2;
    for (int32_t i = 0; i < SONNY_TALENT_MAX && n < max; i++)
        if (c->skill_adder[i] > 0)
            out[n++] = c->skill_adder[i];
    return n;
}

int32_t character_passive_buffs(const Character *c, const char **out,
                                int32_t max)
{
    int32_t n = 0;
    for (int32_t i = 0; i < SONNY_TALENT_MAX && n < max; i++)
        if (c->buff_adder[i][0])
            out[n++] = c->buff_adder[i];
    return n;
}

int32_t character_stat_points(const Character *c)
{
    return c->level - 1;
}

int32_t character_skill_points(const Character *c)
{
    return c->level - 1;
}

void character_apply_item(Character *c, const ItemDef *item, int adding)
{
    if (!item)
        return;
    double sign = adding ? 1.0 : -1.0;
    for (int32_t i = 0; i < SONNY_STATS; i++)
        c->stat_sets[i] += sign * item->stat[i];
    for (int32_t e = 0; e < SONNY_ELEMENTS; e++) {
        c->per_sets[e] += sign * item->per[e];
        c->def_sets[e] += sign * item->def[e];
    }
}

void character_rebuild_sets(Character *c)
{
    for (int32_t i = 0; i < SONNY_STATS; i++)
        c->stat_sets[i] = c->spent[i];
    for (int32_t e = 0; e < SONNY_ELEMENTS; e++) {
        c->per_sets[e] = 0;
        c->def_sets[e] = 0;
    }
    for (int32_t slot = 0; slot < SONNY_EQUIP_SLOTS; slot++)
        character_apply_item(c, item_by_id(c->equip[slot]), 1);
}

StatSets character_stat_sets(const Character *c)
{
    StatSets s;
    for (int32_t i = 0; i < SONNY_STATS; i++)
        s.stat[i] = c->stat_sets[i];
    for (int32_t e = 0; e < SONNY_ELEMENTS; e++) {
        s.per[e] = c->per_sets[e];
        s.def[e] = c->def_sets[e];
    }
    return s;
}

DerivedStats character_derive(const Character *c)
{
    StatSets s = character_stat_sets(c);
    const UnitTemplate *t = c->class_template;
    double level = c->level;
    DerivedStats d;

    d.life = s.stat[0] + ceil(t->life + t->life_growth * level);
    d.strength = s.stat[1] + ceil(t->strength + t->strength_growth * level);
    d.magic = s.stat[2] + ceil(t->magic + t->magic_growth * level);
    d.speed = s.stat[3] + ceil(t->speed + t->speed_growth * level);
    /* Focus takes no growth term: the class value is used as it stands. */
    d.focus = s.stat[4] + ceil(t->focus);

    double baseline = ceil(25 + 5 * level);
    for (int32_t e = 0; e < SONNY_ELEMENTS; e++) {
        d.per[e] = s.per[e] + baseline;
        d.def[e] = s.def[e] + baseline;
    }
    return d;
}

double rewards_enemy_rating(const int32_t *enemy_levels, int32_t count)
{
    if (count <= 0)
        return 0;
    double total = 0;      /* EnemyXP  */
    double scale = 1;      /* EnemyXP2 */
    for (int32_t i = 0; i < count; i++) {
        total += enemy_levels[i];
        scale += 0.2;
    }
    return total / count * scale;
}

int32_t rewards_money(double enemy_rating, Rng *rng)
{
    /* Krin.EuroConstant */
    const double euro_constant = 5;
    double roll = rng ? rng_below(rng, 30) : 0;
    return (int32_t)floor(enemy_rating * euro_constant
                          * ((85 + roll) / 100.0) + 0.5);
}

double character_xp_gain(double enemy_rating, int32_t player_level)
{
    double diff = 1 + (enemy_rating - player_level) * 0.15;
    if (diff < 0)
        diff = 0;
    if (diff > 3)
        diff = 3;
    /* The divisor's exponent itself grows with level, so late levels are much
       slower than a flat curve would make them. */
    return diff * (100 / (1 + pow(player_level,
                                  0.6 + 0.3 * (player_level / 100.0))));
}

int32_t character_award_xp(Character *c, double amount)
{
    c->xp += amount;
    if (c->xp < 100)
        return 0;
    /* The original discards the overflow and stops filling, so a single
       battle grants at most one level. */
    c->xp = 0;
    c->level++;
    return 1;
}

int character_can_equip(const Character *c, const ItemDef *item)
{
    if (!item || item->slot == 0)
        return 0;
    if (item->level_req > c->level)
        return 0;
    /* class_req 0 means any class; -1 marks a non-equippable tool. */
    if (item->class_req < 0)
        return 0;
    if (item->class_req > 0 && c->class_template
        && item->class_req != c->class_template->id)
        return 0;
    return 1;
}
