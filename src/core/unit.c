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
