#include <math.h>
#include "character.h"

int32_t character_stat_points(const Character *c)
{
    return c->level - 1;
}

int32_t character_skill_points(const Character *c)
{
    return c->level - 1;
}

StatSets character_stat_sets(const Character *c)
{
    StatSets s;
    for (int32_t i = 0; i < SONNY_STATS; i++)
        s.stat[i] = c->spent[i];
    for (int32_t e = 0; e < SONNY_ELEMENTS; e++) {
        s.per[e] = 0;
        s.def[e] = 0;
    }

    for (int32_t slot = 0; slot < SONNY_EQUIP_SLOTS; slot++) {
        const ItemDef *item = item_by_id(c->equip[slot]);
        if (!item)
            continue;
        for (int32_t i = 0; i < SONNY_STATS; i++)
            s.stat[i] += item->stat[i];
        for (int32_t e = 0; e < SONNY_ELEMENTS; e++) {
            s.per[e] += item->per[e];
            s.def[e] += item->def[e];
        }
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
