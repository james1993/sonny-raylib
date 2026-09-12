/* A combatant, mirroring the fields the original's unit objects carry through
 * combat. Names follow the ActionScript so the port stays checkable against it:
 * LIFEN/LIFEU are current/max health, FOCUSN/FOCUSU current/max focus, and the
 * *U suffixed stats are the buffed ("used") values the damage math reads.
 */
#ifndef SONNY_UNIT_H
#define SONNY_UNIT_H

#include <stdint.h>

#define SONNY_ELEMENTS   8
#define SONNY_NAME_LEN   32
/* _root.maxBuffLimit */
#define SONNY_MAX_BUFFS  40
#define SONNY_MAX_MOVES  16
/* FILTERSBUFFARRAY and DOTTICKERARRAY, sized as the original initialises them. */
#define SONNY_FILTERS    9
#define SONNY_DOTTICKER  10
/* changeArray, of which applyChangesKrin reads the first 12. */
#define SONNY_CHANGES    21

/* Index order of _root.elementMainArray. Abilities name their element as a
   string; it indexes the per-element piercing/defense arrays. */
typedef enum {
    ELEM_PHYSICAL = 0,
    ELEM_MAGIC,
    ELEM_ICE,
    ELEM_FIRE,
    ELEM_LIGHTNING,
    ELEM_EARTH,
    ELEM_SHADOW,
    ELEM_POISON
} Element;

const char *element_name(Element e);
/* Returns -1 for an unknown name. */
int element_from_name(const char *name);

/* One slot of BUFFARRAYK. CD 0 means the slot is free. */
typedef struct {
    char    buffId[SONNY_NAME_LEN];  /* "None" when free */
    int32_t CD;                      /* turns remaining */
    double  buffValue;               /* the DoT/HoT amount this instance rolled */
} BuffSlot;

typedef struct {
    char    name[SONNY_NAME_LEN];
    int32_t playerID;
    int32_t teamSide;                /* 1 or 2 */
    int32_t plevel;
    int32_t active;

    /* Base stats, before buffs. */
    double  LIFE, STRENGTH, MAGIC, SPEED;
    double  PER[SONNY_ELEMENTS];
    double  DEF[SONNY_ELEMENTS];

    /* Current and buffed values, which the combat math reads. */
    int32_t LIFEN, LIFEU;
    int32_t FOCUSN, FOCUSU;
    double  STRENGTHU, MAGICU, SPEEDU;
    double  PERU[SONNY_ELEMENTS];
    double  DEFU[SONNY_ELEMENTS];

    int32_t SHIELD;
    int32_t SHIELDCOUNTER;
    int32_t STUN;
    int32_t STUNP;                   /* STUN as of the previous update */
    int32_t REFLECT;
    int32_t SSWITCH;                 /* > 0 = incoming damage heals instead */

    double  DMG;                     /* flat damage the attacker adds */
    double  DMG2;                    /* fractional damage bonus, attacker */
    double  IDMG;                    /* flat damage the target takes extra */
    double  IDMG2;                   /* fractional, target */

    /* Appearance, as the battle screen needs it to assemble the doll: the
       model's gender/skin/hair and the look string of each equipment slot. */
    char    model_gender[8];
    char    model_skin[24];
    char    model_hair[24];
    char    looks[7][24];

    /* Accumulated buff contributions. changeArray is
       [str+, str%, mag+, mag%, spd+, spd%, life+, life%, DMG, DMG2, IDMG,
       IDMG2, ...]; the EP/ED pairs are per-element piercing and defense,
       flat then fractional. */
    double  changeArray[SONNY_CHANGES];
    double  changeArrayEP[SONNY_ELEMENTS];
    double  changeArrayEP2[SONNY_ELEMENTS];
    double  changeArrayED[SONNY_ELEMENTS];
    double  changeArrayED2[SONNY_ELEMENTS];

    /* Per-element damage per turn; [8] is unscaled (healing over time) and
       [9] is focus drained per turn. */
    double  DOTTICKERARRAY[SONNY_DOTTICKER];
    int32_t FILTERSBUFFARRAY[SONNY_FILTERS];
    BuffSlot BUFFARRAYK[SONNY_MAX_BUFFS];
} Unit;

/* Zero a unit and put its arrays in the state frame 196 leaves them in. */
void unit_init(Unit *u, int32_t playerID);

#endif
