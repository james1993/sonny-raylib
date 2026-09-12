/* Battle log tool: runs one battle and prints every move as it resolves.
 *
 * This is the harness for checking the port against the original -- set up the
 * same fight in the real game, play it out, and compare the numbers move by
 * move.
 *
 *   build/simulate [seed] [player_level] [enemy_level]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/battle.h"

static const char *kind_name(MoveKind k)
{
    switch (k) {
    case KIND_FULL_DAMAGE: return "damage";
    case KIND_HEAL:        return "heal";
    case KIND_FOCUS:       return "focus";
    default:               return "pass";
    }
}

static void print_roster(const Battle *b)
{
    for (int32_t i = 1; i < SONNY_SLOTS; i++) {
        const Unit *u = &b->units[i];
        if (u->LIFEU == 0)
            continue;
        printf("  slot %d team %d  %-16s Lv%-3d %4d/%-4d hp  %3d/%-3d fp  "
               "str %-5.0f mag %-5.0f spd %-5.0f\n",
               i, u->teamSide, u->name, u->plevel, u->LIFEN, u->LIFEU,
               u->FOCUSN, u->FOCUSU, u->STRENGTHU, u->MAGICU, u->SPEEDU);
    }
}

int main(int argc, char **argv)
{
    uint64_t seed = (argc > 1) ? strtoull(argv[1], NULL, 10) : 1;
    int32_t plevel = (argc > 2) ? atoi(argv[2]) : 5;
    int32_t elevel = (argc > 3) ? atoi(argv[3]) : 4;

    Battle b;
    battle_init(&b, seed, 1);

    /* Sonny goes in through the character path -- the player's stats come off
       the character screen, not the enemy scaling formula. The starting stat
       bonus is the original's Krin.StatSets0 = [0,5,0,3,0]. */
    Character sonny;
    memset(&sonny, 0, sizeof(sonny));
    sonny.class_template = unit_template_by_id(1);
    sonny.level = plevel;
    sonny.spent[1] = 5;
    sonny.spent[3] = 3;
    battle_place_character(&b, 1, &sonny, "Sonny", 1);

    battle_place_enemy(&b, 3, unit_template_by_id(5), plevel, 1);
    battle_place_enemy(&b, 2, unit_template_by_name("Zombie"), elevel, 1);
    battle_place_enemy(&b, 4, unit_template_by_name("ZPCI Assault"), elevel, 1);

    printf("seed %llu\n", (unsigned long long)seed);
    {
        int32_t levels[2] = {elevel, elevel};
        double rating = rewards_enemy_rating(levels, 2);
        Rng money_rng;
        rng_seed(&money_rng, seed);
        printf("enemy rating %.3f -> %.2f%% of a level, %d euros on a win\n",
               rating, character_xp_gain(rating, plevel),
               rewards_money(rating, &money_rng));
    }
    print_roster(&b);

    battle_team_select(&b);
    b.PrevTeam = b.TeamMove;
    for (int32_t i = 1; i < SONNY_SLOTS; i++)
        battle_queue(&b, i, i, 0, 0);
    printf("team %d moves first\n\n", b.TeamMove);

    int32_t phases = 0;
    double damage_by_team[3] = {0, 0, 0};

    while (b.phase != PHASE_OVER && phases < 400) {
        battle_declare_phase(&b);
        printf("-- round %d, team %d acts\n", b.round, b.TeamMoveNow);

        MoveEvent e;
        while (b.phase == PHASE_RESOLVE && battle_resolve_step(&b, &e)) {
            const AbilityDef *a = ability_by_id(e.moveID);
            const char *move = (a && a->name && a->name[0]) ? a->name
                             : (a && a->icon[0]) ? a->icon : "pass";
            if (e.moveID == 0) {
                printf("   %-14s passes\n", b.units[e.caster].name);
                continue;
            }
            printf("   %-14s %-18s -> %-14s %s %4d%s%s%s  "
                   "(target now %d/%d hp)\n",
                   b.units[e.caster].name, move, b.units[e.target].name,
                   kind_name(e.kind), e.amount,
                   e.pierced ? " pierced" : "",
                   e.absorbed ? " (shielded)" : "",
                   e.missed ? " unaffordable" : "",
                   b.units[e.target].LIFEN, b.units[e.target].LIFEU);
            if (e.kind == KIND_FULL_DAMAGE)
                damage_by_team[b.units[e.caster].teamSide] += e.amount;
            if (e.target_died)
                printf("   *** %s dies\n", b.units[e.target].name);
        }
        if (b.phase == PHASE_OVER)
            break;
        battle_end_phase(&b);
        phases++;
    }

    printf("\noutcome: %s after %d phases\n",
           b.winCondition == 1 ? "player team wins"
           : b.winCondition == 0 ? "player team loses"
           : b.winCondition == 2 ? "draw" : "unresolved", phases);
    printf("damage dealt: team 1 %.0f, team 2 %.0f\n",
           damage_by_team[1], damage_by_team[2]);
    print_roster(&b);
    return 0;
}
