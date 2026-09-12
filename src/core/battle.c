#include <math.h>
#include <stdio.h>
#include <string.h>
#include "battle.h"

/* ------------------------------------------------------------------ setup */

void battle_init(Battle *b, uint64_t seed, int32_t playerNumber)
{
    memset(b, 0, sizeof(*b));
    for (int32_t i = 0; i < SONNY_SLOTS; i++) {
        unit_init(&b->units[i], i);
        b->units[i].active = 0;
    }
    b->playerNumber = playerNumber;
    b->winCondition = -1;
    b->round = 1;
    b->phase = PHASE_DECLARE;
    rng_seed(&b->rng, seed);
    rng_refill_krs(&b->rng);
}

/* Shared tail of both placement paths: the AI's move lists and thresholds. */
static void install_brain(Brain *br, const UnitTemplate *t, int32_t ai)
{
    memset(br, 0, sizeof(*br));
    br->AION = ai;
    br->Aggression = t->aggression;
    br->LifeBoundary1 = t->life_boundary1;
    br->LifeBoundary2 = t->life_boundary2;
    br->FocusAggression = t->focus_aggression;
    br->movesA_count = t->moves_a_count < SONNY_AI_MOVES ? t->moves_a_count
                                                         : SONNY_AI_MOVES;
    for (int32_t i = 0; i < br->movesA_count; i++)
        br->movesA[i] = t->moves_a[i];
    br->movesD_count = t->moves_d_count < SONNY_AI_MOVES ? t->moves_d_count
                                                         : SONNY_AI_MOVES;
    for (int32_t i = 0; i < br->movesD_count; i++)
        br->movesD[i] = t->moves_d[i];
}

void battle_place_enemy(Battle *b, int32_t slot, const UnitTemplate *t,
                        int32_t level, int32_t ai)
{
    Unit *u = &b->units[slot];

    unit_init(u, slot);
    install_brain(&b->brains[slot], t, ai);

    snprintf(u->name, SONNY_NAME_LEN, "%s", t->name);
    u->plevel = level;
    u->active = 1;

    /* krinAddNewUnit: linear in level, no rounding of the three stats. */
    u->STRENGTH = t->strength + level * t->strength_growth;
    u->MAGIC = t->magic + level * t->magic_growth;
    u->SPEED = t->speed + level * t->speed_growth;
    u->LIFE = floor((t->life + level * t->life_growth) * 8 + 0.5);
    for (int32_t e = 0; e < SONNY_ELEMENTS; e++) {
        u->PER[e] = t->per[e] + level * 5;
        u->DEF[e] = t->def[e] + level * 5;
    }
    u->LIFEU = u->LIFEN = (int32_t)u->LIFE;
    u->FOCUSU = u->FOCUSN = (int32_t)t->focus;
    u->STRENGTHU = u->STRENGTH;
    u->MAGICU = u->MAGIC;
    u->SPEEDU = u->SPEED;
    for (int32_t e = 0; e < SONNY_ELEMENTS; e++) {
        u->PERU[e] = u->PER[e];
        u->DEFU[e] = u->DEF[e];
    }
}

void battle_place_character(Battle *b, int32_t slot, const Character *c,
                            const char *name, int32_t ai)
{
    Unit *u = &b->units[slot];
    Brain *br = &b->brains[slot];
    DerivedStats d = character_derive(c);

    unit_init(u, slot);
    install_brain(br, c->class_template, ai);

    /* The character's own bar, not the class template's move list. The
       original only ever drives Sonny from input; giving the AI his bar is a
       harness convenience for headless simulation, and leaving the defensive
       list empty makes that AI always attack (an empty defensive list is what
       skips the retreat check). */
    br->movesA_count = 0;
    br->movesD_count = 0;
    for (int32_t i = 0; i < SONNY_MOVE_SLOTS; i++)
        if (c->move_matrix[i] != 0 && br->movesA_count < SONNY_AI_MOVES)
            br->movesA[br->movesA_count++] = c->move_matrix[i];

    snprintf(u->name, SONNY_NAME_LEN, "%s",
             name ? name : c->class_template->name);
    u->plevel = c->level;
    u->active = 1;

    u->STRENGTH = u->STRENGTHU = d.strength;
    u->MAGIC = u->MAGICU = d.magic;
    u->SPEED = u->SPEEDU = d.speed;
    /* frame 196: LIFEU = Math.round(LIFE = Krin.LIFE * 8) */
    u->LIFE = d.life * 8;
    u->LIFEU = u->LIFEN = (int32_t)floor(u->LIFE + 0.5);
    u->FOCUSU = u->FOCUSN = (int32_t)d.focus;
    for (int32_t e = 0; e < SONNY_ELEMENTS; e++) {
        u->PER[e] = u->PERU[e] = d.per[e];
        u->DEF[e] = u->DEFU[e] = d.def[e];
    }

    /* Passive talents are applied as buffs at the start of the battle, with
       the unit as its own caster, then folded in by one applyChangesKrin --
       exactly the loop at the end of frame 212. */
    const char *passives[SONNY_TALENT_MAX];
    int32_t count = character_passive_buffs(c, passives, SONNY_TALENT_MAX);
    for (int32_t i = 0; i < count; i++) {
        const BuffDef *def = buff_find(SONNY_BUFFS, SONNY_BUFF_COUNT,
                                       passives[i]);
        if (def)
            buff_apply(u, def, 1, u, 0);
    }
    if (count > 0)
        unit_apply_changes(u);
}

