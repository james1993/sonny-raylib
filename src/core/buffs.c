#include <math.h>
#include <stdio.h>
#include <string.h>
#include "buffs.h"

const BuffDef *buff_find(const BuffDef *lib, int32_t lib_count, const char *key)
{
    for (int32_t i = 0; i < lib_count; i++)
        if (strcmp(lib[i].key, key) == 0)
            return &lib[i];
    return NULL;
}

void buff_apply(Unit *target, const BuffDef *b, int32_t iftbc,
                const Unit *caster, double debuffValue)
{
    target->STUN += iftbc * b->stun;
    target->REFLECT += iftbc * b->reflect;
    target->SHIELDCOUNTER += iftbc * b->shield;

    /* The original's condition is `if (iftbc)`, which is true for removal too
       (iftbc == -1), so a shield is re-added on expiry and then usually zeroed
       by the SHIELDCOUNTER check below. Kept exactly: with two shields active
       the leftover survives, and that is what the original does. */
    if (iftbc) {
        target->SHIELD += b->shield;
    } else if (b->shield == 1000000) {
        target->SHIELD -= b->shield;
        if (target->SHIELD < 0)
            target->SHIELD = 0;
    }
    if (target->SHIELDCOUNTER == 0)
        target->SHIELD = 0;

    if (b->filter >= 0 && b->filter < SONNY_FILTERS)
        target->FILTERSBUFFARRAY[b->filter] += iftbc;

    /* On application, claim the first free slot. */
    int32_t slot = -1;
    if (iftbc == 1) {
        for (int32_t i = 0; i < SONNY_MAX_BUFFS; i++) {
            if (slot < 0 && target->BUFFARRAYK[i].CD == 0) {
                target->BUFFARRAYK[i].CD = b->duration;
                snprintf(target->BUFFARRAYK[i].buffId, SONNY_NAME_LEN, "%s", b->key);
                slot = i;
            }
        }
    }

    target->SSWITCH += iftbc * b->sswitch;
    for (int32_t s = 0; s < 12; s++)
        target->changeArray[s] += iftbc * b->change[s];
    target->DOTTICKERARRAY[9] += iftbc * b->focus_drain;

    /* Only the buff's own element carries its per-element and DoT effects. */
    for (int32_t i = 0; i < SONNY_ELEMENTS; i++) {
        if (i != b->element)
            continue;

        target->changeArrayEP[i] += iftbc * b->per_flat;
        target->changeArrayEP2[i] += iftbc * b->per_pct;
        target->changeArrayED[i] += iftbc * b->def_flat;
        target->changeArrayED2[i] += iftbc * b->def_pct;

        double gHecker = b->dot_flat + b->dot_strength + b->dot_magic + b->dot_speed;
        double amount;
        if (iftbc == 1) {
            amount = ceil(b->dot_flat
                          + b->dot_strength * (caster ? caster->STRENGTHU : 0)
                          + b->dot_magic * (caster ? caster->MAGICU : 0)
                          + iftbc * b->dot_speed * (caster ? caster->SPEEDU : 0));
            if (slot >= 0) {
                target->BUFFARRAYK[slot].buffValue = 0;
                target->BUFFARRAYK[slot].buffValue += amount;
            }
        } else {
            amount = -debuffValue;
        }

        /* A net-negative total is healing over turns, which bypasses the
           per-element defense scaling by living in slot 8. */
        if (gHecker < 0)
            target->DOTTICKERARRAY[8] += amount;
        else
            target->DOTTICKERARRAY[i] += amount;
    }
}

void unit_apply_changes(Unit *u)
{
    const double *epy = u->changeArray;

    u->STUNP = u->STUN;

    u->STRENGTHU = ceil((u->STRENGTH + epy[0]) * (1 + epy[1]));
    u->MAGICU = ceil((u->MAGIC + epy[2]) * (1 + epy[3]));
    u->SPEEDU = ceil((u->SPEED + epy[4]) * (1 + epy[5]));

    double liferAdder = ceil((u->LIFE + epy[6]) * (1 + epy[7]) - u->LIFEU);
    int was_alive = (u->LIFEN > 0);
    double t_k_factor = (u->LIFEU != 0) ? (double)u->LIFEN / u->LIFEU : 0;

    u->LIFEU += (int32_t)liferAdder;
    if (liferAdder > 0) {
        u->LIFEN += (int32_t)liferAdder;
    } else {
        /* Math.round: AS rounds .5 up, which is not C's round-half-away. */
        u->LIFEN = (int32_t)floor(t_k_factor * u->LIFEU + 0.5);
    }
    if (u->LIFEN <= 0 && was_alive)
        u->LIFEN = 1;
    if (u->LIFEN > u->LIFEU)
        u->LIFEN = u->LIFEU;

    u->DMG = epy[8];
    u->DMG2 = epy[9];
    u->IDMG = epy[10];
    u->IDMG2 = epy[11];

    for (int32_t i = 0; i < SONNY_ELEMENTS; i++) {
        u->PERU[i] = u->PER[i] + u->changeArrayEP[i];
        u->PERU[i] *= u->changeArrayEP2[i] + 1;
    }
    for (int32_t i = 0; i < SONNY_ELEMENTS; i++) {
        u->DEFU[i] = u->DEF[i] + u->changeArrayED[i];
        u->DEFU[i] *= u->changeArrayED2[i] + 1;
    }
}

