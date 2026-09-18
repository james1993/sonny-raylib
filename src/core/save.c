#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "save.h"

#define SAVE_VERSION 1

static void write_int_array(FILE *fh, const char *key, const int32_t *values,
                            int32_t count)
{
    fprintf(fh, "%s", key);
    for (int32_t i = 0; i < count; i++)
        fprintf(fh, " %d", values[i]);
    fprintf(fh, "\n");
}

const char *save_slot_path(int32_t slot)
{
    /* The original keeps each run in its own SharedObject, "sonny_slot1"
       upward, beside where the game is played. */
    static char path[32];
    if (slot < 1 || slot > SONNY_SAVE_SLOTS)
        slot = 1;
    snprintf(path, sizeof(path), "sonny_slot%d.txt", (int)slot);
    return path;
}

int save_write(const Campaign *c, const char *path)
{
    FILE *fh = fopen(path, "w");
    if (!fh)
        return -1;

    fprintf(fh, "sonny-save %d\n", SAVE_VERSION);
    fprintf(fh, "class %d\n",
            c->player.class_template ? c->player.class_template->id : 1);
    fprintf(fh, "level %d\n", c->player.level);
    fprintf(fh, "xp %.10g\n", c->player.xp);
    fprintf(fh, "euros %d\n", c->euros);
    fprintf(fh, "progress %d\n", c->progress_battle);
    fprintf(fh, "zone %d\n", c->zone);
    fprintf(fh, "spent_skill %d\n", c->player.spent_skill_points);
    fprintf(fh, "spent_stat %d\n", c->player.spent_stat_points);
    fprintf(fh, "sets");
    for (int32_t i = 0; i < SONNY_STATS; i++)
        fprintf(fh, " %.10g", c->player.stat_sets[i]);
    fprintf(fh, "\nper");
    for (int32_t e = 0; e < SONNY_ELEMENTS; e++)
        fprintf(fh, " %.10g", c->player.per_sets[e]);
    fprintf(fh, "\ndef");
    for (int32_t e = 0; e < SONNY_ELEMENTS; e++)
        fprintf(fh, " %.10g", c->player.def_sets[e]);
    fprintf(fh, "\n");
    fprintf(fh, "friends");
    for (int32_t i = 0; i < SONNY_PARTY_SIZE; i++)
        fprintf(fh, " %d", c->friends[i]);
    fprintf(fh, "\nline %d %d\n", c->line[0], c->line[1]);

    fprintf(fh, "stats");
    for (int32_t i = 0; i < SONNY_STATS; i++)
        fprintf(fh, " %.10g", c->player.spent[i]);
    fprintf(fh, "\n");

    /* The gameplay tally. The original saves the same six --
       achivementSingleDataKeys plus bgElementsInteracted -- so the settings
       screen still shows the run's after it is loaded back. */
    fprintf(fh, "tally %d %d %d %d %d\n",
            c->stats.zones_cleared, c->stats.respec_used,
            c->stats.training_used, c->stats.top_physical,
            c->stats.top_elemental);
    write_int_array(fh, "scenery", c->stats.scenery, SONNY_SCENERY);

    write_int_array(fh, "equip", c->player.equip, SONNY_EQUIP_SLOTS);
    write_int_array(fh, "bar", c->player.move_matrix, SONNY_MOVE_SLOTS);
    write_int_array(fh, "ranks", c->player.rank, SONNY_TALENT_MAX);
    write_int_array(fh, "skilladder", c->player.skill_adder, SONNY_TALENT_MAX);
    write_int_array(fh, "inventory", c->inventory, c->inventory_count);

    /* Passive talent buff keys, which carry their rank in the name. */
    for (int32_t i = 0; i < SONNY_TALENT_MAX; i++)
        if (c->player.buff_adder[i][0])
            fprintf(fh, "passive %d %s\n", i, c->player.buff_adder[i]);

    fclose(fh);
    return 0;
}

static int32_t read_int_array(const char *line, int32_t *out, int32_t max)
{
    int32_t n = 0;
    const char *p = line;
    while (n < max) {
        char *end;
        long value = strtol(p, &end, 10);
        if (end == p)
            break;
        out[n++] = (int32_t)value;
        p = end;
    }
    return n;
}

