/* The battle state machine, ported from frame 212 (TeamSelect,
 * TeamSpeedAdder, krinAddMove, the enterFrame driver) and frame 62
 * (AImoveAdder, executeMove).
 *
 * Structure of a round, as the original runs it:
 *
 *   TeamSelect      compares each team's average SPEEDU; the faster team is
 *                   TeamMove and acts first. TurnTime is set to 2, so a round
 *                   is two phases -- one per team.
 *   declare         every unit on the phase's team picks a move. AI units run
 *                   AImoveAdder after their cooldowns tick down.
 *   resolve         the three queued moves for that team resolve in slots
 *                   0..2, each followed by a buff tick on its caster.
 *   phase end       TurnTime--. At zero, TeamSelect runs again and the move
 *                   queue window slides.
 *
 * Slots are 1..6 with the original's parity teams: 1/3/5 are team 1 and 2/4/6
 * are team 2. Index 0 is unused, exactly as playerKrin0 is in the original.
 */
#ifndef SONNY_BATTLE_H
#define SONNY_BATTLE_H

#include "buffs.h"
#include "character.h"
#include "formula.h"
#include "../gen/gamedata.h"

#define SONNY_SLOTS      7    /* 1..6 used */
#define SONNY_QUEUE      9    /* MoveArrayFINAL */
#define SONNY_AI_MOVES   15   /* CDArrayA/CDArrayD, as the original iterates */

typedef struct {
    int32_t caster;     /* slot */
    int32_t target;     /* slot */
    int32_t moveID;     /* 0 = pass */
} QueuedMove;

/* Per-unit state that lives outside Unit in the original (on the same object,
   but only used for move selection). */
typedef struct {
    int32_t AION;       /* AI-controlled */
    int32_t AIGoER;     /* declares this phase */
    int32_t teamAdder;  /* 0..2, speed order within the team */

    int32_t movesA[SONNY_AI_MOVES];
    int32_t CDArrayA[SONNY_AI_MOVES];
    int32_t movesA_count;
    int32_t movesD[SONNY_AI_MOVES];
    int32_t CDArrayD[SONNY_AI_MOVES];
    int32_t movesD_count;

    int32_t Aggression;
    int32_t LifeBoundary1;
    int32_t LifeBoundary2;
    int32_t FocusAggression;
} Brain;

typedef enum {
    PHASE_DECLARE = 0,
    PHASE_RESOLVE,
    PHASE_OVER
} BattlePhase;

typedef struct {
    Unit        units[SONNY_SLOTS];
    Brain       brains[SONNY_SLOTS];

    int32_t     TeamMove;       /* the faster team this round */
    int32_t     TeamMoveNow;    /* the team acting in this phase */
    int32_t     PrevTeam;
    int32_t     TurnTime;       /* phases left in the round */
    /* Queue cursor. It runs across the whole round, not the phase: the first
       team's three moves sit at 0..2 and the second team's at 3..5, so it
       advances 0,1,2 then 3,4,5 and is only reset when the round rolls over.
       The original wraps it at 6, which this mirrors. */
    int32_t     PlayerToMove;
    int32_t     Cycler;         /* moves left to resolve in this phase */
    int32_t     s1tm, s2tm;     /* where each team's moves sit in the queue */

    QueuedMove  queue[SONNY_QUEUE];
    double      healedThisTurn[SONNY_SLOTS];

    BattlePhase phase;
    int32_t     round;
    int32_t     winCondition;   /* -1 running, 0 lost, 1 won, 2 draw */
    int32_t     playerNumber;   /* the human's slot */

    Rng         rng;
} Battle;

/* What a single resolution step did, for the UI and for tests. */
typedef struct {
    int32_t  caster;
    int32_t  target;
    int32_t  moveID;
    MoveKind kind;
    int32_t  pierced;
    int32_t  amount;        /* damage dealt, health healed, or focus gained */
    int32_t  absorbed;      /* by a shield */
    int32_t  missed;        /* the move could not be paid for or had no target */
    int32_t  target_died;
} MoveEvent;

void battle_init(Battle *b, uint64_t seed, int32_t playerNumber);

/* Put an enemy (or AI ally) in a slot, built the way krinAddNewUnit does:
 * stats scale linearly with level and are not rounded, health is the vitality
 * figure times eight rounded once, and piercing and defense rise by 5 a level
 * over the template's own values.
 *
 * This is deliberately not how the player is built -- see
 * battle_place_character -- because the original uses two different formulas. */
void battle_place_enemy(Battle *b, int32_t slot, const UnitTemplate *t,
                        int32_t level, int32_t ai);

/* Put the player character in a slot, from the stats the character screen
   derives. Health is that figure times eight, as frame 196 sets it. */
void battle_place_character(Battle *b, int32_t slot, const Character *c,
                            const char *name, int32_t ai);

/* TeamSelect + TeamSpeedAdder. */
void battle_team_select(Battle *b);
/* krinAddMove. */
void battle_queue(Battle *b, int32_t caster, int32_t target, int32_t moveID,
                  int32_t autoMove);
/* AImoveAdder for one slot. */
void battle_ai_declare(Battle *b, int32_t slot);
/* Run the declaration step for the whole phase. Units without AI must already
   have queued a move; a dead or absent one passes. */
void battle_declare_phase(Battle *b);
/* Resolve the next queued move. Returns 0 once this phase's three moves are
   done. */
int  battle_resolve_step(Battle *b, MoveEvent *event);
/* End the phase: TurnTime--, then either flip teams or start a new round. */
void battle_end_phase(Battle *b);
/* Drive declare/resolve/phase-end until the battle ends or the cap is hit.
   Returns the number of phases run. */
int32_t battle_run(Battle *b, int32_t max_phases);

void battle_check_win(Battle *b);
int  battle_team_alive(const Battle *b, int32_t team);

#endif