int32_t buff_dispel(Unit *target, int32_t count, uint32_t elements,
                    int32_t nature, const BuffDef *lib, int32_t lib_count)
{
    int32_t gone = 0;
    for (int32_t i = 0; i < SONNY_MAX_BUFFS && count > 0; i++) {
        BuffSlot *slot = &target->BUFFARRAYK[i];
        if (slot->CD <= 0)
            continue;
        const BuffDef *def = buff_find(lib, lib_count, slot->buffId);
        if (!def || !(elements & (1u << def->element))
            || def->nature != nature)
            continue;
        count--;
        gone++;
        slot->CD = 0;
        buff_apply(target, def, -1, NULL, slot->buffValue);
    }
    return gone;
}

void buff_land(Unit *target, const BuffDef *b, const Unit *caster)
{
    int already = 0;
    if (b->unique) {
        for (int32_t i = 0; i < SONNY_MAX_BUFFS; i++) {
            BuffSlot *slot = &target->BUFFARRAYK[i];
            if (slot->CD != 0 && strcmp(slot->buffId, b->key) == 0) {
                slot->CD = b->duration;
                already = 1;
            }
        }
    }
    if (!already)
        buff_apply(target, b, 1, caster, 0);
}

TickResult buff_tick(Unit *u, const BuffDef *lib, int32_t lib_count)
{
    TickResult out;
    out.died = 0;
    out.shielded = 0;
    /* colorToBe starts at Physical and takes the element of whichever slot
       contributed most. */
    out.element = 0;

    double totalFocus = u->DOTTICKERARRAY[9];
    double totalDmg = u->DOTTICKERARRAY[8];

    double most = 0;
    for (int32_t e = 0; e < SONNY_ELEMENTS; e++) {
        double scaled = ceil((1 + u->IDMG2)
                             * (u->DOTTICKERARRAY[e]
                                * ((25 + u->plevel * 5) / u->DEFU[e])));
        totalDmg += scaled;
        if (scaled > most) {
            most = scaled;
            out.element = e;
        }
    }

    /* Durations tick after this turn's damage is counted, so a buff's final
       turn still hits; expired buffs are then unwound. */
    int32_t expired[SONNY_MAX_BUFFS];
    int32_t expired_count = 0;
    for (int32_t b = 0; b < SONNY_MAX_BUFFS; b++) {
        if (u->BUFFARRAYK[b].CD > 0) {
            u->BUFFARRAYK[b].CD--;
            if (u->BUFFARRAYK[b].CD == 0)
                expired[expired_count++] = b;
        }
    }
    for (int32_t i = 0; i < expired_count; i++) {
        const BuffDef *def = buff_find(lib, lib_count,
                                       u->BUFFARRAYK[expired[i]].buffId);
        if (def)
            buff_apply(u, def, -1, NULL, u->BUFFARRAYK[expired[i]].buffValue);
    }

    if (u->SSWITCH > 0)
        totalDmg *= -1;

    double differenceForSH = 0;
    if (u->SHIELD > 0 && totalDmg > 0)
        differenceForSH = u->SHIELD - totalDmg;

    if (differenceForSH > 0) {
        u->SHIELD -= unit_int(totalDmg);
        out.shielded = 1;
    } else {
        /* Note: the shield is destroyed without reducing the damage. The
           original subtracts it from a leftover display global instead of
           from totalDmgCalcZ, so the full amount still lands. */
        if (totalDmg > 0)
            u->SHIELD = 0;
        u->LIFEN -= unit_int(totalDmg);
    }

    u->FOCUSN -= unit_int(totalFocus);
    if (u->FOCUSN > u->FOCUSU)
        u->FOCUSN = u->FOCUSU;
    if (u->FOCUSN < 0)
        u->FOCUSN = 0;

    if (u->LIFEN <= 0) {
        u->LIFEN = 0;
        u->FOCUSN = 0;
        u->active = 0;
        out.died = 1;
    }

    unit_apply_changes(u);

    out.total_damage = totalDmg;
    out.focus_drained = totalFocus;
    return out;
}