int save_read(Campaign *c, const char *path)
{
    FILE *fh = fopen(path, "r");
    if (!fh)
        return -1;

    char line[4096];
    if (!fgets(line, sizeof(line), fh)) {
        fclose(fh);
        return -1;
    }
    int version = 0;
    if (sscanf(line, "sonny-save %d", &version) != 1 || version != SAVE_VERSION) {
        fclose(fh);
        return -1;
    }

    campaign_new(c, 1);
    c->player.spent[1] = 0;     /* the starting bonus is in the save */
    c->player.spent[3] = 0;
    memset(c->player.stat_sets, 0, sizeof(c->player.stat_sets));
    memset(c->player.per_sets, 0, sizeof(c->player.per_sets));
    memset(c->player.def_sets, 0, sizeof(c->player.def_sets));
    memset(c->player.move_matrix, 0, sizeof(c->player.move_matrix));

    while (fgets(line, sizeof(line), fh)) {
        char key[32];
        if (sscanf(line, "%31s", key) != 1)
            continue;
        const char *rest = line + strlen(key);

        if (strcmp(key, "class") == 0) {
            int32_t id = 1;
            sscanf(rest, "%d", &id);
            c->player.class_template = unit_template_by_id(id);
        } else if (strcmp(key, "level") == 0) {
            sscanf(rest, "%d", &c->player.level);
        } else if (strcmp(key, "xp") == 0) {
            sscanf(rest, "%lf", &c->player.xp);
        } else if (strcmp(key, "euros") == 0) {
            sscanf(rest, "%d", &c->euros);
        } else if (strcmp(key, "progress") == 0) {
            sscanf(rest, "%d", &c->progress_battle);
        } else if (strcmp(key, "zone") == 0) {
            sscanf(rest, "%d", &c->zone);
        } else if (strcmp(key, "spent_skill") == 0) {
            sscanf(rest, "%d", &c->player.spent_skill_points);
        } else if (strcmp(key, "spent_stat") == 0) {
            sscanf(rest, "%d", &c->player.spent_stat_points);
        } else if (strcmp(key, "sets") == 0) {
            const char *p = rest;
            for (int32_t i = 0; i < SONNY_STATS; i++) {
                char *end;
                c->player.stat_sets[i] = strtod(p, &end);
                if (end == p)
                    break;
                p = end;
            }
        } else if (strcmp(key, "per") == 0 || strcmp(key, "def") == 0) {
            double *into = (key[0] == 'p') ? c->player.per_sets
                                           : c->player.def_sets;
            const char *p = rest;
            for (int32_t e = 0; e < SONNY_ELEMENTS; e++) {
                char *end;
                into[e] = strtod(p, &end);
                if (end == p)
                    break;
                p = end;
            }
        } else if (strcmp(key, "friends") == 0) {
            read_int_array(rest, c->friends, SONNY_PARTY_SIZE);
        } else if (strcmp(key, "line") == 0) {
            read_int_array(rest, c->line, SONNY_MAX_ALLIES);
        } else if (strcmp(key, "stats") == 0) {
            const char *p = rest;
            for (int32_t i = 0; i < SONNY_STATS; i++) {
                char *end;
                c->player.spent[i] = strtod(p, &end);
                if (end == p)
                    break;
                p = end;
            }
        } else if (strcmp(key, "tally") == 0) {
            int32_t t[5] = {0, 0, 0, 0, 0};
            read_int_array(rest, t, 5);
            c->stats.zones_cleared = t[0];
            c->stats.respec_used = t[1];
            c->stats.training_used = t[2];
            c->stats.top_physical = t[3];
            c->stats.top_elemental = t[4];
        } else if (strcmp(key, "scenery") == 0) {
            read_int_array(rest, c->stats.scenery, SONNY_SCENERY);
        } else if (strcmp(key, "equip") == 0) {
            read_int_array(rest, c->player.equip, SONNY_EQUIP_SLOTS);
        } else if (strcmp(key, "bar") == 0) {
            read_int_array(rest, c->player.move_matrix, SONNY_MOVE_SLOTS);
        } else if (strcmp(key, "ranks") == 0) {
            read_int_array(rest, c->player.rank, SONNY_TALENT_MAX);
        } else if (strcmp(key, "skilladder") == 0) {
            read_int_array(rest, c->player.skill_adder, SONNY_TALENT_MAX);
        } else if (strcmp(key, "inventory") == 0) {
            c->inventory_count = read_int_array(
                rest, c->inventory,
                (int32_t)(sizeof(c->inventory) / sizeof(c->inventory[0])));
        } else if (strcmp(key, "passive") == 0) {
            int32_t node = 0;
            char buff[SONNY_NAME_LEN] = {0};
            if (sscanf(rest, "%d %31s", &node, buff) == 2
                && node >= 0 && node < SONNY_TALENT_MAX)
                snprintf(c->player.buff_adder[node],
                         sizeof(c->player.buff_adder[node]), "%s", buff);
        }
    }
    fclose(fh);
    return 0;
}
