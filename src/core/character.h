/* Sonny himself: level, spent points, equipment, and the stats those produce.
 *
 * The original keeps this on the `Krin` object and recomputes it whenever the
 * character screen changes something (DefineSprite_1503). A stat is
 *
 *     StatSets[i] + ceil(classBase + classGrowth * Level)
 *
 * where StatSets is the sum of every equipped item's bonus plus the points
 * spent by hand, and the class contributes its own base and per-level growth.
 * Piercing and defense work the same way over a level-scaled baseline of
 * 25 + 5 * Level. Health is carried into battle multiplied by eight.
 *
 * This differs from how enemies are built (krinAddNewUnit), which scales
 * linearly with no rounding -- see battle_place_enemy.
 */
#ifndef SONNY_CHARACTER_H
#define SONNY_CHARACTER_H

#include "rng.h"
#include "unit.h"
#include "../gen/gamedata.h"

#define SONNY_EQUIP_SLOTS 7
#define SONNY_STATS       5    /* health, strength, magic, speed, focus */
#define SONNY_MOVE_SLOTS  8    /* Krin.moveMatrix -- the equipped bar */
#define SONNY_TALENT_MAX  40   /* the arrays the original sizes at 38-40 */

typedef struct {
    int32_t level;
    double  xp;                     /* progress toward the next level */
    int32_t equip[SONNY_EQUIP_SLOTS];   /* item ids, 0 = empty */
    double  spent[SONNY_STATS];     /* points assigned by hand */
    const UnitTemplate *class_template;

    /* Talents. rank is Krin.talentMainArray; skill_adder mirrors
       Krin.skillAdderMatrix (the ability id each learned node currently
       grants); buff_adder mirrors Krin.buffAdderMatrix (the passive buff key
       each learned passive node grants, name plus rank). */
    int32_t rank[SONNY_TALENT_MAX];
    int32_t skill_adder[SONNY_TALENT_MAX];
    char    buff_adder[SONNY_TALENT_MAX][SONNY_NAME_LEN];
    int32_t spent_skill_points;

    /* The eight equipped abilities (Krin.moveMatrix). */
    int32_t move_matrix[SONNY_MOVE_SLOTS];
} Character;

/* Set a character up the way the original does on a new game: level 1, the
   two starting abilities in the first two bar slots, and the starting stat
   bonus Krin.StatSets0 = [0,5,0,3,0]. */
void character_new(Character *c, int32_t class_id);

/* The level needed for this node's next rank:
   LEVELMIN + LEVELSCALE * currentRank. */
int32_t character_talent_next_level(const Character *c, int32_t node);

/* Why a talent cannot be learned, matching the original's four checks in
   order. OK means it can. */
typedef enum {
    TALENT_OK = 0,
    TALENT_NO_POINTS,
    TALENT_MAX_RANK,
    TALENT_LEVEL_TOO_LOW,
    TALENT_MISSING_PREREQ
} TalentError;

TalentError character_can_learn(const Character *c, int32_t node);

/* Spend a point on a node. Upgrades the bar slot already holding this node's
   previous rank, and for a passive records the rank-suffixed buff key.
   Returns TALENT_OK if the point was spent. */
TalentError character_learn(Character *c, int32_t node);

/* Points left to spend: (level - 1) minus what has been spent. */
int32_t character_unspent_skill_points(const Character *c);

/* Every ability the character knows (Krin.moveMatrix2): the two starting
   abilities followed by each learned active node's current rank. Writes up to
   `max` ids and returns how many. */
int32_t character_known_abilities(const Character *c, int32_t *out, int32_t max);

/* The passive buff keys in play, for applying at the start of a battle.
   Returns how many were written. */
int32_t character_passive_buffs(const Character *c, const char **out,
                                int32_t max);

/* Totals from equipment plus spent points: Krin.StatSets / PerSets / DefSets. */
typedef struct {
    double stat[SONNY_STATS];
    double per[SONNY_ELEMENTS];
    double def[SONNY_ELEMENTS];
} StatSets;

/* Points available to spend: Level - 1 of each, as the respec button sets. */
int32_t character_stat_points(const Character *c);
int32_t character_skill_points(const Character *c);

StatSets character_stat_sets(const Character *c);

/* Derived stats, before the health multiplier battles apply. */
typedef struct {
    double life, strength, magic, speed, focus;
    double per[SONNY_ELEMENTS];
    double def[SONNY_ELEMENTS];
} DerivedStats;

DerivedStats character_derive(const Character *c);

/* The battle's enemy rating, as frame 212 accumulates it while the fight is
 * being set up: the average enemy level, scaled up by a fifth per enemy.
 *
 *     EnemyXP / EnemyXP3 * EnemyXP2, with EnemyXP2 starting at 1
 */
double rewards_enemy_rating(const int32_t *enemy_levels, int32_t count);

/* expWorkOut: the percentage of a level the fight is worth. The enemy-to-player
   level gap scales it, clamped to [0, 3], over a divisor that grows with
   level. */
double character_xp_gain(double enemy_rating, int32_t player_level);

/* Euros awarded: the rating times the euro constant, times a random 85-114%.
   Consumes one RNG draw, as the original's random(30) does. */
int32_t rewards_money(double enemy_rating, Rng *rng);

/* Award XP.
 *
 * Careful: the original's XP is a percentage, and on reaching 100 it sets exp
 * back to 0 rather than carrying the remainder, then stops the fill -- so one
 * battle can never grant two levels however overqualified the fight was.
 * Returns 1 if a level was gained. */
int32_t character_award_xp(Character *c, double amount);

/* Can this item be equipped: class and level requirements, as the item
   registrar records them. */
int character_can_equip(const Character *c, const ItemDef *item);

#endif
