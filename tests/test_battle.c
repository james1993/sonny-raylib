/* End-to-end battles on the original's own tables.
 *
 * There is no reference transcription to diff against here -- the turn driver
 * is a state machine spread across frame 212's enterFrame, not a pure
 * function -- so this checks the properties the original guarantees: battles
 * terminate, exactly one side is left standing, and no unit ever leaves a
 * legal state. It also pins determinism, which is what makes replaying a
 * recorded fight against the original possible later.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/core/battle.h"

static void check_invariants(const Battle *b, const char *where)
{
    for (int32_t i = 1; i < SONNY_SLOTS; i++) {
        const Unit *u = &b->units[i];
        if (u->LIFEU == 0)
            continue;   /* empty slot */

        if (u->LIFEN < 0 || u->LIFEN > u->LIFEU) {
            fprintf(stderr, "%s: slot %d health %d/%d out of range\n",
                    where, i, u->LIFEN, u->LIFEU);
            assert(0);
        }
        if (u->FOCUSN < 0 || u->FOCUSN > u->FOCUSU) {
            fprintf(stderr, "%s: slot %d focus %d/%d out of range\n",
                    where, i, u->FOCUSN, u->FOCUSU);
            assert(0);
        }
        if (u->LIFEN == 0 && u->active) {
            fprintf(stderr, "%s: slot %d is at zero health but still active\n",
                    where, i);
            assert(0);
        }
        if (u->SHIELD < 0) {
            fprintf(stderr, "%s: slot %d has negative shield\n", where, i);
            assert(0);
        }
    }
}

static void setup(Battle *b, uint64_t seed, int32_t enemy_level)
{
    battle_init(b, seed, 1);
    /* Slots 1/3/5 are team 1, 2/4/6 are team 2. */
    battle_place_enemy(b, 1, unit_template_by_id(1), 5, 1);
    battle_place_enemy(b, 3, unit_template_by_id(5), 5, 1);  /* the ally */
    battle_place_enemy(b, 2, unit_template_by_name("Zombie"), enemy_level, 1);
    battle_place_enemy(b, 4, unit_template_by_name("ZPCI Assault"), enemy_level, 1);
}

/* Runs a matchup over many seeds and reports how it went. */
typedef struct {
    int wins, losses, draws, unfinished;
    double avg_phases;
} Series;

static Series run_series(int32_t enemy_level, int seeds)
{
    Series s;
    memset(&s, 0, sizeof(s));
    int32_t total = 0;

    for (int seed = 1; seed <= seeds; seed++) {
        Battle b;
        setup(&b, (uint64_t)seed, enemy_level);
        check_invariants(&b, "setup");

        total += battle_run(&b, 600);
        check_invariants(&b, "after battle");

        switch (b.winCondition) {
        case 1:  s.wins++; break;
        case 0:  s.losses++; break;
        case 2:  s.draws++; break;
        default: s.unfinished++; break;
        }
        if (b.winCondition == 0 || b.winCondition == 1)
            assert(battle_team_alive(&b, 1) != battle_team_alive(&b, 2));
    }
    s.avg_phases = (double)total / seeds;
    return s;
}

int main(void)
{
    /* The data loaded at all. */
    assert(SONNY_ABILITY_COUNT == 142);
    assert(SONNY_BUFF_COUNT == 87);
    assert(SONNY_UNIT_COUNT == 43);

    const AbilityDef *smash = ability_by_id(1);
    assert(smash && strcmp(smash->name, "Smash") == 0);
    assert(smash->focus_cost == 15 && smash->cooldown == 6);
    assert(smash->coefs.strength_coef == 1.6);
    assert(smash->coefs.flat_damage == 5);

    /* An even matchup, then one the player should struggle with. Every battle
       must reach a decision, and exactly one side may be left standing. */
    Series easy = run_series(4, 200);
    Series hard = run_series(20, 200);

    printf("battle: vs Lv4  %d wins %d losses %d draws, %.1f phases average\n",
           easy.wins, easy.losses, easy.draws, easy.avg_phases);
    printf("battle: vs Lv20 %d wins %d losses %d draws, %.1f phases average\n",
           hard.wins, hard.losses, hard.draws, hard.avg_phases);

    assert(easy.unfinished == 0);
    assert(hard.unfinished == 0);

    /* The favourable matchup should be won; the steep one should produce real
       losses. Between them this pins that damage, healing and the AI's
       retreat threshold all actually influence the outcome -- a stuck AI
       makes the hard matchup unwinnable and an idle one makes it free. */
    assert(easy.wins == 200);
    assert(hard.losses > 0);
    assert(hard.wins > 0);
    /* Harder enemies mean longer battles. */
    assert(hard.avg_phases > easy.avg_phases);

    /* Determinism: the same seed replays identically. */
    Battle x, y;
    setup(&x, 12345, 12);
    setup(&y, 12345, 12);
    battle_run(&x, 600);
    battle_run(&y, 600);
    assert(x.winCondition == y.winCondition);
    assert(x.round == y.round);
    for (int32_t i = 1; i < SONNY_SLOTS; i++) {
        assert(x.units[i].LIFEN == y.units[i].LIFEN);
        assert(x.units[i].FOCUSN == y.units[i].FOCUSN);
        assert(x.units[i].active == y.units[i].active);
    }

    /* A different seed should generally produce a different battle. */
    Battle z;
    setup(&z, 999, 12);
    battle_run(&z, 600);
    int differs = (z.round != x.round);
    for (int32_t i = 1; i < SONNY_SLOTS && !differs; i++)
        if (z.units[i].LIFEN != x.units[i].LIFEN)
            differs = 1;
    assert(differs);

    printf("battle: determinism and invariants hold\n");
    return 0;
}
