/* Turn-based combat core for the Sonny reimplementation.
 *
 * This layer is pure C with no raylib dependency so it can be unit-tested and
 * fuzz-replayed headlessly. Rendering and input live in src/platform.
 *
 * NOTHING in here hardcodes balance numbers. Abilities, enemies and their
 * scripted AI are loaded as data (see data/ and src/core/gamedata.h) so the
 * exact values lifted from the original game are the single source of truth.
 */
#ifndef SONNY_COMBAT_H
#define SONNY_COMBAT_H

#include <stdint.h>
#include "rng.h"

#define SONNY_MAX_COMBATANTS 8   /* 4 per side in the original's largest fights */
#define SONNY_MAX_BUFFS      12
#define SONNY_MAX_ABILITIES  16
#define SONNY_NAME_LEN       32

typedef enum {
    SIDE_PLAYER = 0,
    SIDE_ENEMY  = 1
} Side;

/* Primary stats. Names follow the original's terminology; the derived formulas
   that turn these into damage/healing are filled in from extracted script. */
typedef struct {
    int32_t strength;
    int32_t instinct;
    int32_t speed;
    int32_t defense;
    int32_t hp_max;
    int32_t focus_max;
} Stats;

typedef enum {
    DMG_PHYSICAL = 0,
    DMG_MAGICAL  = 1,   /* "special"/instinct-scaled damage */
    DMG_TRUE     = 2    /* ignores mitigation */
} DamageKind;

/* A status effect instance on a combatant. Buffs, debuffs, damage-over-time,
   shields and stuns are all represented here; behaviour comes from `kind`. */
typedef enum {
    BUFF_NONE = 0,
    BUFF_STAT_MOD,      /* flat or percent change to a stat */
    BUFF_DOT,           /* damage each tick */
    BUFF_HOT,           /* heal each tick */
    BUFF_SHIELD,        /* absorbs incoming damage */
    BUFF_STUN,          /* loses turns */
    BUFF_TAUNT,
    BUFF_REFLECT,
    BUFF_KIND_COUNT
} BuffKind;

typedef struct {
    BuffKind kind;
    int32_t  ability_id;    /* source ability, for stacking rules */
    int32_t  turns_left;
    int32_t  magnitude;     /* meaning depends on kind */
    int32_t  stat_index;    /* for BUFF_STAT_MOD */
    int32_t  is_percent;
    int32_t  source;        /* combatant index that applied it */
} Buff;

typedef struct {
    char    name[SONNY_NAME_LEN];
    Side    side;
    int32_t alive;
    Stats   base;           /* unmodified stats from data/level-up */
    int32_t hp;
    int32_t focus;
    int32_t ability_ids[SONNY_MAX_ABILITIES];
    int32_t ability_cooldown[SONNY_MAX_ABILITIES];
    int32_t ability_count;
    Buff    buffs[SONNY_MAX_BUFFS];
    int32_t buff_count;
    int32_t ai_script_id;   /* -1 for player-controlled */
} Combatant;

typedef enum {
    PHASE_START_OF_ROUND,
    PHASE_AWAIT_ACTION,     /* waiting on player input */
    PHASE_RESOLVING,
    PHASE_END_OF_ROUND,
    PHASE_VICTORY,
    PHASE_DEFEAT
} CombatPhase;

typedef struct {
    Combatant   units[SONNY_MAX_COMBATANTS];
    int32_t     unit_count;
    int32_t     turn_order[SONNY_MAX_COMBATANTS];
    int32_t     turn_index;
    int32_t     round;
    CombatPhase phase;
    Rng         rng;
} Combat;

/* Effective stat after buffs are applied, in the original's order of
   operations (flat modifiers first, then percentages). */
int32_t combat_stat(const Combat *c, int32_t unit, int32_t stat_index);

void combat_begin(Combat *c, uint64_t seed);
/* Sort turn order by effective speed; ties broken deterministically by index. */
void combat_build_turn_order(Combat *c);
int32_t combat_current_unit(const Combat *c);

/* Apply damage through shields and mitigation. Returns HP actually removed. */
int32_t combat_apply_damage(Combat *c, int32_t target, int32_t amount, DamageKind kind);
int32_t combat_apply_heal(Combat *c, int32_t target, int32_t amount);
void    combat_add_buff(Combat *c, int32_t target, const Buff *b);
void    combat_tick_buffs(Combat *c, int32_t unit);

/* Advance to the next living unit, ticking end-of-turn effects. Sets phase to
   PHASE_AWAIT_ACTION when a player-controlled unit is up. */
void combat_advance_turn(Combat *c);
int  combat_side_alive(const Combat *c, Side s);

#endif
