#include <stdio.h>
#include <string.h>
#include "unit.h"

static const char *const ELEMENT_NAMES[SONNY_ELEMENTS] = {
    "Physical", "Magic", "Ice", "Fire", "Lightning", "Earth", "Shadow", "Poison"
};

const char *element_name(Element e)
{
    return (e >= 0 && e < SONNY_ELEMENTS) ? ELEMENT_NAMES[e] : "?";
}

int element_from_name(const char *name)
{
    if (!name)
        return -1;
    for (int i = 0; i < SONNY_ELEMENTS; i++)
        if (strcmp(name, ELEMENT_NAMES[i]) == 0)
            return i;
    return -1;
}

void unit_init(Unit *u, int32_t playerID)
{
    memset(u, 0, sizeof(*u));
    u->playerID = playerID;
    /* teamSide from slot parity, as Math.pow(-1,i) < 0 does. */
    u->teamSide = (playerID % 2) ? 1 : 2;
    u->plevel = 1;
    u->active = 0;
    for (int i = 0; i < SONNY_MAX_BUFFS; i++) {
        snprintf(u->BUFFARRAYK[i].buffId, SONNY_NAME_LEN, "None");
        u->BUFFARRAYK[i].CD = 0;
        u->BUFFARRAYK[i].buffValue = 0;
    }
}
