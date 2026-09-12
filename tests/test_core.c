/* Headless tests for the combat core. These currently pin down structural
   behaviour (turn order, shields, DoT, win/lose detection), not balance --
   balance tests get written once the real values are extracted. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/core/combat.h"

static int32_t add_unit(Combat *c, const char *name, Side side, int32_t hp,
                        int32_t spd, int32_t def, int32_t ai)
{
    int32_t i = c->unit_count++;
    Combatant *u = &c->units[i];
    memset(u, 0, sizeof(*u));
    snprintf(u->name, SONNY_NAME_LEN, "%s", name);
    u->side = side;
    u->base.hp_max = hp;
    u->base.speed = spd;
    u->base.defense = def;
    u->ai_script_id = ai;
    return i;
}

static void test_turn_order_by_speed(void)
{
    Combat c = {0};
    add_unit(&c, "slow", SIDE_PLAYER, 100, 5, 0, -1);
    add_unit(&c, "fast", SIDE_ENEMY, 100, 20, 0, 0);
    add_unit(&c, "mid", SIDE_PLAYER, 100, 10, 0, -1);
    combat_begin(&c, 1234);

    assert(strcmp(c.units[c.turn_order[0]].name, "fast") == 0);
    assert(strcmp(c.units[c.turn_order[1]].name, "mid") == 0);
    assert(strcmp(c.units[c.turn_order[2]].name, "slow") == 0);
}

static void test_speed_buff_reorders_next_round(void)
{
    Combat c = {0};
    add_unit(&c, "a", SIDE_PLAYER, 100, 10, 0, -1);
    add_unit(&c, "b", SIDE_ENEMY, 100, 12, 0, 0);
    combat_begin(&c, 1);
    assert(strcmp(c.units[c.turn_order[0]].name, "b") == 0);

    Buff haste = {0};
    haste.kind = BUFF_STAT_MOD;
    haste.stat_index = 2;
    haste.magnitude = 50;
    haste.is_percent = 1;
    haste.turns_left = 3;
    haste.ability_id = 7;
    combat_add_buff(&c, 0, &haste);

    combat_build_turn_order(&c);
    assert(combat_stat(&c, 0, 2) == 15);
    assert(strcmp(c.units[c.turn_order[0]].name, "a") == 0);
}

static void test_shield_absorbs_then_expires(void)
{
    Combat c = {0};
    add_unit(&c, "tank", SIDE_PLAYER, 100, 10, 0, -1);
    combat_begin(&c, 2);

    Buff shield = {0};
    shield.kind = BUFF_SHIELD;
    shield.magnitude = 30;
    shield.turns_left = 5;
    shield.ability_id = 3;
    combat_add_buff(&c, 0, &shield);

    assert(combat_apply_damage(&c, 0, 20, DMG_PHYSICAL) == 0);
    assert(c.units[0].hp == 100);
    assert(combat_apply_damage(&c, 0, 25, DMG_PHYSICAL) == 15);
    assert(c.units[0].hp == 85);
    assert(c.units[0].buff_count == 0);
}

static void test_defense_mitigates_but_leaves_one(void)
{
    Combat c = {0};
    add_unit(&c, "wall", SIDE_PLAYER, 50, 10, 40, -1);
    combat_begin(&c, 3);

    assert(combat_apply_damage(&c, 0, 10, DMG_PHYSICAL) == 1);
    assert(combat_apply_damage(&c, 0, 10, DMG_TRUE) == 10);
}

static void test_dot_can_kill_and_ends_combat(void)
{
    Combat c = {0};
    add_unit(&c, "hero", SIDE_PLAYER, 100, 10, 0, -1);
    int32_t foe = add_unit(&c, "foe", SIDE_ENEMY, 5, 1, 0, 0);
    combat_begin(&c, 4);

    Buff poison = {0};
    poison.kind = BUFF_DOT;
    poison.magnitude = 5;
    poison.turns_left = 3;
    poison.ability_id = 9;
    combat_add_buff(&c, foe, &poison);

    combat_tick_buffs(&c, foe);
    assert(c.units[foe].alive == 0);

    combat_advance_turn(&c);
    assert(c.phase == PHASE_VICTORY);
}

static void test_stun_skips_turn(void)
{
    Combat c = {0};
    add_unit(&c, "hero", SIDE_PLAYER, 100, 20, 0, -1);
    int32_t foe = add_unit(&c, "foe", SIDE_ENEMY, 100, 10, 0, 0);
    combat_begin(&c, 5);

    Buff stun = {0};
    stun.kind = BUFF_STUN;
    stun.turns_left = 1;
    stun.ability_id = 11;
    combat_add_buff(&c, foe, &stun);

    /* Hero acts, then the stunned foe is skipped and the round rolls over. */
    combat_advance_turn(&c);
    assert(c.round == 2);
    assert(combat_current_unit(&c) == 0);
    assert(c.units[foe].buff_count == 0);
}

static void test_rng_is_deterministic_and_uniform_enough(void)
{
    Rng a, b;
    rng_seed(&a, 42);
    rng_seed(&b, 42);
    for (int i = 0; i < 100; i++)
        assert(rng_next(&a) == rng_next(&b));

    int buckets[10] = {0};
    rng_seed(&a, 7);
    for (int i = 0; i < 100000; i++)
        buckets[rng_below(&a, 10)]++;
    for (int i = 0; i < 10; i++)
        assert(buckets[i] > 9000 && buckets[i] < 11000);
}

int main(void)
{
    test_turn_order_by_speed();
    test_speed_buff_reorders_next_round();
    test_shield_absorbs_then_expires();
    test_defense_mitigates_but_leaves_one();
    test_dot_can_kill_and_ends_combat();
    test_stun_skips_turn();
    test_rng_is_deterministic_and_uniform_enough();
    printf("all core tests passed\n");
    return 0;
}
