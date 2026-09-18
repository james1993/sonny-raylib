/* Combat math, ported from executeMove()/perScript() in frame 62.
 *
 * The original computes in ActionScript Numbers (IEEE doubles) and rounds only
 * at the very end with Math.ceil, so every intermediate here is a double and
 * the rounding happens in exactly the same place. Do not "simplify" these
 * expressions: the grouping is what reproduces the original's results.
 */
#ifndef SONNY_FORMULA_H
#define SONNY_FORMULA_H

#include "unit.h"
#include "rng.h"

/* The coefficients an ability contributes to the damage formula. These are the
   KRINABILITYB ("B-table") slots, named by the role the engine gives them. */
typedef struct {
    int32_t element;        /* index into PERU/DEFU */
    double  strength_add;   /* B[1]  */
    double  strength_coef;  /* B[2]  */
    double  magic_add;      /* B[3]  */
    double  magic_coef;     /* B[4]  */
    double  speed_add;      /* B[5]  */
    double  speed_coef;     /* B[6]  */
    double  hit_add;        /* B[7]  */
    double  hit_coef;       /* B[8]  */
    double  flat_damage;    /* B[9]  */
    double  damage_coef;    /* B[10] */
    double  focus_coef;     /* B[11] */
} AbilityCoefs;

typedef struct {
    double  PERCALK;        /* attacker's effective piercing */
    double  DEFCALK;        /* target's effective defense */
    int32_t roll;           /* the KRS value consumed */
    int32_t pierced;        /* roll beat the piercing check */
    double  raw;            /* damage before mitigation */
    double  mitigation;     /* the coefficient mitigation applied */
    int32_t damage;         /* final damage, after ceil and the >=1 floor */
} DamageResult;

double formula_percalk(const Unit *caster, int32_t element);
double formula_defcalk(const Unit *caster, const Unit *target, int32_t element);

/* perScript(): consumes one RNG value and reports whether the hit pierced. */
int formula_pierce_check(Rng *rng, double PERCALK, double DEFCALK,
                         const AbilityCoefs *a, int32_t *roll_out);

/* The "Full Damage" branch of executeMove, up to but not including applying
   the result to the target. */
DamageResult formula_full_damage(Rng *rng, const Unit *caster, const Unit *target,
                                 const AbilityCoefs *a);

/* The "Heal" branch of executeMove. The damage formula is reused without the
 * attacker's flat bonus or the target's incoming-damage bonus, the pierce check
 * runs against a fixed defense of 25, and a pierced heal is worth 1.5x. A heal
 * that lands on a unit whose SSWITCH is set becomes damage instead. */
DamageResult formula_heal(Rng *rng, const Unit *caster, const Unit *target,
                          const AbilityCoefs *a);

/* Applies a heal result as executeMove does: healing capped at max health, or
 * damage when the target's SSWITCH is set. Returns health gained (negative
 * when the heal was flipped into damage). */
int32_t formula_apply_heal(Unit *target, int32_t amount);

/* The "Focus" branch: the ability's flat damage slot is focus granted. */
int32_t formula_apply_focus(Unit *target, const AbilityCoefs *a);

/* Applies a damage result to the target the way executeMove does: shields
   absorb first, SSWITCH turns the remainder into healing, and death zeroes
   focus. Returns the health actually lost (negative when healed). */
/* `landed_out`, when given, is the original's DamageOutputKrinFinal: the
   damage left once the shield has taken its part, which is the number the
   fight floats over the target and the amount its life moves by. -1 when the
   shield swallowed the blow whole, which is where the original floats the
   word "shield" instead. It is not clamped to the life the target had, so an
   overkill reads as all of it, the way the original's does. */
int32_t formula_apply_damage(Unit *target, int32_t damage, int32_t *absorbed_out,
                             int32_t *landed_out);

#endif
