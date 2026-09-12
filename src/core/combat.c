#include <string.h>
#include "combat.h"

/* Stat indices match the field order of Stats so data files can address them
   numerically. Keep in sync with Stats. */
static int32_t stat_field(const Stats *s, int32_t index)
{
    switch (index) {
    case 0: return s->strength;
    case 1: return s->instinct;
    case 2: return s->speed;
    case 3: return s->defense;
    case 4: return s->hp_max;
    case 5: return s->focus_max;
    default: return 0;
    }
}

int32_t combat_stat(const Combat *c, int32_t unit, int32_t stat_index)
{
    const Combatant *u = &c->units[unit];
    int32_t value = stat_field(&u->base, stat_index);
    int32_t percent = 0;

    for (int32_t i = 0; i < u->buff_count; i++) {
        const Buff *b = &u->buffs[i];
        if (b->kind != BUFF_STAT_MOD || b->stat_index != stat_index)
            continue;
        if (b->is_percent)
            percent += b->magnitude;
        else
            value += b->magnitude;
    }
    value += value * percent / 100;
    return value < 0 ? 0 : value;
}

void combat_begin(Combat *c, uint64_t seed)
{
    rng_seed(&c->rng, seed);
    c->round = 1;
    c->turn_index = 0;
    for (int32_t i = 0; i < c->unit_count; i++) {
        Combatant *u = &c->units[i];
        u->alive = 1;
        u->hp = u->base.hp_max;
        u->focus = 0;
        u->buff_count = 0;
        memset(u->ability_cooldown, 0, sizeof(u->ability_cooldown));
    }
    combat_build_turn_order(c);
    c->phase = PHASE_START_OF_ROUND;
}

void combat_build_turn_order(Combat *c)
{
    for (int32_t i = 0; i < c->unit_count; i++)
        c->turn_order[i] = i;

    /* Insertion sort: stable, so equal speeds keep roster order. */
    for (int32_t i = 1; i < c->unit_count; i++) {
        int32_t key = c->turn_order[i];
        int32_t key_spd = combat_stat(c, key, 2);
        int32_t j = i - 1;
        while (j >= 0 && combat_stat(c, c->turn_order[j], 2) < key_spd) {
            c->turn_order[j + 1] = c->turn_order[j];
            j--;
        }
        c->turn_order[j + 1] = key;
    }
    c->turn_index = 0;
}

int32_t combat_current_unit(const Combat *c)
{
    if (c->turn_index < 0 || c->turn_index >= c->unit_count)
        return -1;
    return c->turn_order[c->turn_index];
}

static void remove_buff(Combatant *u, int32_t i)
{
    u->buffs[i] = u->buffs[u->buff_count - 1];
    u->buff_count--;
}

int32_t combat_apply_damage(Combat *c, int32_t target, int32_t amount, DamageKind kind)
{
    Combatant *u = &c->units[target];
    if (!u->alive || amount <= 0)
        return 0;

    if (kind != DMG_TRUE) {
        int32_t def = combat_stat(c, target, 3);
        /* Placeholder mitigation curve -- replaced by the extracted formula. */
        amount -= def;
        if (amount < 1)
            amount = 1;
    }

    /* Shields soak first, oldest shield first, and expire when depleted. */
    for (int32_t i = 0; i < u->buff_count && amount > 0; ) {
        if (u->buffs[i].kind != BUFF_SHIELD) {
            i++;
            continue;
        }
        int32_t soak = u->buffs[i].magnitude < amount ? u->buffs[i].magnitude : amount;
        u->buffs[i].magnitude -= soak;
        amount -= soak;
        if (u->buffs[i].magnitude <= 0)
            remove_buff(u, i);
        else
            i++;
    }

    int32_t before = u->hp;
    u->hp -= amount;
    if (u->hp <= 0) {
        u->hp = 0;
        u->alive = 0;
    }
    return before - u->hp;
}

int32_t combat_apply_heal(Combat *c, int32_t target, int32_t amount)
{
    Combatant *u = &c->units[target];
    if (!u->alive || amount <= 0)
        return 0;
    int32_t before = u->hp;
    u->hp += amount;
    if (u->hp > u->base.hp_max)
        u->hp = u->base.hp_max;
    return u->hp - before;
}

void combat_add_buff(Combat *c, int32_t target, const Buff *b)
{
    Combatant *u = &c->units[target];

    /* Same ability from the same source refreshes instead of stacking. */
    for (int32_t i = 0; i < u->buff_count; i++) {
        if (u->buffs[i].ability_id == b->ability_id && u->buffs[i].source == b->source) {
            u->buffs[i] = *b;
            return;
        }
    }
    if (u->buff_count < SONNY_MAX_BUFFS)
        u->buffs[u->buff_count++] = *b;
}

void combat_tick_buffs(Combat *c, int32_t unit)
{
    Combatant *u = &c->units[unit];

    for (int32_t i = 0; i < u->buff_count; ) {
        Buff *b = &u->buffs[i];
        switch (b->kind) {
        case BUFF_DOT:
            combat_apply_damage(c, unit, b->magnitude, DMG_TRUE);
            break;
        case BUFF_HOT:
            combat_apply_heal(c, unit, b->magnitude);
            break;
        default:
            break;
        }
        if (b->turns_left > 0)
            b->turns_left--;
        if (b->turns_left == 0 && b->kind != BUFF_SHIELD)
            remove_buff(u, i);
        else
            i++;
    }
}

int combat_side_alive(const Combat *c, Side s)
{
    for (int32_t i = 0; i < c->unit_count; i++)
        if (c->units[i].side == s && c->units[i].alive)
            return 1;
    return 0;
}

static int is_stunned(const Combatant *u)
{
    for (int32_t i = 0; i < u->buff_count; i++)
        if (u->buffs[i].kind == BUFF_STUN)
            return 1;
    return 0;
}

void combat_advance_turn(Combat *c)
{
    int32_t current = combat_current_unit(c);
    if (current >= 0) {
        combat_tick_buffs(c, current);
        for (int32_t i = 0; i < c->units[current].ability_count; i++)
            if (c->units[current].ability_cooldown[i] > 0)
                c->units[current].ability_cooldown[i]--;
    }

    for (;;) {
        if (!combat_side_alive(c, SIDE_ENEMY)) {
            c->phase = PHASE_VICTORY;
            return;
        }
        if (!combat_side_alive(c, SIDE_PLAYER)) {
            c->phase = PHASE_DEFEAT;
            return;
        }

        c->turn_index++;
        if (c->turn_index >= c->unit_count) {
            c->round++;
            combat_build_turn_order(c);
            c->phase = PHASE_START_OF_ROUND;
        }

        int32_t next = combat_current_unit(c);
        if (next < 0)
            return;
        const Combatant *u = &c->units[next];
        if (!u->alive)
            continue;
        if (is_stunned(u)) {
            combat_tick_buffs(c, next);
            continue;
        }
        c->phase = (u->ai_script_id < 0) ? PHASE_AWAIT_ACTION : PHASE_RESOLVING;
        return;
    }
}