/* -------------------------------------------------------------- turn order */

int battle_team_alive(const Battle *b, int32_t team)
{
    for (int32_t i = 1; i < SONNY_SLOTS; i++)
        if (b->units[i].teamSide == team && b->units[i].active)
            return 1;
    return 0;
}

void battle_check_win(Battle *b)
{
    int32_t player_team = b->units[b->playerNumber].teamSide;
    int win = 0, lose = 0;

    if (!battle_team_alive(b, 1))
        (player_team == 1) ? lose++ : win++;
    if (!battle_team_alive(b, 2))
        (player_team == 2) ? lose++ : win++;

    if (win)
        b->winCondition = 1;
    if (lose)
        b->winCondition = 0;
    if (win + lose == 2)
        b->winCondition = 2;
    if (b->winCondition >= 0)
        b->phase = PHASE_OVER;
}

/* Sort three slots by SPEEDU descending, ties broken by slot descending, and
   record each unit's position as its teamAdder. */
static void team_speed_order(Battle *b, const int32_t slots[3], int32_t base)
{
    int32_t order[3] = {slots[0], slots[1], slots[2]};
    for (int32_t i = 1; i < 3; i++) {
        int32_t key = order[i];
        int32_t j = i - 1;
        while (j >= 0) {
            double a = b->units[order[j]].SPEEDU;
            double c = b->units[key].SPEEDU;
            if (a > c || (a == c && order[j] > key))
                break;
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }
    for (int32_t i = 0; i < 3; i++)
        b->brains[order[i]].teamAdder = i;
    (void)base;
}

void battle_team_select(Battle *b)
{
    double t1 = 0, t2 = 0;
    int32_t d1 = 0, d2 = 0;

    for (int32_t i = 1; i < SONNY_SLOTS; i++) {
        if (!b->units[i].active)
            continue;
        if (i % 2) {
            t1 += b->units[i].SPEEDU;
            d1++;
        } else {
            t2 += b->units[i].SPEEDU;
            d2++;
        }
    }
    /* The original divides unconditionally; with no living members the result
       is NaN and every comparison below is false, which only happens once the
       battle is already decided. */
    t1 /= d1;
    t2 /= d2;

    if (t1 > t2) {
        b->TeamMove = 1;
        b->s1tm = 0;
        b->s2tm = 3;
    }
    if (t2 > t1) {
        b->TeamMove = 2;
        b->s1tm = 3;
        b->s2tm = 0;
    }
    if (t1 == t2) {
        /* Single player: team 1 wins the tie. */
        b->TeamMove = 1;
        b->s1tm = 0;
        b->s2tm = 3;
    }

    b->TurnTime = 2;
    b->TeamMoveNow = b->TeamMove;

    const int32_t team1[3] = {1, 3, 5};
    const int32_t team2[3] = {2, 4, 6};
    team_speed_order(b, team1, b->s1tm);
    team_speed_order(b, team2, b->s2tm);

    for (int32_t i = 1; i < SONNY_SLOTS; i++)
        if (b->units[i].active)
            b->brains[i].AIGoER = (b->units[i].teamSide == b->TeamMoveNow);
}

static void ai_go_er_switch(Battle *b)
{
    for (int32_t i = 1; i < SONNY_SLOTS; i++)
        b->brains[i].AIGoER = !b->brains[i].AIGoER;
}

/* ------------------------------------------------------------- move queue */

void battle_queue(Battle *b, int32_t caster, int32_t target, int32_t moveID,
                  int32_t autoMove)
{
    int32_t adder = 3;
    if (b->units[caster].teamSide == b->TeamMove) {
        if (b->TeamMove == b->TeamMoveNow && !autoMove)
            adder = 0;
        else
            adder = 6;
    }
    int32_t box = adder + b->brains[caster].teamAdder;
    if (box < 0 || box >= SONNY_QUEUE)
        return;
    b->queue[box].caster = caster;
    b->queue[box].target = target;
    b->queue[box].moveID = moveID;
}

/* --------------------------------------------------------------------- AI */

/* A move is affordable if its focus and health costs can be paid.
 *
 * A move whose costs the original never defined fails here, because that is
 * what ActionScript does: `FOCUSN >= undefined` evaluates undefined to NaN and
 * the comparison is false. That is the mechanism that keeps the "None"
 * placeholder out of the AI's move lists -- and with it out of the defensive
 * list, which in turn decides whether the health-based retreat check runs at
 * all. Defaulting those costs to zero instead makes units with no real
 * defensive move idle away their turns. */
static int can_pay(const Unit *u, const AbilityDef *a)
{
    if (!a->costs_defined)
        return 0;
    double health_cost = a->health_cost
                       + floor(u->LIFEU * a->health_cost_pct + 0.5);
    return u->FOCUSN >= a->focus_cost && u->LIFEN > health_cost;
}

void battle_ai_declare(Battle *b, int32_t slot)
{
    Unit *pg = &b->units[slot];
    Brain *br = &b->brains[slot];
    int32_t team = pg->teamSide;

    /* Usable attack and defensive moves: off cooldown, affordable, and either
       able to target an enemy or self, or with a living ally to target. */
    int32_t attack[SONNY_AI_MOVES], attack_cd[SONNY_AI_MOVES], attack_n = 0;
    int32_t defend[SONNY_AI_MOVES], defend_cd[SONNY_AI_MOVES], defend_n = 0;

    int32_t friend_lock = 0;
    int has_friend = 0;
    for (int32_t off = 0; off < 6; off += 2) {
        int32_t mate = team + off;
        if (mate < SONNY_SLOTS && b->units[mate].active && mate != slot) {
            has_friend = 1;
            friend_lock = mate;
        }
    }

    for (int32_t i = 0; i < br->movesA_count; i++) {
        if (br->CDArrayA[i] != 0)
            continue;
        const AbilityDef *a = ability_by_id(br->movesA[i]);
        if (!a || !can_pay(pg, a))
            continue;
        if (a->target_self + a->target_enemy > 0 || has_friend) {
            attack[attack_n] = br->movesA[i];
            attack_cd[attack_n] = i;
            attack_n++;
        }
    }
    for (int32_t i = 0; i < br->movesD_count; i++) {
        if (br->CDArrayD[i] != 0)
            continue;
        const AbilityDef *a = ability_by_id(br->movesD[i]);
        if (!a || !can_pay(pg, a))
            continue;
        if (a->target_self + a->target_enemy > 0 || has_friend) {
            defend[defend_n] = br->movesD[i];
            defend_cd[defend_n] = i;
            defend_n++;
        }
    }

    int attack_mode = 1;
    int script_end = 0;
    if (attack_n == 0) {
        script_end = 1;
        attack_mode = 0;
    }
    if (defend_n == 0)
        script_end = 1;

    /* Team health, counting healing already queued this phase, decides
       whether to switch to the defensive list. */
    double total = 0, missing = 0;
    for (int32_t off = 0; off < 6; off += 2) {
        int32_t mate = team + off;
        if (mate >= SONNY_SLOTS || !b->units[mate].active)
            continue;
        total += b->units[mate].LIFEU;
        missing += b->units[mate].LIFEU
                 - (b->units[mate].LIFEN + b->healedThisTurn[mate]);
    }
    if (!script_end) {
        double health_pct = (1 - missing / total) * 100;
        if (health_pct <= br->LifeBoundary1) {
            if (health_pct <= br->LifeBoundary2) {
                attack_mode = 0;
            } else {
                int32_t roll = rng_krrr(&b->rng);
                attack_mode = (br->Aggression >= roll);
            }
        }
    }

    /* Pick from the chosen list by rolling into equal-width buckets. */
    int32_t *list = attack_mode ? attack : defend;
    int32_t *cds = attack_mode ? attack_cd : defend_cd;
    int32_t count = attack_mode ? attack_n : defend_n;
    if (count == 0) {
        battle_queue(b, slot, slot, 0, 0);
        return;
    }
    double bits = 100.0 / count;
    int32_t roll = rng_krrr(&b->rng);
    int32_t index = (int32_t)ceil(roll / bits) - 1;
    if (index < 0)
        index = 0;
    if (index >= count)
        index = count - 1;
    int32_t moveID = list[index];
    int32_t cd_slot = cds[index];
    const AbilityDef *a = ability_by_id(moveID);

    int32_t target = 0;
    if (a->target_self == 1)
        target = slot;

    /* Enemy-targeting: gather the living, track the weakest, then either focus
       the weakest or pick at random. Order matches the original's. */
    int32_t alive[3], alive_n = 0, weakest = 0;
    if (a->target_enemy == 1) {
        const int32_t probe[3] = {7 - team, 5 - team, 3 - team};
        for (int32_t i = 0; i < 3; i++) {
            int32_t e = probe[i];
            if (e < 1 || e >= SONNY_SLOTS || !b->units[e].active)
                continue;
            alive[alive_n++] = e;
            if (weakest == 0 || b->units[e].LIFEN < b->units[weakest].LIFEN)
                weakest = e;
        }
        int32_t focus_roll = rng_krrr(&b->rng);
        if (focus_roll < br->FocusAggression) {
            target = weakest;
        } else if (alive_n > 0) {
            double bits2 = 100.0 / alive_n;
            int32_t r2 = rng_krrr(&b->rng);
            int32_t idx = (int32_t)ceil(r2 / bits2) - 1;
            if (idx < 0)
                idx = 0;
            if (idx >= alive_n)
                idx = alive_n - 1;
            target = alive[idx];
        }
    }

    /* Ally-targeting heals go to the worst-off mate, counting healing already
       queued this phase so two healers do not pile onto one target. */
    if (a->target_ally == 1 && (target == 0 || target == slot)) {
        if (b->units[team].active)
            target = team;

        double heal_guess = a->coefs.damage_coef
            * ((pg->STRENGTHU + a->coefs.strength_add) * a->coefs.strength_coef
             + (pg->MAGICU + a->coefs.magic_add) * a->coefs.magic_coef
             + (pg->SPEEDU + a->coefs.speed_add) * a->coefs.speed_coef
             + pg->FOCUSN * a->coefs.focus_coef + a->coefs.flat_damage);

        for (int32_t off = 2; off < 6; off += 2) {
            int32_t mate = team + off;
            if (mate >= SONNY_SLOTS || !b->units[mate].active || target == 0)
                continue;
            double mate_frac = (b->units[mate].LIFEN + b->healedThisTurn[mate])
                             / (double)b->units[mate].LIFEU;
            double cur_frac = (b->units[target].LIFEN + b->healedThisTurn[target])
                            / (double)b->units[target].LIFEU;
            int at_full = b->units[target].LIFEN == b->units[target].LIFEU;
            if (at_full || mate_frac <= cur_frac)
                target = mate;
        }
        if (target > 0)
            b->healedThisTurn[target] += heal_guess;
    }
    if (a->target_ally == 1 && a->target_self == 0)
        target = friend_lock;

    if (a->target_enemy == 1 && alive_n == 0) {
        battle_queue(b, slot, slot, 0, 0);
        return;
    }
    if (target <= 0 || target >= SONNY_SLOTS) {
        battle_queue(b, slot, slot, 0, 0);
        return;
    }

    if (attack_mode)
        br->CDArrayA[cd_slot] = a->cooldown;
    else
        br->CDArrayD[cd_slot] = a->cooldown;
    battle_queue(b, slot, target, moveID, 0);
}

void battle_declare_phase(Battle *b)
{
    for (int32_t off = 0; off < 3; off++) {
        for (int32_t i = 1; i < SONNY_SLOTS; i++) {
            if (b->brains[i].teamAdder != off)
                continue;
            if (!b->brains[i].AIGoER)
                continue;
            /* A dead unit does not declare: its pass was queued when it
               died, exactly as the original does. */
            if (!b->units[i].active)
                continue;
            if (!b->brains[i].AION)
                continue;   /* driven from outside via battle_queue */

            for (int32_t k = 0; k < SONNY_AI_MOVES; k++) {
                if (b->brains[i].CDArrayD[k] != 0)
                    b->brains[i].CDArrayD[k]--;
                if (b->brains[i].CDArrayA[k] != 0)
                    b->brains[i].CDArrayA[k]--;
            }
            battle_ai_declare(b, i);
        }
    }
    ai_go_er_switch(b);
    b->Cycler = 3;
    b->phase = PHASE_RESOLVE;
}

/* ---------------------------------------------------------------- resolve */

static void pay_costs(Unit *u, const AbilityDef *a)
{
    u->FOCUSN -= a->focus_cost;
    u->LIFEN -= a->health_cost + (int32_t)floor(u->LIFEU * a->health_cost_pct + 0.5);
    if (u->FOCUSN < 0)
        u->FOCUSN = 0;
}

int battle_resolve_step(Battle *b, MoveEvent *event)
{
    if (b->Cycler <= 0)
        return 0;
    if (b->PlayerToMove == 6)
        b->PlayerToMove = 0;

    QueuedMove *q = &b->queue[b->PlayerToMove];
    Unit *caster = &b->units[q->caster];
    Unit *target = &b->units[q->target];
    const AbilityDef *a = ability_by_id(q->moveID);

    memset(event, 0, sizeof(*event));
    event->caster = q->caster;
    event->target = q->target;
    event->moveID = q->moveID;
    event->kind = a ? a->kind : KIND_NONE;

    int usable = q->moveID != 0 && a && caster->active && target->active
                 && caster->STUN == 0;
    if (usable && !can_pay(caster, a)) {
        usable = 0;
        event->missed = 1;
    }

    if (usable) {
        pay_costs(caster, a);

        if (a->kind == KIND_FULL_DAMAGE) {
            DamageResult d = formula_full_damage(&b->rng, caster, target,
                                                &a->coefs);
            int32_t absorbed = 0;
            formula_apply_damage(target, d.damage, &absorbed);
            event->pierced = d.pierced;
            event->amount = d.damage;
            event->absorbed = absorbed;
        } else if (a->kind == KIND_HEAL) {
            DamageResult h = formula_heal(&b->rng, caster, target, &a->coefs);
            event->pierced = h.pierced;
            event->amount = formula_apply_heal(target, h.damage);
        } else if (a->kind == KIND_FOCUS) {
            event->amount = formula_apply_focus(target, &a->coefs);
        }

        if (a->buff && a->buff[0]) {
            const BuffDef *def = buff_find(SONNY_BUFFS, SONNY_BUFF_COUNT,
                                           a->buff);
            if (def) {
                buff_apply(target, def, 1, caster, 0);
                unit_apply_changes(target);
            }
        }
        if (!target->active)
            event->target_died = 1;
    }

    /* A unit that just died queues a pass for its next turn. */
    if (!target->active && event->target_died)
        battle_queue(b, q->target, q->target, 0, 1);

    /* The caster ticks its own buffs after acting. */
    if (caster->active) {
        TickResult tick = buff_tick(caster, SONNY_BUFFS, SONNY_BUFF_COUNT);
        if (tick.died)
            battle_queue(b, q->caster, q->caster, 0, 1);
    }

    b->PlayerToMove++;
    b->Cycler--;
    battle_check_win(b);
    return 1;
}

void battle_end_phase(Battle *b)
{
    for (int32_t i = 0; i < SONNY_SLOTS; i++)
        b->healedThisTurn[i] = 0;

    b->TurnTime--;
    if (b->TurnTime == 0) {
        battle_team_select(b);
        if (b->TeamMove == b->PrevTeam) {
            for (int32_t i = 0; i < 3; i++)
                b->queue[i] = b->queue[i + 6];
        } else {
            for (int32_t i = 0; i < 3; i++)
                b->queue[i] = b->queue[i + 3];
            for (int32_t i = 3; i < 6; i++)
                b->queue[i] = b->queue[i + 3];
        }
        b->PrevTeam = b->TeamMove;
        b->PlayerToMove = 0;
        b->round++;
    } else {
        /* Mid-round: the other team acts, and the cursor carries on into
           their half of the queue. */
        b->TeamMoveNow = (b->TeamMove == 1) ? 2 : 1;
    }
    if (b->phase != PHASE_OVER)
        b->phase = PHASE_DECLARE;
}

int32_t battle_run(Battle *b, int32_t max_phases)
{
    int32_t phases = 0;

    battle_team_select(b);
    b->PrevTeam = b->TeamMove;
    for (int32_t i = 1; i < SONNY_SLOTS; i++)
        battle_queue(b, i, i, 0, 0);

    while (b->phase != PHASE_OVER && phases < max_phases) {
        battle_declare_phase(b);
        MoveEvent event;
        while (b->phase == PHASE_RESOLVE && battle_resolve_step(b, &event))
            ;
        if (b->phase == PHASE_OVER)
            break;
        battle_end_phase(b);
        phases++;
    }
    return phases;
}
