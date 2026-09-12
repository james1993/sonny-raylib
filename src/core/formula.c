#include <math.h>
#include "formula.h"

double formula_percalk(const Unit *caster, int32_t element)
{
    return (caster->PERU[element] + caster->SPEEDU / 2)
           / (25 + 5 * (double)caster->plevel) * 25;
}

double formula_defcalk(const Unit *caster, const Unit *target, int32_t element)
{
    double d = target->DEFU[element] / (25 + 5 * (double)target->plevel) * 25
               - (caster->plevel - target->plevel) * 3.0;
    if (d <= 0)
        d = 1;
    return d;
}

int formula_pierce_check(Rng *rng, double PERCALK, double DEFCALK,
                         const AbilityCoefs *a, int32_t *roll_out)
{
    int32_t roll = rng_krrr(rng);
    if (roll_out)
        *roll_out = roll;
    return roll < (PERCALK + a->hit_add) * a->hit_coef / DEFCALK * 15;
}

DamageResult formula_full_damage(Rng *rng, const Unit *caster, const Unit *target,
                                 const AbilityCoefs *a)
{
    DamageResult r;
    r.PERCALK = formula_percalk(caster, a->element);
    r.DEFCALK = formula_defcalk(caster, target, a->element);

    r.raw = (caster->STRENGTHU + a->strength_add) * a->strength_coef
          + (caster->MAGICU + a->magic_add) * a->magic_coef
          + (caster->SPEEDU + a->speed_add) * a->speed_coef
          + caster->FOCUSN * a->focus_coef
          + a->flat_damage + caster->DMG + target->IDMG;

    r.pierced = formula_pierce_check(rng, r.PERCALK, r.DEFCALK, a, &r.roll);
    if (r.pierced) {
        r.mitigation = 1 - (r.DEFCALK - r.PERCALK) / 100;
    } else {
        r.mitigation = 1 - r.DEFCALK * (1 - r.PERCALK / 200) / 100;
    }
    if (r.mitigation < 0)
        r.mitigation = 0;

    double final = ceil(r.raw * a->damage_coef * r.mitigation
                        * (1 + caster->DMG2) * (1 + target->IDMG2));
    if (final <= 0)
        final = 1;
    r.damage = (int32_t)final;
    return r;
}

int32_t formula_apply_damage(Unit *target, int32_t damage, int32_t *absorbed_out)
{
    int32_t absorbed = 0;
    int32_t before = target->LIFEN;

    /* Note the original's exact shield condition: a shield equal to the
       incoming damage does NOT take the absorbing branch. */
    int32_t difference = (target->SHIELD > 0) ? target->SHIELD - damage : 0;
    if (difference > 0) {
        target->SHIELD -= damage;
        absorbed = damage;
        damage = 0;
    } else {
        absorbed = target->SHIELD;
        damage -= target->SHIELD;
        target->SHIELD = 0;

        if (target->SSWITCH == 0) {
            target->LIFEN -= damage;
            if (target->LIFEN <= 0) {
                target->LIFEN = 0;
                target->FOCUSN = 0;
                target->active = 0;
            }
        } else {
            target->LIFEN += damage;
            if (target->LIFEN > target->LIFEU)
                target->LIFEN = target->LIFEU;
        }
    }
    if (absorbed_out)
        *absorbed_out = absorbed;
    return before - target->LIFEN;
}
