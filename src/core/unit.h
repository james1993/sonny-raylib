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
#define SONNY_MAX_BUFFS  16
#define SONNY_MAX_MOVES  16

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

typedef struct {
    char    name[SONNY_NAME_LEN];
    int32_t playerID;
    int32_t teamSide;
    int32_t plevel;
    int32_t active;

    int32_t LIFEN, LIFEU;
    int32_t FOCUSN, FOCUSU;
    double  STRENGTHU, MAGICU, SPEEDU;
    double  PERU[SONNY_ELEMENTS];   /* piercing, per element */
    double  DEFU[SONNY_ELEMENTS];   /* defense, per element */

    int32_t SHIELD;
    int32_t STUN;
    int32_t SSWITCH;                /* 1 = incoming damage heals instead */

    double  DMG;                    /* flat damage the attacker adds */
    double  DMG2;                   /* fractional damage bonus, attacker */
    double  IDMG;                   /* flat damage the target takes extra */
    double  IDMG2;                  /* fractional, target */
} Unit;

#endif
