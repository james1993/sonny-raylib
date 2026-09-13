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

/* elementColorArray, in element order. */
static const uint32_t ELEMENT_COLORS[SONNY_ELEMENTS] = {
    0xC40000, 0xFB95C8, 0x68CBF4, 0xFF6600,
    0xFFCC00, 0x856B47, 0x664D80, 0x508349
};

uint32_t element_color(Element e)
{
    return (e >= 0 && e < SONNY_ELEMENTS) ? ELEMENT_COLORS[e] : 0xFFFFFF;
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
