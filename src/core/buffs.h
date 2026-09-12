/* Buffs and debuffs, ported from addNewBuffKrin / applyBuffKrin / buffTicker /
 * applyChangesKrin in frame 62.
 *
 * A buff definition is the KRINBUFF array the original builds: 32 slots whose
 * meaning is fixed by how applyBuffKrin reads them. The named fields below are
 * those reads; slots the combat code never touches (20, 27) are display-only
 * and kept only so the table round-trips.
 */
#ifndef SONNY_BUFFS_H
#define SONNY_BUFFS_H

#include "unit.h"

#define SONNY_BUFF_FIELDS 32

typedef struct {
    char    key[SONNY_NAME_LEN];    /* e.g. "WOUND1" -- how abilities name it */
    char    name[SONNY_NAME_LEN];   /* [0]  display name */
    int32_t element;                /* [1]  which element's arrays it touches */
    double  change[12];             /* [2..13] -> changeArray[0..11] */
    double  dot_flat;               /* [14] damage per turn, flat */
    double  focus_drain;            /* [15] -> DOTTICKERARRAY[9] */
    int32_t duration;               /* [16] turns */
    int32_t stun;                   /* [17] */
    int32_t reflect;                /* [18] */
    int32_t shield;                 /* [19] */
    double  per_flat;               /* [21] piercing, flat */
    double  def_flat;               /* [22] defense, flat */
    double  per_pct;                /* [23] piercing, fractional */
    double  def_pct;                /* [24] defense, fractional */
    int32_t sswitch;                /* [26] */
    double  dot_strength;           /* [28] damage per turn from caster STR */
    double  dot_magic;              /* [29] ... from caster MAGIC */
    double  dot_speed;              /* [30] ... from caster SPEED */
    int32_t filter;                 /* [31] visual filter slot */
} BuffDef;

/* applyBuffKrin(target, buff, iftbc, caster, debuffValue).
 * iftbc is 1 to apply and -1 to remove; `caster` scales the damage-per-turn on
 * application and is unused on removal, where debuffValue is the amount the
 * instance originally rolled. */
void buff_apply(Unit *target, const BuffDef *b, int32_t iftbc,
                const Unit *caster, double debuffValue);

/* applyChangesKrin: recompute the buffed stats from base + accumulated changes.
 * Also mirrors the original's max-health handling, which preserves the health
 * fraction when max drops and never drops a living unit to zero. */
void unit_apply_changes(Unit *u);

typedef struct {
    double  total_damage;   /* after elements and shields; negative = healed */
    double  focus_drained;
    int32_t died;
} TickResult;

/* buffTicker: one end-of-turn pass. Buff durations tick down here, and buffs
 * that expire are removed after this turn's damage has already been counted.
 * `lib`/`lib_count` resolve expiring buffs by key. */
TickResult buff_tick(Unit *u, const BuffDef *lib, int32_t lib_count);

const BuffDef *buff_find(const BuffDef *lib, int32_t lib_count, const char *key);

#endif
