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

    /* What the screen draws a target's bars against while the attacker is
       still crossing the floor: the reading the move was resolved against,
       which the event has to carry because the fight has already applied it.
       Deriving it from the damage instead once gave a killed target its own
       overkill as a life total -- seventeen left, hit for twenty-three, and
       the bar read twenty-three. */
    Battle w;
    setup(&w, 4242, 12);
    int32_t hits = 0, kills = 0;
    while (w.phase != PHASE_OVER) {
        battle_declare_phase(&w);
        MoveEvent e;
        for (;;) {
            int32_t life[SONNY_SLOTS], focus[SONNY_SLOTS];
            for (int32_t i = 0; i < SONNY_SLOTS; i++) {
                life[i] = w.units[i].LIFEN;
                focus[i] = w.units[i].FOCUSN;
            }
            if (!battle_resolve_step(&w, &e))
                break;
            assert(e.target_life == life[e.target]);
            assert(e.target_focus == focus[e.target]);
            if (e.fired && e.kind == KIND_FULL_DAMAGE && e.landed >= 0) {
                hits++;
                /* A blow never leaves a target on more life than it had, and
                   one that takes more than is left reads as all of it. */
                assert(w.units[e.target].LIFEN <= e.target_life);
                if (e.landed > e.target_life) {
                    kills++;
                    assert(w.units[e.target].LIFEN == 0);
                }
            }
        }
        battle_end_phase(&w);
    }
    assert(hits > 0);
    assert(kills > 0);

    /* Whether a blow lands. The original races the target's speed against
       the caster's, scaled by how fast the move is, and never misses with a
       move delivered as a shock or at a target being held stunned. A magic
       class hardly ever misses because its moves are shocks. */
    {
        Battle m;
        setup(&m, 77, 12);
        int32_t rolled = 0, shocks = 0, missed = 0;
        while (m.phase != PHASE_OVER) {
            battle_declare_phase(&m);
            MoveEvent e;
            while (battle_resolve_step(&m, &e)) {
                const AbilityDef *a = ability_by_id(e.moveID);
                if (!e.fired || !a)
                    continue;
                if (a->delivery == DELIVER_SHOCK) {
                    shocks++;
                    assert(!e.missed);       /* a shock cannot miss */
                } else {
                    rolled++;
                }
                if (e.missed) {
                    missed++;
                    /* Nothing about the move happened but the cost. */
                    assert(e.amount == 0 && e.landed < 0 && !e.tally
                           && !e.target_died);
                }
            }
            battle_end_phase(&m);
        }
        assert(rolled > 0 && shocks > 0);
        assert(missed > 0);
    }

    /* A unit that has fallen keeps its buffs exactly as they were: the
       driver never ticks an inactive caster, so its icons neither count down
       nor go away. And an empty slot, which is never active, never ticks
       either -- nor floats a number. */
    {
        Battle d;
        setup(&d, 99, 5);
        const BuffDef *poison = NULL;
        for (int32_t i = 0; i < SONNY_BUFF_COUNT && !poison; i++)
            if (SONNY_BUFFS[i].duration > 1)
                poison = &SONNY_BUFFS[i];
        assert(poison);
        buff_apply(&d.units[2], poison, 1, &d.units[1], 0);
        int32_t held = -1;
        for (int32_t i = 0; i < SONNY_MAX_BUFFS; i++)
            if (d.units[2].BUFFARRAYK[i].CD > 0)
                held = i;
        assert(held >= 0);
        int32_t cd = d.units[2].BUFFARRAYK[held].CD;
        d.units[2].LIFEN = 0;
        d.units[2].active = 0;
        for (int32_t phase = 0; phase < 6 && d.phase != PHASE_OVER; phase++) {
            battle_declare_phase(&d);
            MoveEvent e;
            while (battle_resolve_step(&d, &e)) {
                if (e.caster == 2 || d.units[e.caster].LIFEU == 0)
                    assert(e.tick_damage == 0 && !e.tick_shielded);
            }
            battle_end_phase(&d);
        }
        assert(d.units[2].BUFFARRAYK[held].CD == cd);
    }

    /* A unique buff put on twice is one buff with its time put back; an
       ordinary one stacks. */
    {
        Battle u;
        setup(&u, 5, 5);
        const BuffDef *once = buff_find(SONNY_BUFFS, SONNY_BUFF_COUNT, "BURNING");
        assert(once && once->unique);
        buff_land(&u.units[2], once, &u.units[1]);
        int32_t first = -1, held = 0;
        for (int32_t i = 0; i < SONNY_MAX_BUFFS; i++)
            if (u.units[2].BUFFARRAYK[i].CD > 0) {
                first = i;
                held++;
            }
        assert(held == 1);
        u.units[2].BUFFARRAYK[first].CD = 1;
        buff_land(&u.units[2], once, &u.units[1]);
        held = 0;
        for (int32_t i = 0; i < SONNY_MAX_BUFFS; i++)
            if (u.units[2].BUFFARRAYK[i].CD > 0)
                held++;
        assert(held == 1 && u.units[2].BUFFARRAYK[first].CD == once->duration);
    }

    /* The dispel takes off only what matches, and no more than it may:
       Heroic Motivation's first rank lifts one harmful buff and leaves a
       helpful one alone. */
    {
        Battle d;
        setup(&d, 6, 5);
        Unit *t = &d.units[1];
        double speed_before = t->SPEEDU;
        const BuffDef *harm = NULL, *help = NULL;
        for (int32_t i = 0; i < SONNY_BUFF_COUNT; i++) {
            const BuffDef *buff = &SONNY_BUFFS[i];
            if (buff->duration <= 0 || buff->unique)
                continue;
            if (!harm && buff->nature == -1)
                harm = buff;
            if (!help && buff->nature == 1)
                help = buff;
        }
        assert(harm && help);
        buff_land(t, harm, &d.units[2]);
        buff_land(t, harm, &d.units[2]);
        buff_land(t, help, &d.units[2]);
        unit_apply_changes(t);
        const AbilityDef *motivation = NULL;
        for (int32_t i = 0; i < SONNY_ABILITY_COUNT && !motivation; i++)
            if (SONNY_ABILITIES[i].dispel_count == 1
                && SONNY_ABILITIES[i].dispel_nature == -1)
                motivation = &SONNY_ABILITIES[i];
        assert(motivation);
        int32_t gone = buff_dispel(t, motivation->dispel_count,
                                   motivation->dispel_elements,
                                   motivation->dispel_nature, SONNY_BUFFS,
                                   SONNY_BUFF_COUNT);
        assert(gone == 1);
        int32_t harmful = 0, helpful = 0;
        for (int32_t i = 0; i < SONNY_MAX_BUFFS; i++) {
            if (t->BUFFARRAYK[i].CD <= 0)
                continue;
            if (strcmp(t->BUFFARRAYK[i].buffId, harm->key) == 0)
                harmful++;
            if (strcmp(t->BUFFARRAYK[i].buffId, help->key) == 0)
                helpful++;
        }
        assert(harmful == 1 && helpful == 1);
        /* And what is left unwinds cleanly: nothing lingers in the stats. */
        buff_dispel(t, 10, 0xFF, -1, SONNY_BUFFS, SONNY_BUFF_COUNT);
        buff_dispel(t, 10, 0xFF, 1, SONNY_BUFFS, SONNY_BUFF_COUNT);
        unit_apply_changes(t);
        assert(t->SPEEDU == speed_before);
    }

    printf("battle: determinism and invariants hold\n");
    return 0;
}
