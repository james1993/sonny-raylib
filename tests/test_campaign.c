/* Campaign data and progression.
 *
 * Checks the rosters resolve against the real tables, that a battle built from
 * a roster is legal, that drops stay inside their declared tables, and that a
 * character who spends his points can actually walk the first zone -- which
 * exercises setup, combat, rewards and advancement together.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/core/campaign.h"

/* Spend every available point on whatever the tree currently allows, then put
   the learned abilities on the bar. A stand-in for the player's choices. */
static void auto_spend(Character *c)
{
    int progress = 1;
    while (progress && character_unspent_skill_points(c) > 0) {
        progress = 0;
        for (int32_t node = 0; node < SONNY_TALENT_COUNT; node++) {
            if (character_unspent_skill_points(c) <= 0)
                break;
            if (character_can_learn(c, node) == TALENT_OK) {
                character_learn(c, node);
                progress = 1;
            }
        }
    }
    int32_t known[SONNY_TALENT_MAX + 2];
    int32_t n = character_known_abilities(c, known, SONNY_TALENT_MAX + 2);
    for (int32_t i = 0; i < SONNY_MOVE_SLOTS && i < n; i++)
        c->move_matrix[i] = known[i];
}

int main(void)
{
    assert(SONNY_BATTLE_COUNT == 50);
    assert(SONNY_ZONE_COUNT == 4);

    /* Zones cover a contiguous, ascending span of battle ids. */
    for (int i = 0; i < SONNY_ZONE_COUNT; i++) {
        assert(SONNY_ZONES[i].first_battle <= SONNY_ZONES[i].last_battle);
        if (i > 0)
            assert(SONNY_ZONES[i].first_battle
                   > SONNY_ZONES[i - 1].last_battle);
        /* Training battles must exist as rosters of their own. */
        for (int32_t t = 0; t < SONNY_ZONES[i].training_count; t++)
            assert(battle_def_by_id(SONNY_ZONES[i].training[t]) != NULL);
    }

    /* Every roster entry resolves, and every drop names a real item. */
    int dangling = 0, with_allies = 0, with_drops = 0;
    for (int i = 0; i < SONNY_BATTLE_COUNT; i++) {
        const BattleDef *d = &SONNY_BATTLES[i];
        int enemies = 0;
        for (int32_t s = 0; s < SONNY_BATTLE_SLOTS; s++) {
            int32_t entry = d->players[s];
            if (entry > 0) {
                enemies++;
                if (!unit_template_by_id(entry)) {
                    fprintf(stderr, "battle %d slot %d: no template %d\n",
                            d->id, s + 2, entry);
                    dangling++;
                }
            } else if (entry < 0) {
                with_allies++;
                /* Ally entries index friendArrayX[entry + 2]. */
                assert(entry + 2 >= 0 && entry + 2 < SONNY_MAX_ALLIES);
                /* An ally must land on the player's side of the parity split. */
                assert((s + 2) % 2 == 1);
            }
        }
        /* A fight needs something to fight. */
        assert(enemies > 0);

        for (int32_t k = 0; k < d->drop_count; k++) {
            if (!item_by_id(d->drops[k].item_id)) {
                fprintf(stderr, "battle %d: no drop item %d\n", d->id,
                        d->drops[k].item_id);
                dangling++;
            }
            assert(d->drops[k].chance > 0 && d->drops[k].chance <= 100);
        }
        for (int32_t k = 0; k < d->rare_count; k++) {
            if (!item_by_id(d->rare[k])) {
                fprintf(stderr, "battle %d: no rare item %d\n", d->id,
                        d->rare[k]);
                dangling++;
            }
        }
        if (d->drop_count || d->rare_count)
            with_drops++;
    }
    assert(dangling == 0);

    /* Setting up a roster puts the player in slot 1 and the enemies where the
       definition says. */
    Campaign c;
    campaign_new(&c, 1);
    assert(c.progress_battle == 2);
    assert(campaign_zone(&c)->zone == 0);

    const BattleDef *def = battle_def_by_id(2);
    Battle b;
    assert(campaign_setup_battle(&c, def, &b, 42));
    assert(b.units[1].active && strcmp(b.units[1].name, "Sonny") == 0);
    for (int32_t s = 0; s < SONNY_BATTLE_SLOTS; s++) {
        if (def->players[s] > 0)
            assert(b.units[s + 2].active);
        else if (def->players[s] == 0)
            assert(!b.units[s + 2].active);
    }
    /* Battle 2 forces a team to start. */
    assert(def->absolute_start == 1);
    assert(b.TeamMove == 1);

    /* Drops only ever come from the battle's own tables. */
    Rng rng;
    rng_seed(&rng, 7);
    int32_t dropped[SONNY_MAX_DROPPED];
    int rolls = 0, any = 0;
    for (int trial = 0; trial < 500; trial++) {
        int32_t n = campaign_roll_drops(def, &rng, dropped, SONNY_MAX_DROPPED);
        rolls += n;
        for (int32_t k = 0; k < n; k++) {
            int found = 0;
            for (int32_t j = 0; j < def->drop_count; j++)
                if (def->drops[j].item_id == dropped[k])
                    found = 1;
            for (int32_t j = 0; j < def->rare_count; j++)
                if (def->rare[j] == dropped[k])
                    found = 1;
            assert(found);
            any = 1;
        }
    }
    (void)any;

    /* Drop rates should match their declared chances. Pick a battle whose
       drops are all chance-based -- battle 1 also grants a guaranteed rare,
       and its rare table repeats the chance drop's id, so counting ids there
       would conflate the two paths. */
    const BattleDef *dropper = NULL;
    for (int i = 0; i < SONNY_BATTLE_COUNT && !dropper; i++)
        if (SONNY_BATTLES[i].drop_count > 0
            && SONNY_BATTLES[i].rare_dropper == 0)
            dropper = &SONNY_BATTLES[i];
    assert(dropper != NULL);

    double expected = 0;
    for (int32_t k = 0; k < dropper->drop_count; k++)
        expected += dropper->drops[k].chance / 100.0;

    const int trials = 20000;
    int hits = 0;
    for (int trial = 0; trial < trials; trial++)
        hits += campaign_roll_drops(dropper, &rng, dropped, SONNY_MAX_DROPPED);
    double rate = (double)hits / trials;
    if (rate < expected - 0.02 || rate > expected + 0.02) {
        fprintf(stderr, "drop rate %.3f, expected %.3f\n", rate, expected);
        return 1;
    }

    /* And a guaranteed rare really is guaranteed: battle 1 hands out exactly
       one rare pick on top of its 40% chance drop. */
    const BattleDef *rare_dropper = battle_def_by_id(1);
    assert(rare_dropper->rare_dropper == 1 && rare_dropper->rare_count == 2);
    int total = 0;
    for (int trial = 0; trial < trials; trial++)
        total += campaign_roll_drops(rare_dropper, &rng, dropped,
                                     SONNY_MAX_DROPPED);
    double per_battle = (double)total / trials;
    if (per_battle < 1.38 || per_battle > 1.42) {
        fprintf(stderr, "items per battle %.3f, expected ~1.40\n", per_battle);
        return 1;
    }

    /* Walk zone 1's progress battles with a character who spends his points.
       Enemies scale with the roster, so this exercises setup, combat, rewards
       and advancement in sequence. */
    Campaign run;
    campaign_new(&run, 1);
    run.player.level = 12;
    auto_spend(&run.player);

    int fought = 0, won = 0;
    while (run.progress_battle <= SONNY_ZONES[0].last_battle) {
        const BattleDef *d = battle_def_by_id(run.progress_battle);
        if (!d)
            break;

        Battle fight;
        assert(campaign_setup_battle(&run, d, &fight, 1000 + fought));
        /* Drive Sonny with the AI so the fight resolves without input. */
        fight.brains[1].AION = 1;
        battle_run(&fight, 600);
        fought++;

        if (fight.winCondition == 1) {
            Rng reward_rng;
            rng_seed(&reward_rng, fought);
            BattleRewards r = campaign_award(&run, &fight, &reward_rng);
            assert(r.enemy_rating > 0);
            assert(r.xp_percent >= 0);
            assert(r.euros > 0);
            if (r.leveled)
                auto_spend(&run.player);
            campaign_advance(&run);
            won++;
        } else {
            /* A loss does not advance; level up and try again. */
            run.player.level++;
            auto_spend(&run.player);
        }
        assert(fought < 60);
    }

    printf("campaign: %d battles, %d zones, %d with allies, %d with drops\n",
           SONNY_BATTLE_COUNT, SONNY_ZONE_COUNT, with_allies, with_drops);
    printf("campaign: cleared zone 1 in %d fights (%d wins), now level %d "
           "with %d euros\n", fought, won, run.player.level, run.euros);

    assert(won == SONNY_ZONES[0].last_battle - SONNY_ZONES[0].first_battle + 1);
    assert(run.euros > 0);
    return 0;
}
